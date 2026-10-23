//go:build windows

package desktopruntime

import (
	"archive/zip"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	processcontrol "github.com/uvwt/agentdock/internal/process"
)

const (
	ngrokTokenEntropy     = "agentdock.ngrok.tunnel.v1"
	ngrokInspectorURL     = "http://127.0.0.1:4067/api/tunnels"
	ngrokProvisionTimeout = 40 * time.Second
)

// Local ngrok is optional. First use an already installed binary; otherwise
// fetch only the official Windows agent and verify its Authenticode signature.
func availableNgrokBinary(root string) string {
	installed := filepath.Join(root, "bin", "ngrok.exe")
	if info, err := os.Stat(installed); err == nil && info.Mode().IsRegular() {
		return installed
	}
	// Never adopt or terminate an unrelated ngrok installed elsewhere on PATH.
	return ""
}

func resolveNgrokBinary(ctx context.Context, root string) (string, error) {
	if path := availableNgrokBinary(root); path != "" {
		return path, nil
	}
	arch := runtime.GOARCH
	if arch != "amd64" && arch != "arm64" {
		return "", fmt.Errorf("unsupported ngrok Windows architecture: %s", arch)
	}
	source := fmt.Sprintf("https://bin.ngrok.com/c/bNyj1mQVY4c/ngrok-v3-stable-windows-%s.zip", arch)
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, source, nil)
	if err != nil {
		return "", err
	}
	client := &http.Client{Timeout: 65 * time.Second}
	response, err := client.Do(request)
	if err != nil {
		return "", fmt.Errorf("download ngrok from official site: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return "", fmt.Errorf("ngrok download failed: HTTP %d", response.StatusCode)
	}
	archive, err := io.ReadAll(io.LimitReader(response.Body, 110<<20))
	if err != nil {
		return "", err
	}
	if len(archive) == 110<<20 {
		return "", errors.New("ngrok ZIP exceeded 110 MiB limit")
	}
	reader, err := zip.NewReader(bytes.NewReader(archive), int64(len(archive)))
	if err != nil {
		return "", fmt.Errorf("invalid ngrok download ZIP: %w", err)
	}
	var executable *zip.File
	for _, entry := range reader.File {
		if entry.Name == "ngrok.exe" && !entry.FileInfo().IsDir() {
			executable = entry
			break
		}
	}
	if executable == nil || executable.UncompressedSize64 > 100<<20 {
		return "", errors.New("official ngrok ZIP does not contain a valid ngrok.exe")
	}
	binaryDirectory := filepath.Join(root, "bin")
	if err := os.MkdirAll(binaryDirectory, 0o700); err != nil {
		return "", err
	}
	file, err := os.CreateTemp(binaryDirectory, ".ngrok-download-*.exe")
	if err != nil {
		return "", err
	}
	tmp := file.Name()
	defer os.Remove(tmp)
	input, err := executable.Open()
	if err != nil {
		file.Close()
		return "", err
	}
	_, copyErr := io.Copy(file, io.LimitReader(input, 100<<20))
	closeErr := file.Close()
	input.Close()
	if copyErr != nil {
		return "", copyErr
	}
	if closeErr != nil {
		return "", closeErr
	}
	if err := verifyNgrokSignature(ctx, tmp); err != nil {
		return "", err
	}
	destination := filepath.Join(binaryDirectory, "ngrok.exe")
	if err := os.Rename(tmp, destination); err != nil {
		// Concurrent first installs are harmless if another verified binary won.
		if path := availableNgrokBinary(root); path != "" {
			return path, nil
		}
		return "", fmt.Errorf("install ngrok agent: %w", err)
	}
	return destination, nil
}

func verifyNgrokSignature(ctx context.Context, path string) error {
	// The trust check verifies the Windows certificate chain and publisher,
	// not merely the HTTPS download host.
	escaped := strings.ReplaceAll(path, "'", "''")
	script := "$sig=Get-AuthenticodeSignature -LiteralPath '" + escaped +
		"'; if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=\"ngrok, Inc\\.\"'){exit 7}"
	command := exec.CommandContext(ctx, "powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script)
	if output, err := command.CombinedOutput(); err != nil {
		return fmt.Errorf("downloaded ngrok.exe is not Authenticode-signed by ngrok, Inc.: %w (%s)", err, strings.TrimSpace(string(output)))
	}
	return nil
}

func ngrokAgentCommand(ctx context.Context, runtime tunnelRuntime) (*exec.Cmd, error) {
	token, err := readProtectedText(filepath.Join(runtime.root, "ngrok-token.dpapi"), ngrokTokenEntropy)
	if err != nil {
		return nil, fmt.Errorf("read protected ngrok authtoken: %w", err)
	}
	if strings.TrimSpace(token) == "" {
		return nil, errors.New("ngrok authtoken is required")
	}
	// A token-free config isolates inspector settings from the user's global
	// ngrok profile. Auth stays in DPAPI at rest and a subprocess environment
	// variable at runtime, never in argv or the on-disk YAML file.
	config := filepath.Join(runtime.root, "ngrok-isolated.yml")
	if err := writeRuntimeText(config, "version: 3\nagent:\n  web_addr: 127.0.0.1:4067\n"); err != nil {
		return nil, err
	}
	arguments := []string{"http", fmt.Sprintf("http://127.0.0.1:%d", runtime.settings.Port), "--config", config}
	domain, err := readTrimmedText(filepath.Join(runtime.root, "ngrok-domain.txt"))
	if err != nil {
		return nil, err
	}
	if domain != "" {
		origin, err := normalizeHTTPSOrigin(domain)
		if err != nil {
			return nil, err
		}
		arguments = append(arguments, "--url", origin)
	}
	command := exec.CommandContext(ctx, runtime.manifest.CloudflaredBinary, arguments...)
	command.Env = append(environmentWithout(os.Environ(), "NGROK_AUTHTOKEN"), "NGROK_AUTHTOKEN="+token)
	command.Dir = runtime.root
	processcontrol.Configure(command)
	return command, nil
}

func readNgrokInspectorURL(ctx context.Context, port int) (string, error) {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, ngrokInspectorURL, nil)
	if err != nil {
		return "", err
	}
	client := &http.Client{Timeout: 1500 * time.Millisecond}
	response, err := client.Do(request)
	if err != nil {
		return "", err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return "", fmt.Errorf("ngrok inspector HTTP %d", response.StatusCode)
	}
	var payload struct {
		Tunnels []struct {
			PublicURL string `json:"public_url"`
			Config    struct {
				Addr string `json:"addr"`
			} `json:"config"`
		} `json:"tunnels"`
	}
	if err := json.NewDecoder(io.LimitReader(response.Body, 1<<20)).Decode(&payload); err != nil {
		return "", err
	}
	expected := fmt.Sprintf("127.0.0.1:%d", port)
	for _, tunnel := range payload.Tunnels {
		parsed, err := url.Parse(strings.TrimSpace(tunnel.PublicURL))
		if err != nil || parsed.Scheme != "https" || parsed.Host == "" || parsed.User != nil {
			continue
		}
		destination := strings.TrimPrefix(strings.TrimPrefix(tunnel.Config.Addr, "http://"), "https://")
		destination = strings.TrimRight(destination, "/")
		if destination != expected && destination != fmt.Sprintf("localhost:%d", port) && destination != fmt.Sprintf("%d", port) {
			continue // an unrelated ngrok tunnel must never be adopted
		}
		return "https://" + parsed.Host, nil
	}
	return "", errors.New("ngrok inspector has not registered an HTTPS tunnel for AgentDock")
}

func waitNgrokURL(ctx context.Context, runtime tunnelRuntime, timeout time.Duration) (string, error) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		url, err := readNgrokInspectorURL(ctx, runtime.settings.Port)
		if err == nil && url != "" {
			return url, nil
		}
		running, checkErr := processRunningAtPath(runtime.manifest.CloudflaredBinary)
		if checkErr != nil {
			return "", checkErr
		}
		if !running {
			return "", errors.New("ngrok exited before registering a public HTTPS URL; check tunnel logs and account token")
		}
		select {
		case <-ctx.Done():
			return "", ctx.Err()
		case <-time.After(500 * time.Millisecond):
		}
	}
	return "", fmt.Errorf("ngrok did not publish an HTTPS endpoint within %s", timeout)
}

func runNgrokOnce(ctx context.Context, runtime tunnelRuntime, logs *processLogs) error {
	if err := verifyNgrokCoreAuthentication(ctx, runtime); err != nil {
		return err
	}
	command, err := ngrokAgentCommand(ctx, runtime)
	if err != nil {
		return err
	}
	command.Stdout = logs.stdout
	command.Stderr = logs.stderr
	if err := command.Start(); err != nil {
		return err
	}
	origin, err := waitNgrokURL(ctx, runtime, ngrokProvisionTimeout)
	if err != nil {
		_ = command.Process.Kill()
		_ = command.Wait()
		return err
	}
	if err := applyQuickTunnelURL(ctx, runtime, origin); err != nil {
		_ = command.Process.Kill()
		_ = command.Wait()
		return err
	}
	return command.Wait()
}

func verifyNgrokCoreAuthentication(ctx context.Context, runtime tunnelRuntime) error {
	token, err := readProtectedText(filepath.Join(runtime.root, "auth-token.dpapi"), "agentdock.startup.v1")
	if err != nil || strings.TrimSpace(token) == "" {
		return errors.New("AgentDock Bearer authentication is not configured; refusing to expose the core")
	}
	endpoint := fmt.Sprintf("http://127.0.0.1:%d/mcp", runtime.settings.Port)
	client := &http.Client{Timeout: 2400 * time.Millisecond}
	for _, authenticated := range []bool{false, true} {
		req, err := http.NewRequestWithContext(ctx, http.MethodGet, endpoint, nil)
		if err != nil {
			return err
		}
		req.Header.Set("Accept", "application/json, text/event-stream")
		if authenticated {
			req.Header.Set("Authorization", "Bearer "+token)
		}
		reply, err := client.Do(req)
		if err != nil {
			return fmt.Errorf("AgentDock core unavailable; refusing public access: %w", err)
		}
		io.Copy(io.Discard, io.LimitReader(reply.Body, 4096))
		reply.Body.Close()
		if !authenticated && reply.StatusCode != http.StatusUnauthorized {
			return fmt.Errorf("AgentDock core failed anonymous Bearer check (HTTP %d); refusing public access", reply.StatusCode)
		}
		if authenticated {
			switch reply.StatusCode {
			case http.StatusOK, http.StatusAccepted, http.StatusNoContent,
				http.StatusBadRequest, http.StatusMethodNotAllowed, http.StatusNotAcceptable:
			default:
				return fmt.Errorf("AgentDock core rejected its protected Bearer token (HTTP %d); refusing public access", reply.StatusCode)
			}
		}
	}
	return nil
}

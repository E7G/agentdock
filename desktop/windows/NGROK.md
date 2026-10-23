# AgentDock Windows: ngrok HTTPS

This fork extends the **original** AgentDock WinUI control panel and Go tunnel supervisor. It does not install or require the separate Qt frontend.

## Start ngrok

1. Open AgentDock > Connections > Advanced connection settings.
2. Find the **ngrok** card. Enter your account's ngrok Authtoken. Optionally enter an assigned HTTPS domain, for example `https://example.ngrok-free.app`.
3. Click **Enable ngrok HTTPS**. The first launch installs the official Windows ngrok agent under the AgentDock runtime `bin` directory after verifying the executable's Windows Authenticode publisher signature.
4. When connected, copy the public MCP URL ending in `/mcp`. Remote clients must pass your **AgentDock Bearer token** (not the ngrok Authtoken) in `Authorization: Bearer ...`.
5. Use **Disable ngrok** to stop the tunnel and remove the public endpoint. The original Cloudflare Tunnel options remain available as an alternative.

The tunnel runs under the existing AgentDock background supervisor and uses the current AgentDock service port. It automatically restarts after unexpected ngrok exits. The core is only exposed after its local Bearer authentication is confirmed to deny anonymous MCP requests and accept its stored token. The supervisor updates the original core's public URL so OAuth-origin handling stays aligned.

ngrok account credentials are DPAPI-protected at rest in `ngrok-token.dpapi`. The downloaded agent comes from ngrok's official HTTPS host, and its Authenticode signer must be ngrok, Inc. The token is passed through a subprocess environment variable, not CLI arguments or an on-disk ngrok YAML config. An Internet connection and a valid ngrok account token are required for a real public tunnel; compilation and tests do not establish one.

**Windows x64:** The packaged installer and portable ZIP retain upstream AgentDock's directory layout. The build is unsigned; Windows may show a SmartScreen prompt. Stop the installed AgentDock services before manually replacing binaries. Prefer the full installer rather than copying one executable over an active installation.

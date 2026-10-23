param(
    [string]$CorePath = (Join-Path $PSScriptRoot '..\build-win\Release\agentdock.exe'),
    [int]$Port = 39771
)
$ErrorActionPreference = 'Stop'
$workingHome = Join-Path $env:TEMP ('agentdock-qt-auth-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workingHome -Force | Out-Null
$oldEnv = @{}
$keys = @('AGENTDOCK_HOME','AGENTDOCK_HOST','AGENTDOCK_PORT','AGENTDOCK_AUTH_TOKEN','AGENTDOCK_RUNTIME_ROOT','AGENTDOCK_OAUTH_ENABLED')
foreach ($key in $keys) { $oldEnv[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
$token = 'integration-test-only-token-1'
$env:AGENTDOCK_HOME = $workingHome
$env:AGENTDOCK_HOST = '127.0.0.1'
$env:AGENTDOCK_PORT = [string]$Port
$env:AGENTDOCK_AUTH_TOKEN = $token
$env:AGENTDOCK_RUNTIME_ROOT = ''
$env:AGENTDOCK_OAUTH_ENABLED = 'false'
$proc = $null
$passed = 0
function Assert($ok, [string]$description) {
    if (!$ok) { throw "FAILED: $description" }
    $script:passed++
    Write-Output "PASS: $description"
}
function HttpStatus([string]$endpoint, [string]$bearer = '') {
    $headers = @{Accept='application/json, text/event-stream'}
    if ($bearer) { $headers['Authorization'] = 'Bearer ' + $bearer }
    try {
        $response = Invoke-WebRequest -UseBasicParsing -Uri $endpoint -Headers $headers -Method Get -TimeoutSec 4 -ErrorAction Stop
        return [int]$response.StatusCode
    } catch {
        if ($null -ne $_.Exception.Response) { return [int]$_.Exception.Response.StatusCode }
        throw
    }
}
try {
    $exe = [IO.Path]::GetFullPath($CorePath)
    if (!(Test-Path -LiteralPath $exe)) { throw "AgentDock Go core not found: $exe" }
    $errFile = Join-Path $workingHome 'stderr.log'
    $proc = Start-Process -FilePath $exe -ArgumentList @('--browser-enabled=false') -PassThru -RedirectStandardError $errFile
    $ready = $false
    for ($i=0; $i -lt 45; $i++) {
        Start-Sleep -Milliseconds 350
        if ($proc.HasExited) { throw "AgentDock core exited early: $($proc.ExitCode)" }
        try {
            if ((HttpStatus "http://127.0.0.1:$Port/healthz") -eq 200) { $ready = $true; break }
        } catch { }
    }
    Assert $ready 'AgentDock /healthz running'
    $mcp = "http://127.0.0.1:$Port/mcp"
    Assert ((HttpStatus $mcp) -eq 401) 'AgentDock rejects anonymous MCP access'
    Assert ((HttpStatus $mcp 'wrong-token') -eq 401) 'AgentDock rejects invalid Bearer token'
    $authorizedStatus = HttpStatus $mcp $token
    Assert ($authorizedStatus -in @(200,202,204,400,405,406)) 'AgentDock accepts its native Bearer token'
    $body = '{"jsonrpc":"2.0","id":1,"method":"ping"}'
    $headers = @{ Authorization = ('Bearer ' + $token); Accept = 'application/json, text/event-stream' }
    try {
        $ping = Invoke-WebRequest -UseBasicParsing -Uri $mcp -Method Post -Headers $headers -ContentType 'application/json' -Body $body -TimeoutSec 5 -ErrorAction Stop
        Assert ($ping.StatusCode -in @(200,202)) 'Authenticated MCP JSON-RPC POST'
    } catch {
        $httpCode = if ($_.Exception.Response) { [int]$_.Exception.Response.StatusCode } else { 0 }
        throw "Authenticated MCP POST failed: HTTP $httpCode - $($_.Exception.Message)"
    }
    Write-Output "AgentDock integration: $passed assertions passed."
} finally {
    if ($proc -and !$proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $oldEnv[$key], 'Process') }
    if (Test-Path -LiteralPath $workingHome) { Remove-Item -LiteralPath $workingHome -Recurse -Force -ErrorAction SilentlyContinue }
}

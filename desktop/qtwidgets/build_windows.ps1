param(
    [switch]$SkipGo,
    [switch]$SkipDeploy,
    [string]$QtRoot = $(if ($env:QT_ROOT) { $env:QT_ROOT } elseif ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { 'C:\Qt\6.8.3\msvc2022_64' })
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vswhere = Join-Path (Get-Item 'C:\Program Files (x86)').FullName 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Visual Studio vswhere.exe not found' }
$version = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
if (!$version) { throw 'MSVC C++ toolset is not installed' }
$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsInstall) { throw 'Visual Studio C++ installation path is missing' }
$env:VCINSTALLDIR = Join-Path $vsInstall 'VC\'
$toolset = Get-ChildItem (Join-Path $vsInstall 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
if ($toolset) { $env:VCToolsInstallDir = $toolset.FullName + '\' }
$major = [int]($version.Split('.')[0])
$generator = switch ($major) {
    18 { 'Visual Studio 18 2026' }
    17 { 'Visual Studio 17 2022' }
    default { throw "Unsupported Visual Studio version: $version" }
}
$qtBinary = Join-Path $QtRoot 'bin\windeployqt.exe'
if (!(Test-Path $qtBinary)) { throw "Qt not installed at $QtRoot" }

$build = Join-Path $PSScriptRoot 'build-win'
$dist = Join-Path $PSScriptRoot 'dist'
cmake -S $PSScriptRoot -B $build -G $generator -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build $build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Qt Release build failed' }

$source = Join-Path $build 'Release\agentdock-qt.exe'
if (!(Test-Path $source)) { throw 'Qt executable was not produced' }
if (Test-Path $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $dist -Force
if (!$SkipGo) {
    Push-Location $repo
    try {
        go build -trimpath -o (Join-Path $dist 'agentdock.exe') ./cmd/agentdock
        if ($LASTEXITCODE -ne 0) { throw 'Go core build failed (Go 1.26.5+ required)' }
    } finally { Pop-Location }
} else {
    $old = Join-Path $build 'Release\agentdock.exe'
    if (Test-Path $old) { Copy-Item -LiteralPath $old -Destination $dist -Force }
}
if (!$SkipDeploy) {
    & $qtBinary --release --no-translations --compiler-runtime (Join-Path $dist 'agentdock-qt.exe')
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }
}
Write-Output "Qt desktop build: $dist"
if (!(Test-Path (Join-Path $dist 'agentdock.exe'))) {
    Write-Warning 'No AgentDock Go core is present: build it or select its path in the GUI.'
}

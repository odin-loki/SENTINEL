# Install or verify Windows 11 SDK for MSVC SENTINEL builds.
# Opens Visual Studio Installer when available; otherwise prints exact manual steps.

$ErrorActionPreference = "Stop"

Write-Host "SENTINEL — Windows 11 SDK for MSVC builds"
Write-Host ""

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installPath = $null

if (Test-Path $vswhere) {
    $installPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
}

if (-not $installPath) {
    Write-Host "Visual Studio 2022 (Build Tools or Community) not detected."
    Write-Host ""
    Write-Host "1. Download Visual Studio Build Tools 2022:"
    Write-Host "   https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022"
    Write-Host "2. Run the installer and select:"
    Write-Host "   [x] Desktop development with C++"
    Write-Host "   [x] Windows 11 SDK (10.0.22621.0 or newer)"
    Write-Host "3. After install, open 'x64 Native Tools Command Prompt for VS 2022'"
    Write-Host "4. Re-run: & packaging/windows/build_release.ps1"
    exit 1
}

Write-Host "Found VS install: $installPath"
Write-Host ""

# Check if SDK headers are already present
$sdkOk = $false
if ($env:INCLUDE) {
    foreach ($inc in @($env:INCLUDE -split ';')) {
        if ($inc -and (Test-Path (Join-Path $inc "stddef.h"))) {
            Write-Host "Windows SDK headers OK: $inc"
            $sdkOk = $true
            break
        }
    }
}

if (-not $sdkOk) {
    $vcvars = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"
    if (Test-Path $vcvars) {
        $cmd = if ($env:ComSpec) { $env:ComSpec } else { "C:\Windows\System32\cmd.exe" }
        & $cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
            if ($_ -match '^INCLUDE=(.*)$') {
                foreach ($inc in @($matches[1] -split ';')) {
                    if ($inc -and (Test-Path (Join-Path $inc "stddef.h"))) {
                        Write-Host "Windows SDK headers OK (after vcvars): $inc"
                        $sdkOk = $true
                    }
                }
            }
        }
    }
}

if ($sdkOk) {
    Write-Host ""
    Write-Host "Windows SDK is installed. MSVC build is ready:"
    Write-Host "  & packaging/windows/build_release.ps1"
    exit 0
}

Write-Host "Windows 11 SDK headers not found (stddef.h missing)."
Write-Host ""
Write-Host "Opening Visual Studio Installer — Modify your installation:"
Write-Host "  [x] Desktop development with C++"
Write-Host "  [x] Windows 11 SDK (10.0.22621.0 or newer)"
Write-Host ""

$installer = Join-Path (Split-Path $vswhere -Parent) "setup.exe"
if (Test-Path $installer) {
    Write-Host "Launching: $installer modify ..."
    Start-Process -FilePath $installer -ArgumentList "modify", "--installPath", "`"$installPath`"", `
        "--add", "Microsoft.VisualStudio.Workload.VCTools", `
        "--add", "Microsoft.VisualStudio.Component.Windows11SDK.22621", `
        "--passive", "--norestart"
    Write-Host ""
    Write-Host "When the installer finishes, open a new terminal and run:"
    Write-Host "  & packaging/windows/build_release.ps1"
} else {
    Write-Host "Could not find setup.exe. Open 'Visual Studio Installer' from Start Menu manually."
    Write-Host "Select Modify on VS 2022, enable the components above, then Apply."
}

exit 1

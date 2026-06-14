# Build a native Windows release bundle (MSVC + Qt, ZIP + optional NSIS installer).
#
# Usage:
#   powershell -File packaging/windows/build_release.ps1
#   powershell -File packaging/windows/build_release.ps1 -QtPath "C:/Qt/6.11.0/msvc2022_64"

param(
    [string]$QtPath = "",
    [string]$CmakePath = "C:/Qt/Tools/CMake_64/bin",
    [string]$NinjaPath = "C:/Qt/Tools/Ninja",
    [string]$NsisPath = "C:/Program Files (x86)/NSIS",
    [string]$BuildDir = "build-msvc-release",
    [string]$Version = "1.0.0",
    [string]$InstallRoot = "",
    [ValidateSet("Ninja", "Visual Studio 17 2022")]
    [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

function Find-MsvcQtKit {
    param([string]$Override)
    if ($Override) {
        if (-not (Test-Path (Join-Path $Override "bin/qmake.exe"))) {
            throw "QtPath not found or invalid: $Override"
        }
        return $Override.Replace('\', '/')
    }
    foreach ($kit in @("C:/Qt/6.11.0/msvc2022_64", "C:/Qt/6.6.0/msvc2019_64")) {
        if (Test-Path (Join-Path $kit "bin/qmake.exe")) { return $kit }
    }
    throw "No MSVC Qt kit found under C:/Qt (msvc2022_64 or msvc2019_64)."
}

function Initialize-MsvcEnvironment {
    if ($env:VCINSTALLDIR -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        Write-Host "==> MSVC environment already active ($env:VCINSTALLDIR)"
        return
    }
    $installPath = $null
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
    }
    if (-not $installPath) {
        foreach ($candidate in @(
            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools",
            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community",
            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools"
        )) {
            if (Test-Path (Join-Path $candidate "VC\Auxiliary\Build\vcvars64.bat")) {
                $installPath = $candidate
                break
            }
        }
    }
    if (-not $installPath) {
        throw "Visual Studio C++ tools not found. Run packaging/windows/build_msvc.bat."
    }
    $vcvars = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"
    Write-Host "==> Initializing MSVC environment via $vcvars"
    $cmd = if ($env:ComSpec) { $env:ComSpec } else { "C:\Windows\System32\cmd.exe" }
    & $cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            if ($matches[1] -eq 'PATH') { $env:PATH = $matches[2] }
            else { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
        }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "Failed to initialize MSVC environment (cl.exe not in PATH)"
    }
}

function Get-SentinelExePath {
    param([string]$Root)
    foreach ($candidate in @(
        (Join-Path $Root "bin/sentinel.exe"),
        (Join-Path $Root "sentinel.exe")
    )) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }
    throw "sentinel.exe not found under $Root"
}

function Remove-DirectorySafe {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return }
    try {
        Remove-Item -Recurse -Force $Path -ErrorAction Stop
    } catch {
        $renamed = "${Path}_old_$(Get-Date -Format yyyyMMddHHmmss)"
        Write-Host "    Could not delete $Path; renaming to $renamed"
        Rename-Item -Path $Path -NewName (Split-Path $renamed -Leaf) -ErrorAction SilentlyContinue
    }
}

$QtPath = Find-MsvcQtKit -Override $QtPath
Write-Host "==> Using Qt MSVC kit: $QtPath"
Initialize-MsvcEnvironment

$env:PATH = "$NinjaPath;$CmakePath;$NsisPath;${QtPath}\bin;$env:PATH"
if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
    $clDir = Split-Path (Get-Command cl.exe).Source -Parent
    $env:PATH = "$clDir;$env:PATH"
}
$env:QT_PLUGIN_PATH = "$QtPath/plugins"

$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$Root = (Resolve-Path (Join-Path $ScriptRoot "../..")).Path
Set-Location $Root

if (-not $InstallRoot) {
    $InstallRoot = Join-Path $Root "packaging/pkg"
} elseif (-not [System.IO.Path]::IsPathRooted($InstallRoot)) {
    $InstallRoot = Join-Path $Root $InstallRoot
}
$InstallRoot = (New-Item -ItemType Directory -Force -Path $InstallRoot).FullName

$Dist = Join-Path $Root "dist"
$PortableDir = Join-Path $Dist "SENTINEL-$Version-win64-portable"
Remove-DirectorySafe $InstallRoot
Remove-DirectorySafe $PortableDir
New-Item -ItemType Directory -Force -Path $Dist | Out-Null

Write-Host "==> Configuring Release build (Generator: $Generator)"
$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    $cacheText = Get-Content $cacheFile -Raw
    if ($cacheText -match "mingw" -and $QtPath -match "msvc") {
        Write-Host "    Removing stale MinGW CMake cache in $BuildDir (MSVC build requested)"
        Remove-DirectorySafe $BuildDir
    }
}
$cmakeConfigure = @(
    "-B", $BuildDir,
    "-DCMAKE_PREFIX_PATH=$QtPath",
    "-DCMAKE_INSTALL_PREFIX=$InstallRoot"
)
if ($Generator -eq "Ninja") {
    $cmakeConfigure += @(
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_MAKE_PROGRAM=$NinjaPath/ninja.exe"
    )
    if ($QtPath -match "msvc") {
        $cmakeConfigure += @("-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl")
    }
} else {
    $cmakeConfigure += @("-G", "Visual Studio 17 2022", "-A", "x64")
}
& cmake @cmakeConfigure
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

$configArgs = @()
if ($Generator -eq "Visual Studio 17 2022") { $configArgs = @("--config", "Release") }

Write-Host "==> Building project"
& cmake --build $BuildDir @configArgs -j
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

Write-Host "==> Running tests (ctest -j4, QT_QPA_PLATFORM=offscreen)"
$env:QT_QPA_PLATFORM = "offscreen"
& ctest --test-dir $BuildDir @configArgs --output-on-failure -j4
if ($LASTEXITCODE -ne 0) { throw "ctest failed" }

Write-Host "==> Installing to $InstallRoot"
& cmake --install $BuildDir @configArgs
if ($LASTEXITCODE -ne 0) { throw "cmake install failed" }

$Exe = Get-SentinelExePath -Root $InstallRoot
$BinDir = Split-Path $Exe -Parent
Write-Host "    sentinel.exe: $Exe"

$Windeploy = Join-Path $QtPath "bin/windeployqt.exe"
if (-not (Test-Path $Windeploy)) { throw "windeployqt not found at $Windeploy" }

Write-Host "==> Deploying Qt runtime (windeployqt, MSVC redist)"
& $Windeploy --release --no-translations --compiler-runtime $Exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

$QtCoreDll = Join-Path $BinDir "Qt6Core.dll"
if (-not (Test-Path $QtCoreDll)) {
    throw "Qt6Core.dll not deployed next to sentinel.exe in $BinDir"
}
Write-Host "    Verified: Qt6Core.dll in $BinDir"

if ($env:SIGN_CERT) {
    $signtool = Get-Command signtool -ErrorAction SilentlyContinue
    if ($signtool) {
        Write-Host "==> Code signing $Exe"
        $signArgs = @("sign", "/fd", "SHA256", "/f", $env:SIGN_CERT, $Exe)
        if ($env:SIGN_PASSWORD) { $signArgs += @("/p", $env:SIGN_PASSWORD) }
        & signtool @signArgs
        if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
    } else {
        Write-Host "    SIGN_CERT set but signtool not found - skipping code signing"
    }
}

Write-Host "==> Staging portable layout under dist/"
Copy-Item -Path $InstallRoot -Destination $PortableDir -Recurse -Force

$ZipName = "SENTINEL-$Version-win64-portable.zip"
$ZipPath = Join-Path $Dist $ZipName
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Write-Host "==> Creating portable ZIP: $ZipName"
Compress-Archive -Path (Join-Path $PortableDir "*") -DestinationPath $ZipPath -Force
Write-Host "    $ZipPath"

$Nsis = Get-Command makensis -ErrorAction SilentlyContinue
Push-Location $BuildDir
try {
    if ($Nsis) {
        Write-Host "==> Running CPack (NSIS installer)"
        $cpackOutput = & cpack -G NSIS -C Release 2>&1
        $cpackOutput | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -eq 0) {
            Get-ChildItem -Filter "SENTINEL*.exe" | ForEach-Object {
                Copy-Item $_.FullName -Destination $Dist -Force
                Write-Host "    Installer: $(Join-Path $Dist $_.Name)"
            }
        } else {
            Write-Host "    CPack NSIS failed - portable ZIP is ready"
        }
    } else {
        Write-Host "==> NSIS not in PATH - skipping installer (portable ZIP is ready)"
    }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Release artifacts in: $Dist"
Get-ChildItem $Dist | Format-Table Name, Length, LastWriteTime
$StagedExe = Get-SentinelExePath -Root $PortableDir
$StagedBin = Split-Path $StagedExe -Parent
Write-Host ""
Write-Host "Portable bin/ DLL sample:"
Get-ChildItem $StagedBin -Filter "*.dll" | Select-Object -First 8 Name | Format-Table -HideTableHeaders

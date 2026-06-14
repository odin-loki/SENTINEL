# Extract portable release to packaging/pkg (runnable install under the project).
param([string]$Zip = "")

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
if (-not $Zip) {
    $found = Get-ChildItem (Join-Path $Root "dist") -Filter "SENTINEL-*-win64-portable.zip" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $found) {
        $found = Get-ChildItem (Join-Path $Root "dist") -Filter "SENTINEL-*-portable.zip" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
    }
    if ($found) { $Zip = $found.FullName }
}
if (-not $Zip -or -not (Test-Path $Zip)) {
    throw "No portable ZIP in dist/. Run: & packaging/windows/build_mingw.ps1"
}

$Target = Join-Path $Root "packaging/pkg"
if (Test-Path $Target) { Remove-Item -Recurse -Force $Target -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $Target | Out-Null

& "C:\Windows\System32\tar.exe" -xf $Zip -C $Target
if (-not (Test-Path "$Target\bin\sentinel.exe")) {
    throw "Extract failed - expected $Target\bin\sentinel.exe"
}

Write-Host "Installed to $Target"
Write-Host "Run: $Target\bin\sentinel.exe"
Write-Host "Qt6Core.dll present: $(Test-Path (Join-Path $Target 'bin\Qt6Core.dll'))"

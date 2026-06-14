# run_real_data_tests.ps1 — one-command real-data test runner
param(
    [string]$BuildDir = "",
    [string]$QtPath = "",
    [string]$MingwPath = "C:/Qt/Tools/mingw1310_64"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build" }
$Data = Join-Path $Root "data"

if (-not $QtPath) {
    foreach ($candidate in @("C:/Qt/6.11.0/mingw_64", "C:/Qt/6.6.0/mingw_64")) {
        if (Test-Path (Join-Path $candidate "bin/qmake.exe")) {
            $QtPath = $candidate
            break
        }
    }
}
if (-not $QtPath) { throw "MinGW Qt kit not found. Pass -QtPath." }
$QtPath = $QtPath.Replace('\', '/')
$MingwPath = $MingwPath.Replace('\', '/')

Write-Host "SENTINEL real-data test runner"
Write-Host "Project: $Root"
Write-Host "Qt: $QtPath"

# 1. Verify datasets
$required = @(
    "crimes\uk_metropolitan_street_2024.csv",
    "crimes\cincinnati_pdi_crimes_sample.csv",
    "crimes\sfpd_incidents_sample.csv",
    "crimes\london_crimes_2024.csv",
    "co_offending\moreno_person_crime.csv",
    "co_offending\chicago_co_offending.csv",
    "weather\london_2024_h1.json",
    "manifest.json"
)
$missing = @()
foreach ($f in $required) {
    if (-not (Test-Path (Join-Path $Data $f))) { $missing += $f }
}
if ($missing.Count -gt 0) {
    Write-Host "Missing datasets — running fetch script first..."
    & (Join-Path $Data "scripts\fetch_datasets.ps1")
    foreach ($f in $required) {
        if (-not (Test-Path (Join-Path $Data $f))) {
            throw "Still missing: $f"
        }
    }
}

# 2. Build tests
$env:PATH = "C:/Qt/Tools/Ninja;${MingwPath}/bin;C:/Qt/Tools/CMake_64/bin;${QtPath}/bin;$env:PATH"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_PLUGIN_PATH = "$QtPath/plugins"

if (-not (Test-Path $BuildDir)) {
    cmake -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_PREFIX_PATH=$QtPath" `
        "-DCMAKE_CXX_COMPILER=$MingwPath/bin/g++.exe" `
        "-DCMAKE_C_COMPILER=$MingwPath/bin/gcc.exe" `
        "-DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe"
}
cmake --build $BuildDir --target test_local_datasets test_real_data_evaluation -j4
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# 3. Run tests
Write-Host "`n--- test_local_datasets ---"
& (Join-Path $BuildDir "tests\test_local_datasets.exe")
$localExit = $LASTEXITCODE

Write-Host "`n--- test_real_data_evaluation (metrics on stderr) ---"
& (Join-Path $BuildDir "tests\test_real_data_evaluation.exe") 2>&1 | Tee-Object -Variable evalOut
$evalExit = $LASTEXITCODE
$evalOut | Select-String "EVAL:"

if ($localExit -ne 0 -or $evalExit -ne 0) {
    Write-Host "FAILED local=$localExit eval=$evalExit"
    exit 1
}

Write-Host "`n--- EVAL threshold check ---"
$evalText = ($evalOut | Out-String)
$evalText | & (Join-Path $PSScriptRoot "check_eval_thresholds.ps1")
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "`nAll real-data tests passed."
Write-Host "See docs/REAL_DATA_EVALUATION.md for interpretation."

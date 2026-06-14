# check_eval_thresholds.ps1 — fail CI if real-data EVAL metrics regress
param(
    [string]$LogFile = "",
    [double]$MinUkPai5 = 6.0,
    [double]$MinWeatherHitRate = 0.5,
    [double]$MinUkPassRate = 0.85
)

$ErrorActionPreference = "Stop"

function Get-EvalLines {
    param([string]$Text)
    $Text -split "`n" | Where-Object { $_ -match "EVAL:" }
}

if ($LogFile -and (Test-Path $LogFile)) {
    $content = Get-Content -Raw $LogFile
} elseif ($LogFile) {
    throw "Log file not found: $LogFile"
} elseif ($input) {
    $content = ($input | Out-String)
} else {
    $content = [Console]::In.ReadToEnd()
}

$lines = Get-EvalLines $content
if (-not $lines) {
    Write-Host "No EVAL: lines found in input" -ForegroundColor Red
    exit 1
}

$failures = @()
$ukPai5 = $null
$weatherHitRate = $null
$ukPassRate = $null

foreach ($line in $lines) {
    Write-Host $line
    if ($line -match "EVAL:\s+uk_rows=\d+\s+coords=\d+\s+dates=\d+\s+types=\d+\s+passRate=([\d.]+)") {
        $ukPassRate = [double]$Matches[1]
    }
    if ($line -match "EVAL:\s+uk_pai5=([\d.]+)") {
        $ukPai5 = [double]$Matches[1]
    }
    if ($line -match "EVAL:\s+weather_cached=\d+\s+lookups=\d+\s+hits=\d+\s+hitRate=([\d.]+)") {
        $weatherHitRate = [double]$Matches[1]
    }
}

if ($null -eq $ukPai5) {
    $failures += "Missing EVAL uk_pai5 line"
} elseif ($ukPai5 -lt $MinUkPai5) {
    $failures += "uk_pai5=$ukPai5 below minimum $MinUkPai5"
}

if ($null -eq $weatherHitRate) {
    $failures += "Missing EVAL weather hitRate line"
} elseif ($weatherHitRate -lt $MinWeatherHitRate) {
    $failures += "weather hitRate=$weatherHitRate below minimum $MinWeatherHitRate"
}

if ($null -eq $ukPassRate) {
    $failures += "Missing EVAL uk passRate line"
} elseif ($ukPassRate -lt $MinUkPassRate) {
    $failures += "UK passRate=$ukPassRate below minimum $MinUkPassRate (uk pass issues)"
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "EVAL threshold check FAILED:" -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
    exit 1
}

Write-Host ""
Write-Host "EVAL threshold check passed (uk_pai5=$ukPai5, weather hitRate=$weatherHitRate, uk passRate=$ukPassRate)."
exit 0

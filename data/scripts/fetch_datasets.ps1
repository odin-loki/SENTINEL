# fetch_datasets.ps1 — download curated SENTINEL test datasets into sentinel/data/
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Crimes = Join-Path $Root "crimes"
$CoOff = Join-Path $Root "co_offending"
$Weather = Join-Path $Root "weather"
$BulkCrimes = Join-Path $Root "bulk\crimes"
$BulkCoOff = Join-Path $Root "bulk\co_offending"
@($Crimes, $CoOff, $Weather, $BulkCrimes, $BulkCoOff) | ForEach-Object { New-Item -ItemType Directory -Force -Path $_ | Out-Null }

function Invoke-RestWithRetry($Url, $Attempts = 5) {
    for ($i = 1; $i -le $Attempts; $i++) {
        try {
            return Invoke-RestMethod -Uri $Url -Headers @{ "User-Agent" = "SENTINEL-data-fetch/1.0" } -TimeoutSec 120
        } catch {
            Write-Host "  attempt $i failed: $($_.Exception.Message)"
            if ($i -eq $Attempts) { throw }
            Start-Sleep -Seconds (3 * $i)
        }
    }
}

function Download-File($Url, $Dest) {
    Write-Host "GET $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing -TimeoutSec 600
    Write-Host "  -> $Dest ($((Get-Item $Dest).Length) bytes)"
}

function Trim-Csv($Src, $Dest, $MaxRows) {
    $reader = [System.IO.StreamReader]::new($Src)
    $writer = [System.IO.StreamWriter]::new($Dest, $false, [System.Text.UTF8Encoding]::new($false))
    $header = $reader.ReadLine()
    $writer.WriteLine($header)
    $count = 0
    while ($null -ne ($line = $reader.ReadLine()) -and $count -lt $MaxRows) {
        $writer.WriteLine($line)
        $count++
    }
    $reader.Close()
    $writer.Close()
    Write-Host "  trimmed -> $Dest ($count rows)"
    return $count
}

function Ensure-SampleCsv {
    param(
        [string]$SampleName,
        [string]$Dir,
        [string]$Url,
        [int]$MaxRows,
        [string]$FullName
    )
    $out = Join-Path $Dir $SampleName
    if (Test-Path $out) {
        $lines = (Get-Content $out | Measure-Object -Line).Lines - 1
        Write-Host "Reusing $SampleName ($lines rows)"
        return $lines
    }
    $bulkDir = if ($Dir -eq $CoOff) { $BulkCoOff } else { $BulkCrimes }
    $fullPath = Join-Path $Dir $FullName
    if (-not (Test-Path $fullPath)) {
        $fullPath = Join-Path $bulkDir $FullName
    }
    if (Test-Path $fullPath) {
        return Trim-Csv $fullPath $out $MaxRows
    }
    $tmp = Join-Path $Dir "_tmp_$SampleName"
    Download-File $Url $tmp
    $rows = Trim-Csv $tmp $out $MaxRows
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    return $rows
}

# UK Police — skip if already fetched
$ukPath = Join-Path $Crimes "uk_metropolitan_street_2024.csv"
if ((Test-Path $ukPath) -and ((Get-Item $ukPath).Length -gt 100000)) {
    $ukRows = (Get-Content $ukPath | Measure-Object -Line).Lines - 1
    Write-Host "Reusing $ukPath ($ukRows rows)"
} else {
    $months = @("2024-01","2024-02","2024-03","2024-04","2024-05","2024-06")
    $grid = @(
        @(51.5074, -0.1278), @(51.5500, -0.1000), @(51.4500, -0.1500),
        @(51.5200, -0.2500), @(51.5100, 0.0500)
    )
    $seen = @{}
    $ukRows = 0
    $ukWriter = [System.IO.StreamWriter]::new($ukPath, $false, [System.Text.UTF8Encoding]::new($false))
    $ukWriter.WriteLine("Crime ID,Month,Reported by,Falls within,Longitude,Latitude,Location,LSOA code,LSOA name,Crime type,Last outcome category,Context")
    foreach ($month in $months) {
        foreach ($pt in $grid) {
            $lat = $pt[0]; $lng = $pt[1]
            $url = "https://data.police.uk/api/crimes-street/all-crime?date=$month&lat=$lat&lng=$lng"
            Write-Host "UK police $month @ $lat,$lng ..."
            $crimes = Invoke-RestWithRetry $url
            foreach ($crime in $crimes) {
                if ($seen.ContainsKey($crime.id)) { continue }
                $seen[$crime.id] = $true
                $loc = if ($crime.location -and $crime.location.street) { $crime.location.street.name } else { "" }
                $lon = if ($crime.location) { $crime.location.longitude } else { "" }
                $latv = if ($crime.location) { $crime.location.latitude } else { "" }
                $outcome = if ($crime.outcome_status) { $crime.outcome_status.category } else { "" }
                $line = @(
                    $crime.id, $crime.month, "", "",
                    $lon, $latv, $loc,
                    "", "", $crime.category, $outcome, ""
                ) -join ","
                $ukWriter.WriteLine($line)
                $ukRows++
            }
        }
    }
    $ukWriter.Close()
    Write-Host "  -> $ukPath ($ukRows rows)"
}

$datasetMeta = @(
    @{ id = "uk_police"; path = "crimes/uk_metropolitan_street_2024.csv"; rows = $ukRows; source = "https://data.police.uk/docs/method/crime-street/"; license = "OGL v3.0" }
)

$cinciRows = Ensure-SampleCsv -SampleName "cincinnati_pdi_crimes_sample.csv" -Dir $Crimes -Url "https://data.cincinnati-oh.gov/api/views/k59e-2pvf/rows.csv?`$limit=15000" -MaxRows 15000 -FullName "cincinnati_pdi_crimes.csv"
$datasetMeta += @{ id = "cincinnati_pdi_crimes_sample"; path = "crimes/cincinnati_pdi_crimes_sample.csv"; rows = $cinciRows; source = "https://data.cincinnati-oh.gov/api/views/k59e-2pvf"; license = "public open data" }

$sfpdRows = Ensure-SampleCsv -SampleName "sfpd_incidents_sample.csv" -Dir $Crimes -Url "https://data.sfgov.org/api/views/wg3w-h783/rows.csv?`$limit=15000" -MaxRows 15000 -FullName "sfpd_incidents.csv"
$datasetMeta += @{ id = "sfpd_incidents_sample"; path = "crimes/sfpd_incidents_sample.csv"; rows = $sfpdRows; source = "https://data.sfgov.org/api/views/wg3w-h783"; license = "public open data" }

$chiRows = Ensure-SampleCsv -SampleName "chicago_arrests_sample.csv" -Dir $CoOff -Url "https://data.cityofchicago.org/api/views/dpt3-jri9/rows.csv?`$limit=15000" -MaxRows 15000 -FullName "chicago_arrests.csv"
$datasetMeta += @{ id = "chicago_arrests_sample"; path = "co_offending/chicago_arrests_sample.csv"; rows = $chiRows; source = "https://data.cityofchicago.org/api/views/dpt3-jri9"; license = "public open data" }

# Chicago co-offending
$chicagoSrc = Join-Path $CoOff "chicago_arrests_sample.csv"
$chicagoCo = Join-Path $CoOff "chicago_co_offending.csv"
$byCase = @{}
Import-Csv $chicagoSrc | ForEach-Object {
    $case = $_.'CASE NUMBER'
    $id = $_.'CB_NO'
    if ($case -and $id) {
        if (-not $byCase.ContainsKey($case)) { $byCase[$case] = @() }
        $byCase[$case] += "arrest_$id"
    }
}
$coCount = 0
$coWriter = [System.IO.StreamWriter]::new($chicagoCo, $false, [System.Text.UTF8Encoding]::new($false))
$coWriter.WriteLine("person_id,incident_id,role")
foreach ($kv in $byCase.GetEnumerator()) {
    if ($kv.Value.Count -lt 2) { continue }
    $incident = "case_$($kv.Key)"
    foreach ($person in $kv.Value) {
        $coWriter.WriteLine("$person,$incident,suspect")
        $coCount++
    }
}
$coWriter.Close()
Write-Host "Chicago co-offend -> $chicagoCo ($coCount records)"
$datasetMeta += @{ id = "chicago_co_offending"; path = "co_offending/chicago_co_offending.csv"; rows = $coCount; source = "derived from Chicago arrests" }

# Moreno Crime network
$morenoOut = Join-Path $CoOff "moreno_person_crime.csv"
if (-not (Test-Path $morenoOut)) {
    $morenoTar = Join-Path $BulkCoOff "_moreno.tar.bz2"
    if (-not (Test-Path $morenoTar)) {
        Download-File "http://konect.cc/files/download.tsv.moreno_crime.tar.bz2" $morenoTar
    }
    $extractDir = Join-Path $CoOff "_moreno_extract"
    if (Test-Path $extractDir) { Remove-Item $extractDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
    & C:\Windows\System32\tar.exe -xjf $morenoTar -C $extractDir
    $edgeFile = Get-ChildItem -Path $extractDir -Recurse -Filter "out.moreno_crime_crime" | Select-Object -First 1
    $roleFile = Get-ChildItem -Path $extractDir -Recurse -Filter "rel.moreno_crime_crime.person.role" | Select-Object -First 1
    if (-not $edgeFile) { throw "Moreno edge file not found" }
    $edges = Get-Content $edgeFile.FullName | Where-Object { $_ -and -not $_.StartsWith('%') }
    $roles = if ($roleFile) { Get-Content $roleFile.FullName } else { @() }
    $morenoCount = 0
    $moWriter = [System.IO.StreamWriter]::new($morenoOut, $false, [System.Text.UTF8Encoding]::new($false))
    $moWriter.WriteLine("person_id,incident_id,role")
    for ($i = 0; $i -lt $edges.Count; $i++) {
        $parts = $edges[$i] -split '\s+'
        if ($parts.Count -lt 2) { continue }
        $role = if ($i -lt $roles.Count) { $roles[$i].ToLower() } else { "participant" }
        $moWriter.WriteLine("person_$($parts[0]),crime_$($parts[1]),$role")
        $morenoCount++
    }
    $moWriter.Close()
    Remove-Item $extractDir -Recurse -Force
    Write-Host "Moreno -> $morenoOut ($morenoCount records)"
} else {
    $morenoCount = (Get-Content $morenoOut | Measure-Object -Line).Lines - 1
    Write-Host "Reusing moreno_person_crime.csv ($morenoCount records)"
}
$datasetMeta += @{ id = "moreno_person_crime"; path = "co_offending/moreno_person_crime.csv"; rows = $morenoCount; source = "http://konect.cc/networks/moreno_crime/" }

# Weather
$weatherPath = Join-Path $Weather "london_2024_h1.json"
if (-not (Test-Path $weatherPath)) {
    $weatherUrl = "https://historical-forecast-api.open-meteo.com/v1/forecast?latitude=51.5074&longitude=-0.1278&start_date=2024-01-01&end_date=2024-06-30&hourly=temperature_2m,precipitation,windspeed_10m,visibility,is_day,weathercode&timezone=Europe%2FLondon&wind_speed_unit=kmh"
    Download-File $weatherUrl $weatherPath
}
$weatherJson = Get-Content $weatherPath -Raw | ConvertFrom-Json
$hourlyCount = $weatherJson.hourly.time.Count
$datasetMeta += @{ id = "london_weather_h1"; path = "weather/london_2024_h1.json"; hourly_records = $hourlyCount; source = "https://open-meteo.com/"; license = "CC BY 4.0" }

$manifest = @{ datasets = $datasetMeta }
$manifestPath = Join-Path $Root "manifest.json"
$manifest | ConvertTo-Json -Depth 5 | Set-Content $manifestPath -Encoding UTF8
Write-Host "Wrote $manifestPath"

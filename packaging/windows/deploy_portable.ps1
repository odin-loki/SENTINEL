# Package sentinel.exe with ALL Qt DLLs (fixes missing DLL GUI crash).
# Works with MinGW or MSVC builds - auto-detects Qt kit from build CMakeCache.
# Stages to packaging/pkg/ under the project root, then copies ZIP + NSIS to dist/.

param(
    [string]$BuildDir = "",
    [string]$QtPath = "",
    [string]$Version = "1.0.0",
    [string]$InstallRoot = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
Set-Location $Root

if (-not $InstallRoot) {
    $InstallRoot = Join-Path $Root "packaging/pkg"
} elseif (-not [System.IO.Path]::IsPathRooted($InstallRoot)) {
    $InstallRoot = Join-Path $Root $InstallRoot
}
$InstallRoot = (New-Item -ItemType Directory -Force -Path $InstallRoot).FullName
$BinDir = Join-Path $InstallRoot "bin"

if (-not $BuildDir) {
    foreach ($d in @("build", "build-msvc-release", "build-release-msvc")) {
        if (Test-Path (Join-Path $d "sentinel.exe")) { $BuildDir = $d; break }
        if (Test-Path (Join-Path $d "Release/sentinel.exe")) { $BuildDir = $d; break }
    }
}
if (-not $BuildDir) { throw "No sentinel.exe found. Build first or pass -BuildDir" }



$Exe = @(

    (Join-Path $BuildDir "sentinel.exe"),

    (Join-Path $BuildDir "Release/sentinel.exe")

) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $Exe) { throw "sentinel.exe not in $BuildDir" }



function Resolve-QtPathFromCache {

    param([string]$CacheFile, [string]$Override)



    if ($Override) {

        $normalized = $Override.Replace('\', '/')

        if (-not (Test-Path (Join-Path $normalized "bin/windeployqt.exe"))) {

            throw "QtPath not found or invalid: $Override"

        }

        return $normalized

    }



    if (Test-Path $CacheFile) {

        $prefixLine = Select-String -Path $CacheFile -Pattern '^CMAKE_PREFIX_PATH:.*=(.+)$' |

            Select-Object -First 1

        if ($prefixLine) {

            $prefix = ($prefixLine.Line -replace '^CMAKE_PREFIX_PATH:[^=]+=', '').Trim()

            if ($prefix -match ';') {

                $prefix = ($prefix -split ';')[0]

            }

            $prefix = $prefix.Replace('\', '/')

            if ($prefix -and (Test-Path (Join-Path $prefix "bin/windeployqt.exe"))) {

                return $prefix

            }

        }

    }



    foreach ($candidate in @(

        "C:/Qt/6.11.0/mingw_64",

        "C:/Qt/6.6.0/mingw_64",

        "C:/Qt/6.11.0/msvc2022_64",

        "C:/Qt/6.6.0/msvc2022_64"

    )) {

        if (Test-Path (Join-Path $candidate "bin/windeployqt.exe")) {

            return $candidate

        }

    }



    throw "Could not resolve Qt kit. Pass -QtPath or configure build with CMAKE_PREFIX_PATH."

}



$cache = Join-Path $BuildDir "CMakeCache.txt"

$QtPath = Resolve-QtPathFromCache -CacheFile $cache -Override $QtPath



$Windeploy = Join-Path $QtPath "bin/windeployqt.exe"

if (-not (Test-Path $Windeploy)) { throw "windeployqt not found at $Windeploy" }



$Dist = Join-Path $Root "dist"

if (Test-Path $InstallRoot) { Remove-Item -Recurse -Force $InstallRoot }

New-Item -ItemType Directory -Force -Path $BinDir | Out-Null

New-Item -ItemType Directory -Force -Path $Dist | Out-Null



$StagedExe = Join-Path $BinDir "sentinel.exe"

Copy-Item $Exe -Destination $StagedExe -Force

Write-Host "==> Deploying Qt from $QtPath to $InstallRoot"
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $Windeploy --release --no-translations --compiler-runtime $StagedExe 2>&1 | ForEach-Object { Write-Host $_ }
$wdqExit = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($wdqExit -ne 0) { throw "windeployqt failed with exit code $wdqExit" }



# Sample data + docs

$DataSrc = Join-Path $Root "data"

if (Test-Path $DataSrc) {

    $DataDst = Join-Path $InstallRoot "share/sentinel/data"

    New-Item -ItemType Directory -Force -Path $DataDst | Out-Null

    Get-ChildItem $DataSrc -Recurse -File | Where-Object {

        $_.FullName -notmatch '\\bulk\\' -and $_.FullName -notmatch '\\scripts\\'

    } | ForEach-Object {

        $rel = $_.FullName.Substring($DataSrc.Length + 1)

        $dest = Join-Path $DataDst $rel

        New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null

        Copy-Item $_.FullName $dest -Force

    }

}

Copy-Item (Join-Path $Root "README.md") (Join-Path $InstallRoot "README.txt") -Force -ErrorAction SilentlyContinue

$DocDir = Join-Path $InstallRoot "share/doc/Sentinel"
New-Item -ItemType Directory -Force -Path $DocDir | Out-Null
foreach ($doc in @("README.md", "docs/ANALYST_GUIDE.md", "docs/REAL_DATA_EVALUATION.md")) {
    $src = Join-Path $Root $doc
    if (Test-Path $src) {
        $name = if ($doc -eq "README.md") { "README.md" } else { Split-Path $doc -Leaf }
        Copy-Item $src (Join-Path $DocDir $name) -Force
    }
}



foreach ($path in @(

    (Join-Path $BinDir "Qt6Core.dll"),

    (Join-Path $BinDir "platforms/qwindows.dll")

)) {

    if (-not (Test-Path -LiteralPath $path)) {

        throw "Missing after windeployqt: $path"

    }

}

Write-Host "    Verified: Qt6Core.dll, platforms/qwindows.dll"

if ($env:SIGN_CERT) {
    $signtool = Get-Command signtool -ErrorAction SilentlyContinue
    if ($signtool) {
        Write-Host "==> Code signing $StagedExe"
        $signArgs = @("sign", "/fd", "SHA256", "/f", $env:SIGN_CERT, $StagedExe)
        if ($env:SIGN_PASSWORD) { $signArgs += @("/p", $env:SIGN_PASSWORD) }
        & signtool @signArgs
        if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
    } else {
        Write-Host "    SIGN_CERT set but signtool not found - skipping code signing"
    }
}



# Remove stale CPack NSIS exe (small, no Qt DLLs bundled) - keep *-setup.exe from sentinel.nsi

$staleThresholdBytes = 10MB

Get-ChildItem $Dist -Filter "SENTINEL-*-win64.exe" -ErrorAction SilentlyContinue | ForEach-Object {

    if ($_.Length -lt $staleThresholdBytes) {

        Write-Host "==> Removing stale CPack installer (no Qt DLLs): $($_.Name) ($([math]::Round($_.Length / 1MB, 2)) MB)"

        Remove-Item $_.FullName -Force

    }

}



$Zip = Join-Path $Dist "SENTINEL-$Version-win64-portable.zip"

if (Test-Path $Zip) { Remove-Item -Force $Zip }



$SetupDist = Join-Path $Dist "SENTINEL-$Version-win64-setup.exe"

$makensis = Get-Command makensis -ErrorAction SilentlyContinue

if (-not $makensis) {

    $nsis = "C:/Program Files (x86)/NSIS/makensis.exe"

    if (Test-Path $nsis) { $makensis = Get-Command $nsis }

}

if ($makensis) {

    Write-Host "==> Building NSIS setup.exe from $InstallRoot"

    $nsi = Join-Path $PSScriptRoot "sentinel.nsi"

    & $makensis "/DAPP_VERSION=$Version" "/DSTAGE_DIR=$InstallRoot" $nsi

    if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit code $LASTEXITCODE" }

    $setup = Join-Path $PSScriptRoot "SENTINEL-$Version-win64-setup.exe"

    if (-not (Test-Path $setup)) { throw "NSIS did not produce $setup" }

    Copy-Item $setup $SetupDist -Force

    Write-Host "    $SetupDist ($([math]::Round((Get-Item $SetupDist).Length / 1MB, 2)) MB)"

    if ($env:SIGN_CERT) {
        $signtool = Get-Command signtool -ErrorAction SilentlyContinue
        if ($signtool) {
            Write-Host "==> Code signing $SetupDist"
            $signArgs = @("sign", "/fd", "SHA256", "/f", $env:SIGN_CERT, $SetupDist)
            if ($env:SIGN_PASSWORD) { $signArgs += @("/p", $env:SIGN_PASSWORD) }
            & signtool @signArgs
            if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
        }
    }

} else {

    Write-Host "WARNING: makensis not found - portable ZIP only. Install NSIS: https://nsis.sourceforge.io/"

}



Write-Host "==> Creating portable ZIP"

$tar = "C:\Windows\System32\tar.exe"

if (Test-Path $tar) {

    & $tar -a -c -f $Zip -C $InstallRoot .

} else {

    $PortableDir = Join-Path $Dist "SENTINEL-$Version-win64-portable"

    if (Test-Path $PortableDir) { Remove-Item -Recurse -Force $PortableDir }

    Copy-Item $InstallRoot $PortableDir -Recurse -Force

    Compress-Archive -Path (Join-Path $PortableDir "*") -DestinationPath $Zip -Force

}

Write-Host "    $Zip ($([math]::Round((Get-Item $Zip).Length / 1MB, 2)) MB)"



Write-Host ""

Write-Host "DONE - staged to $InstallRoot"

Write-Host "  Run: $StagedExe"

Write-Host "  dist/:"

Get-ChildItem $Dist -File | Sort-Object Name | Format-Table Name, @{N='SizeMB';E={[math]::Round($_.Length/1MB,2)}}, LastWriteTime -AutoSize

Write-Host "bin/ DLLs:"

Get-ChildItem $BinDir -Filter "*.dll" | Select-Object -First 10 Name



# MinGW release: configure, build sentinel, package with deploy_portable.ps1.

#

# Usage:

#   & packaging/windows/build_mingw.ps1

#   & packaging/windows/build_mingw.ps1 -QtPath "C:/Qt/6.11.0/mingw_64" -Version 1.2.0

#

# CI (aqtinstall):

#   & packaging/windows/build_mingw.ps1 `

#     -QtPath "$env:GITHUB_WORKSPACE/6.6.0/mingw_64" `

#     -MingwPath "$env:GITHUB_WORKSPACE/Tools/mingw1310_64" `

#     -CmakePath "$env:GITHUB_WORKSPACE/Tools/CMake_64/bin" `

#     -NinjaPath "$env:GITHUB_WORKSPACE/Tools/Ninja"



param(

    [string]$QtPath = "",

    [string]$MingwPath = "C:/Qt/Tools/mingw1310_64",

    [string]$CmakePath = "C:/Qt/Tools/CMake_64/bin",

    [string]$NinjaPath = "C:/Qt/Tools/Ninja",

    [string]$BuildDir = "build",
    [string]$Version = "1.0.0",
    [string]$InstallRoot = ""
)



$ErrorActionPreference = "Stop"

$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }

$Root = (Resolve-Path (Join-Path $ScriptRoot "../..")).Path

Set-Location $Root



if (-not $QtPath) {

    foreach ($candidate in @("C:/Qt/6.11.0/mingw_64", "C:/Qt/6.6.0/mingw_64")) {

        if (Test-Path (Join-Path $candidate "bin/qmake.exe")) {

            $QtPath = $candidate

            break

        }

    }

}

if (-not $QtPath) {

    throw "MinGW Qt kit not found. Install Qt mingw_64 or pass -QtPath."

}



$QtPath = $QtPath.Replace('\', '/')

$MingwPath = $MingwPath.Replace('\', '/')

$env:PATH = "$NinjaPath;$MingwPath/bin;$CmakePath;$QtPath/bin;$env:PATH"

$env:QT_PLUGIN_PATH = "$QtPath/plugins"



Write-Host "==> Configuring MinGW Release (Qt: $QtPath)"

& cmake -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `

    "-DCMAKE_PREFIX_PATH=$QtPath" `

    "-DCMAKE_CXX_COMPILER=$MingwPath/bin/g++.exe" `

    "-DCMAKE_C_COMPILER=$MingwPath/bin/gcc.exe" `

    "-DCMAKE_MAKE_PROGRAM=$NinjaPath/ninja.exe"

if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }



Write-Host "==> Building sentinel"

& cmake --build $BuildDir --target sentinel -j

if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }



& (Join-Path $ScriptRoot "deploy_portable.ps1") `

    -BuildDir $BuildDir `

    -QtPath $QtPath `

    -Version $Version `

    -InstallRoot $InstallRoot



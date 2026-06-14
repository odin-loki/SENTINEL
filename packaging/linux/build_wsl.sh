#!/usr/bin/env bash
# Run a full Clang Release build inside WSL Ubuntu.
#
# Prerequisites (Ubuntu 22.04 / 24.04):
#   sudo apt-get update
#   sudo apt-get install -y --no-install-recommends \
#     clang lld cmake ninja-build \
#     qt6-base-dev qt6-charts-dev libqt6sql6-sqlite \
#     python3 python3-pip \
#     dpkg-dev file ca-certificates git
#
# Usage (from Windows, in PowerShell):
#   wsl bash -lc "cd '/mnt/c/Users/you/path/to/sentinel' && ./packaging/linux/build_wsl.sh"
#
# Or inside WSL:
#   cd /mnt/c/Users/you/path/to/sentinel
#   ./packaging/linux/build_wsl.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

APT_PACKAGES=(
    clang
    lld
    cmake
    ninja-build
    qt6-base-dev
    qt6-charts-dev
    libqt6sql6-sqlite
    python3
    python3-pip
    dpkg-dev
    file
    ca-certificates
    git
)

missing=()
for pkg in "${APT_PACKAGES[@]}"; do
    if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
        missing+=("$pkg")
    fi
done

if ((${#missing[@]} > 0)); then
    echo "==> Installing missing apt packages: ${missing[*]}"
    if ! command -v sudo >/dev/null 2>&1; then
        echo "ERROR: sudo required to install: ${missing[*]}" >&2
        exit 1
    fi
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends "${missing[@]}"
fi

export CC=clang
export CXX=clang++
export QT_QPA_PLATFORM=offscreen

chmod +x "$ROOT/packaging/linux/build_release.sh"
exec env VERSION="${VERSION:-1.0.0}" "$ROOT/packaging/linux/build_release.sh"

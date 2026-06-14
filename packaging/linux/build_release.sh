#!/usr/bin/env bash
# Build native Linux packages (TGZ + DEB) with Clang.
#
# Usage:
#   ./packaging/linux/build_release.sh
#   QT_PREFIX=/opt/Qt/6.6.0/gcc_64 ./packaging/linux/build_release.sh
#   VERSION=1.2.0 BUILD_DIR=build-release ./packaging/linux/build_release.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

export CC="${CC:-clang}"
export CXX="${CXX:-clang++}"

VERSION="${VERSION:-1.0.0}"
BUILD_DIR="${BUILD_DIR:-build-release}"
STAGE="${ROOT}/dist/stage"
DIST="${ROOT}/dist"
ICON="${ROOT}/packaging/icons/sentinel.svg"

# ── Detect Qt6 prefix ────────────────────────────────────────────────────────
detect_qt_prefix() {
    if [[ -n "${QT_PREFIX:-}" ]]; then
        echo "$QT_PREFIX"
        return
    fi

    if command -v qmake6 >/dev/null 2>&1; then
        local prefix=""
        if prefix="$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null)"; then
            if [[ -n "$prefix" && -d "$prefix" ]]; then
                echo "$prefix"
                return
            fi
        fi
    fi

    if pkg-config --exists Qt6Core 2>/dev/null; then
        echo "/usr"
        return
    fi

    for candidate in /usr/lib/qt6 /usr/lib/x86_64-linux-gnu/qt6; do
        if [[ -d "$candidate" ]]; then
            echo "/usr"
            return
        fi
    done

    local candidates=()
    shopt -s nullglob
    candidates=("$HOME"/Qt/6.*/gcc_64 "$HOME"/Qt/6.*/clang_64)
    shopt -u nullglob
    for qt_dir in "${candidates[@]}"; do
        if [[ -d "$qt_dir" ]]; then
            echo "$qt_dir"
            return
        fi
    done

    echo "/usr"
}

QT_PREFIX="$(detect_qt_prefix)"

if ! command -v "$CC" >/dev/null 2>&1 || ! command -v "$CXX" >/dev/null 2>&1; then
    echo "ERROR: Clang not found (CC=$CC CXX=$CXX). Install clang and retry." >&2
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$DIST"

echo "==> Configuring Release build (Qt: $QT_PREFIX, CC: $CC, CXX: $CXX)"
cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DSENTINEL_VERSION="$VERSION"

echo "==> Building sentinel and tests"
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"

echo "==> Running unit tests (excluding slow real-data suite)"
export QT_QPA_PLATFORM=offscreen
if ! ctest --test-dir "$BUILD_DIR" \
    -E "test_local_datasets|test_real_data_evaluation" \
    --output-on-failure -j"$(nproc 2>/dev/null || echo 4)"; then
    echo "ERROR: ctest failed" >&2
    exit 1
fi

echo "==> Building sentinel application target"
cmake --build "$BUILD_DIR" --target sentinel -j"$(nproc 2>/dev/null || echo 4)"

echo "==> Installing to staging (DESTDIR)"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR"

if [[ -f "$ICON" ]]; then
    echo "==> Ensuring desktop icon is staged"
    icon_dest="$STAGE/usr/share/icons/hicolor/scalable/apps"
    mkdir -p "$icon_dest"
    cp -f "$ICON" "$icon_dest/sentinel.svg"
fi

echo "==> Creating TGZ archive"
TARBALL="$DIST/SENTINEL-${VERSION}-linux-x86_64.tar.gz"
tar -C "$STAGE" -czf "$TARBALL" .
echo "    $TARBALL"

pushd "$BUILD_DIR" >/dev/null

echo "==> Running CPack (DEB + TGZ)"
if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "ERROR: dpkg-deb not found — install dpkg-dev for .deb packaging." >&2
    exit 1
fi

cpack -G DEB -C Release
cpack -G TGZ -C Release

find . -maxdepth 1 -name '*.deb' -exec cp -v {} "$DIST/" \;
find . -maxdepth 1 -name 'SENTINEL*.tar.gz' -exec cp -v {} "$DIST/" \;

popd >/dev/null

echo ""
echo "Release artifacts in: $DIST"
ls -lh "$DIST"

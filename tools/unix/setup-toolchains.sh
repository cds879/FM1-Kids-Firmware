#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Install the Linux x86-64 build tools into DEST (default: ~/.fm1-toolchains):
#   DEST/jieli   JieLi Linux toolchain (pi32v2 clang 4.0.1) for firmware builds
#   DEST/wasi-sdk-27.0-x86_64-linux   WASI SDK 27 for the browser preview
# Used by the Docker image and by GitHub Actions. Re-running skips what exists.
#
# JieLi publishes the toolchain at a moving "latest" URL. Set
# JIELI_TOOLCHAIN_SHA256 to pin the archive; the hash is printed on download.
# Set SKIP_JIELI=1 to install only the preview tools.
set -eu
DEST="${1:-$HOME/.fm1-toolchains}"
JIELI_URL="${JIELI_TOOLCHAIN_URL:-https://pkgman.jieliapp.com/s/linux-toolchain}"
WASI_URL="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/wasi-sdk-27.0-x86_64-linux.tar.gz"
WASI_SHA256="b7d4d944c88503e4f21d84af07ac293e3440b1b6210bfd7fe78e0afd92c23bc2"
mkdir -p "$DEST"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$DEST/wasi-sdk-27.0-x86_64-linux/bin/clang" ]; then
    echo "setup: downloading WASI SDK 27"
    curl -fsSL --retry 3 -o "$TMP/wasi.tgz" "$WASI_URL"
    echo "$WASI_SHA256  $TMP/wasi.tgz" | sha256sum -c - >/dev/null
    tar -xzf "$TMP/wasi.tgz" -C "$DEST"
fi

if [ "${SKIP_JIELI:-0}" != 1 ] && [ ! -e "$DEST/jieli/pi32v2/bin/clang" ]; then
    echo "setup: downloading JieLi Linux toolchain from $JIELI_URL"
    curl -fsSL --retry 3 -o "$TMP/jieli.tar.xz" "$JIELI_URL"
    SUM="$(sha256sum "$TMP/jieli.tar.xz" | cut -d' ' -f1)"
    echo "setup: JieLi toolchain archive sha256 $SUM"
    if [ -n "${JIELI_TOOLCHAIN_SHA256:-}" ] && [ "$SUM" != "$JIELI_TOOLCHAIN_SHA256" ]; then
        echo "setup: expected $JIELI_TOOLCHAIN_SHA256 - JieLi changed the toolchain." >&2; exit 1
    fi
    mkdir -p "$TMP/jieli" && tar -xJf "$TMP/jieli.tar.xz" -C "$TMP/jieli"
    TC="$(find "$TMP/jieli" -maxdepth 3 -type f -path '*/pi32v2/bin/clang' | head -1)"
    [ -n "$TC" ] || { echo "setup: no pi32v2/bin/clang in the JieLi archive:" >&2; find "$TMP/jieli" -maxdepth 4 | head -40 >&2; exit 1; }
    rm -rf "$DEST/jieli"; mv "$(dirname "$(dirname "$(dirname "$TC")")")" "$DEST/jieli"
    echo "$SUM" > "$DEST/jieli/ARCHIVE_SHA256"
fi
[ -e "$DEST/jieli/pi32v2/bin/clang" ] && "$DEST/jieli/pi32v2/bin/clang" --version | head -1
echo "setup: tools ready in $DEST"

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Build FM1 on macOS or Linux.
#   ./build.sh preview            browser preview  -> build/preview/index.html
#   ./build.sh firmware           flashable image  -> build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc
#   ./build.sh package            firmware + workbench (set FM1_SOURCE_URL)
#   ./build.sh lock               pin the toolchain after a good firmware build
#   ./build.sh preview --native   preview without Docker (needs pwsh: brew install powershell)
# Everything else runs in a linux/amd64 Docker container (Docker Desktop on a Mac).
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
MODE="${1:-firmware}"
NATIVE="${2:-}"

if [ "$NATIVE" = "--native" ]; then
    [ "$MODE" = "preview" ] || { echo "build.sh: only 'preview' builds natively; JieLi's firmware compiler is Linux-only." >&2; exit 2; }
    command -v pwsh >/dev/null || { echo "build.sh: install PowerShell first: brew install powershell" >&2; exit 1; }
    python3 "$ROOT/tools/unix/patch_scripts.py" "$ROOT"
    # Downloads and verifies WASI SDK 27 for this Mac into build/tools on first use.
    FM1_ROOT="$ROOT" exec pwsh -NoProfile -File "$ROOT/build/unix/Build-Web.ps1" -LocalPreview
fi

command -v docker >/dev/null || { echo "build.sh: install Docker Desktop, or use: ./build.sh preview --native" >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo "build.sh: Docker is not running" >&2; exit 1; }
STAMP="$(cat "$ROOT/tools/unix/Dockerfile" "$ROOT/tools/unix/setup-toolchains.sh" | shasum -a 256 2>/dev/null || cat "$ROOT/tools/unix/Dockerfile" "$ROOT/tools/unix/setup-toolchains.sh" | sha256sum)"
IMAGE="fm1-build:$(echo "$STAMP" | cut -c1-12)"
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "build.sh: building $IMAGE (first run downloads ~700 MB of tools)"
    docker build --platform linux/amd64 -f "$ROOT/tools/unix/Dockerfile" \
        --build-arg JIELI_TOOLCHAIN_SHA256="${JIELI_TOOLCHAIN_SHA256:-a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd}" -t "$IMAGE" "$ROOT"
fi
exec docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -e FM1_SOURCE_URL="${FM1_SOURCE_URL:-}" -e FM1_NO_LTO="${FM1_NO_LTO:-}" \
    -v "$ROOT:/work" -w /work "$IMAGE" sh tools/unix/run.sh "$MODE"

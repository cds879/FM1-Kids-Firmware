#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Build on Linux x86-64 (inside the Docker image or on GitHub Actions).
#   tools/unix/run.sh preview    browser preview  -> build/preview/index.html
#   tools/unix/run.sh firmware   flashable image  -> build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc
#   tools/unix/run.sh package    firmware + workbench for distribution (needs FM1_SOURCE_URL)
#   tools/unix/run.sh lock       pin the current toolchain in tools/unix/toolchain.lock.json
# Tools come from FM1_TOOLS (default ~/.fm1-toolchains, see setup-toolchains.sh)
# unless FM1_PI32_TOOLCHAIN / FM1_WASI_SDK are already set.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOLS="${FM1_TOOLS:-$HOME/.fm1-toolchains}"
export FM1_ROOT="$ROOT"
export FM1_PI32_TOOLCHAIN="${FM1_PI32_TOOLCHAIN:-$TOOLS/jieli}"
if [ -z "${FM1_WASI_SDK:-}" ] && [ -d "$TOOLS/wasi-sdk-27.0-x86_64-linux" ]; then
    export FM1_WASI_SDK="$TOOLS/wasi-sdk-27.0-x86_64-linux"
fi
export DOTNET_CLI_TELEMETRY_OPTOUT=1 POWERSHELL_TELEMETRY_OPTOUT=1
command -v pwsh >/dev/null || { echo "run.sh: PowerShell 7 (pwsh) is required" >&2; exit 1; }
python3 "$ROOT/tools/unix/patch_scripts.py" "$ROOT"
case "${1:-firmware}" in
    preview)  pwsh -NoProfile -File "$ROOT/build/unix/Build-Web.ps1" -LocalPreview ;;
    firmware) pwsh -NoProfile -File "$ROOT/build/unix/Build.ps1" -FirmwareOnly ;;
    package)  pwsh -NoProfile -File "$ROOT/build/unix/Build.ps1" -Package ;;
    lock)
        [ -f "$ROOT/build/dx7/toolchain.json" ] || { echo "run.sh: build firmware first" >&2; exit 1; }
        cp "$ROOT/build/dx7/toolchain.json" "$ROOT/tools/unix/toolchain.lock.json"
        echo "run.sh: pinned toolchain in tools/unix/toolchain.lock.json" ;;
    *) echo "usage: run.sh preview|firmware|package|lock" >&2; exit 2 ;;
esac

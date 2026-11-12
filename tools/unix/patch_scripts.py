#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Make Linux/macOS-ready copies of M-VAVE's Windows build scripts.

sdk/Build.ps1 is on the SDK's own hash-checked file list, so it is never
edited. This script writes patched copies to build/unix/. Every replacement
must match exactly the expected number of times; if M-VAVE changes the
scripts, this fails loudly instead of producing a half-patched build.

    python3 tools/unix/patch_scripts.py [ROOT]
"""
import pathlib
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2])
OUT = ROOT / "build" / "unix"

SEP = "[IO.Path]::DirectorySeparatorChar"

# Toolchain lookup for JieLi's Linux toolchain (pi32v2 clang 4.0.1).
# Replaces the Windows-only block that requires bin/clang.exe and checks the
# 22 pinned Windows toolchain hashes. The SDK payload checks are kept.
UNIX_TOOLCHAIN = r'''if (!$Toolchain) { throw 'Set FM1_PI32_TOOLCHAIN to the JieLi Linux toolchain directory (see tools/unix/README.md).' }
$Toolchain = [IO.Path]::GetFullPath($Toolchain)
function Find-Tool([string]$label, [string[]]$candidates, [switch]$Optional) {
    foreach ($c in $candidates) {
        $p = Join-Path $Toolchain $c
        if (Test-Path -LiteralPath $p -PathType Leaf) { return $p }
    }
    if ($Optional) { return $null }
    $seen = (Get-ChildItem -LiteralPath $Toolchain -Recurse -File -Depth 3 | Where-Object { $_.FullName -match '/bin/' } |
        ForEach-Object { $_.FullName.Substring($Toolchain.Length+1) } | Select-Object -First 60) -join ', '
    throw "JieLi Linux toolchain: cannot find $label (tried $($candidates -join ', ')). Files under bin/: $seen"
}
$tools = [ordered]@{
    clang   = Find-Tool 'clang'   @('pi32v2/bin/clang','common/bin/clang','pi32/bin/clang','bin/clang')
    ld      = Find-Tool 'linker'  @('pi32v2/bin/pi32v2-ld','pi32v2/bin/ld','common/bin/pi32v2-ld','common/bin/ld','pi32/bin/pi32v2-ld','bin/pi32v2-ld','bin/ld')
    nm      = Find-Tool 'llvm-nm' @('pi32v2/bin/llvm-nm','common/bin/llvm-nm','common/bin/nm','pi32/bin/llvm-nm','bin/llvm-nm')
    objcopy = Find-Tool 'objcopy' @('pi32v2/bin/llvm-objcopy','common/bin/llvm-objcopy','common/bin/objcopy','pi32/bin/llvm-objcopy','bin/llvm-objcopy')
    gold    = Find-Tool 'LLVMgold' @('pi32v2/bin/LLVMgold.so','pi32v2/lib/LLVMgold.so','common/bin/LLVMgold.so','common/lib/LLVMgold.so','lib/LLVMgold.so','bin/LLVMgold.so') -Optional
}
if ($env:FM1_NO_LTO -eq '1') { $tools.gold = $null }
$clangVersion = (& $tools.clang --version 2>&1 | Out-String)
if ($clangVersion -notmatch 'clang version 4\.0\.1') { throw "Expected JieLi pi32v2 clang 4.0.1, got: $clangVersion" }
if (!$tools.gold) { Write-Warning 'No LLVMgold plugin in this toolchain: building WITHOUT link-time optimisation. The image will be larger than an official build.' }
$toolHashes = [ordered]@{}
foreach ($k in $tools.Keys) { if ($tools[$k]) { $toolHashes[$k] = Hash $tools[$k] } }
# Optional pin: tools/unix/toolchain.lock.json, written by `build.sh lock`.
$lockFile = Join-Path $root 'tools/unix/toolchain.lock.json'
if (Test-Path -LiteralPath $lockFile) {
    $lock = Get-Content -LiteralPath $lockFile -Raw | ConvertFrom-Json
    foreach ($k in $toolHashes.Keys) {
        if ($lock.PSObject.Properties[$k] -and $lock.$k -cne $toolHashes[$k]) { throw "Toolchain $k differs from tools/unix/toolchain.lock.json. Rebuild the toolchain or re-run 'build.sh lock'." }
    }
}
[IO.File]::WriteAllText((Join-Path $build 'toolchain.json'), ($toolHashes | ConvertTo-Json), $utf8)
$manifest = Get-Content -LiteralPath (Join-Path $sdk 'manifest.json') -Encoding UTF8 -Raw | ConvertFrom-Json
if ($manifest.abi -ne 131072) { throw 'This application requires the matching 128-frame SDK.' }
'''

BUILD = [
    # Locate the tree from the environment, not from the copied script's own folder.
    ("$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))",
     "$root = [IO.Path]::GetFullPath($env:FM1_ROOT)", 1),
    ("$sdk = $PSScriptRoot", "$sdk = Join-Path $root 'sdk'", 1),
    # Windows path separators in containment checks.
    ("$full.StartsWith($sdk + '\\', ", f"$full.StartsWith($sdk + {SEP}, ", 1),
    ("(Join-Path $root 'app/synth'))+'\\')", f"(Join-Path $root 'app/synth'))+{SEP})", 1),
    ("$objectDirectory.StartsWith($root + '\\build\\', ",
     f"$objectDirectory.StartsWith((Join-Path $root 'build') + {SEP}, ", 1),
    # Toolchain binaries.
    ("(Join-Path $bin 'llvm-nm.exe')", "$tools.nm", 1),
    ("(Join-Path $bin 'llvm-objcopy.exe')", "$tools.objcopy", 1),
    ("(Join-Path $bin 'clang.exe')", "$tools.clang", 4),
    ("(Join-Path $bin 'pi32v2-ld.exe')", "$tools.ld", 1),
    ("$link = @('--plugin',(Join-Path $bin 'LLVMgold.dll'),'-o','sdk.elf') + $objects + @(",
     "$link = $(if ($tools.gold) { @('--plugin',$tools.gold) } else { @() }) + @('-o','sdk.elf') + $objects + @(", 1),
    # Without the gold plugin there is no LTO: drop -flto and plugin options.
    ("    Write-Text (Join-Path $build 'compile_commands.json')",
     "    if (!$tools.gold) { $link = @($link | Where-Object { $_ -cne '-flto' -and $_ -notlike '--plugin-opt=*' }) }\n"
     "    Write-Text (Join-Path $build 'compile_commands.json')", 1),
    ("foreach ($dir in $include) { $flags += '-I' + (Join-Path $root $dir) }",
     "if (!$tools.gold) { $flags = @($flags | Where-Object { $_ -cne '-flto' }) }\n"
     "foreach ($dir in $include) { $flags += '-I' + (Join-Path $root $dir) }", 1),
    # Use the patched web script.
    ("(Join-Path $root 'tools/Build-Web.ps1')", "(Join-Path $root 'build/unix/Build-Web.ps1')", 1),
    ('Write-Host "Firmware: $build\\sdk.elf"', 'Write-Host "Firmware: $build/sdk.elf"', 1),
]

WEB = [
    ("$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))",
     "$root = [IO.Path]::GetFullPath($env:FM1_ROOT)\n"
     "$exe = if ($IsWindows) { '.exe' } else { '' }\n"
     "$wasiHost = if ($IsMacOS) { if ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64-macos' } else { 'x86_64-macos' } } elseif ($IsLinux) { 'x86_64-linux' } else { 'x86_64-windows' }\n"
     "$wasiHash = @{'x86_64-windows'='4a576c13125c91996d8cc3b70b7ea0612c2044598d2795c9be100d15f874adf6'; 'x86_64-linux'='b7d4d944c88503e4f21d84af07ac293e3440b1b6210bfd7fe78e0afd92c23bc2'; @MAC_HASHES@}[$wasiHost]", 1),
    ("'wasi-sdk-27.0-x86_64-windows'", "\"wasi-sdk-27.0-$wasiHost\"", 1),
    ("'wasi-sdk-27.0-x86_64-windows.tar.gz'", "\"wasi-sdk-27.0-$wasiHost.tar.gz\"", 1),
    ("$expected = '4a576c13125c91996d8cc3b70b7ea0612c2044598d2795c9be100d15f874adf6'", "$expected = $wasiHash", 1),
    ("'https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/wasi-sdk-27.0-x86_64-windows.tar.gz'",
     "\"https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/wasi-sdk-27.0-$wasiHost.tar.gz\"", 1),
    ("Run 'curl.exe' @(", "Run \"curl$exe\" @(", 1),
    ("Run 'tar.exe' @(", "Run \"tar$exe\" @(", 1),
    ("'bin/clang.exe'", "\"bin/clang$exe\"", 2),
    ("$scanPath = if ($WasiSdk.StartsWith('\\\\?\\'))", "$scanPath = if (!$IsWindows) { $WasiSdk } elseif ($WasiSdk.StartsWith('\\\\?\\'))", 1),
]

MAC_HASHES = "'arm64-macos'='@ARM64@'; 'x86_64-macos'='@X64@'"


def patch(src: pathlib.Path, rules, extra=None) -> str:
    text = src.read_text(encoding="utf-8")
    for old, new, count in rules:
        found = text.count(old)
        if found != count:
            sys.exit(f"patch_scripts: {src.name}: expected {count} x {old!r}, found {found}. "
                     "M-VAVE changed the script; update tools/unix/patch_scripts.py.")
        text = text.replace(old, new)
    if extra:
        text = extra(text)
    return text


def toolchain_block(text: str) -> str:
    start = text.index("if (!$Toolchain) { $Toolchain = Join-Path $sdk 'tools' }")
    end = text.index("$synth = Get-Content -LiteralPath (Join-Path $sdk 'synth.json')")
    return text[:start] + UNIX_TOOLCHAIN + text[end:]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    build = patch(ROOT / "sdk" / "Build.ps1", BUILD, toolchain_block)
    if "$bin" in build.replace("$binary", ""):
        sys.exit("patch_scripts: Build.ps1 still references the Windows $bin folder.")
    mac = MAC_HASHES.replace("@ARM64@", MAC_ARM64).replace("@X64@", MAC_X64)
    web = patch(ROOT / "tools" / "Build-Web.ps1", WEB).replace("@MAC_HASHES@", mac)
    for name, text in (("Build.ps1", build), ("Build-Web.ps1", web)):
        (OUT / name).write_text(text, encoding="utf-8")
    print(f"patch_scripts: wrote {OUT / 'Build.ps1'} and {OUT / 'Build-Web.ps1'}")


# SHA-256 of the official wasi-sdk-27 release archives (checked 2026-11-12).
MAC_ARM64 = "055c3dc2766772c38e71a05d353e35c322c7b2c6458a36a26a836f9808a550f8"
MAC_X64 = "163dfd47f989b1a682744c1ae1f0e09a83ff5c4bbac9dcd8546909ab54cda5a1"

if __name__ == "__main__":
    main()

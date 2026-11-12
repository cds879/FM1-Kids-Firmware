# Building on macOS, Linux and GitHub Actions

M-VAVE's FM1 Open builds only on Windows. These files build the same sources
on a Mac (through Docker), on Linux, and in the cloud, without editing
M-VAVE's files.

## On a Mac

Install [Docker Desktop](https://www.docker.com/products/docker-desktop/).
On Apple silicon, keep "Use Rosetta for x86_64/amd64 emulation" turned on.

```sh
./build.sh preview      # browser preview: open build/preview/index.html
./build.sh firmware     # flashable image: build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc
```

The first run builds a `fm1-build` Docker image (about 700 MB of tools). Later
runs reuse it.

For quick UI work without Docker: install PowerShell (`brew install powershell`),
then run `./build.sh preview --native`. Firmware still needs Docker, because
JieLi's compiler only runs on Linux x86-64.

## In the cloud

Every push to `main` and every pull request runs `.github/workflows/build.yml`.
The preview page and the `.fwsc` firmware appear as a downloadable artifact on
the run's summary page. You can also start a run by hand from the Actions tab
("Run workflow").

## How it works

- `sdk/Build.ps1` is on the SDK's own hash-checked file list, so it is never
  edited. `patch_scripts.py` writes Linux/macOS-ready copies of it and of
  `tools/Build-Web.ps1` to `build/unix/`. Each change must match the original
  text exactly. If M-VAVE changes their scripts, the patch stops with an error
  instead of producing a half-patched build.
- What the patch changes: Windows path separators, `.exe` tool names, the
  location of JieLi's Linux toolchain, and the WASI SDK download for
  Linux/macOS. All of M-VAVE's checks on the SDK and the finished image still
  run.
- The Windows build checks 22 pinned toolchain hashes. The Linux toolchain has
  different files, so after the first good firmware build, run
  `./build.sh lock` and commit `tools/unix/toolchain.lock.json`. Later builds
  then refuse a changed toolchain.
- If the Linux toolchain has no `LLVMgold` plugin, the build continues without
  link-time optimisation and prints a warning. `FM1_NO_LTO=1` forces this.

## Flashing

The `.fwsc` installs with M-VAVE's workbench (Device update → choose file) or
with the M-UPGRADE tool. Back up first, and keep a recovery path ready (see
the FM-1 Transporter project) before installing a build that hasn't been
tested on hardware.

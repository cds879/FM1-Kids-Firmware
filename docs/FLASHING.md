# Installing FM1 Kids on an FM-1

The normal install runs in Chrome or Edge from the hosted installer,
**https://cds879.github.io/FM1-Kids-Firmware/**, over a USB-C cable. A recovery dongle is your safety net if an install is
interrupted. Build and test the dongle **before** the first install, and keep
a full backup of the FM-1 made with it.

Order of work:

1. Build the recovery dongle (once)
2. Back up the FM-1 (once, before anything else)
3. Install the original-UI build: proves this repository's build works on hardware
4. Install FM1 Kids
5. Know the ways back

## 1. Build the recovery dongle

Uses [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter) (MIT).

**Parts:** a Seeed XIAO RP2040, a USB-A female socket breakout, three short
wires, and the FM-1's usual USB cable. Three solder joints.

| XIAO RP2040 | USB-A socket pin |
|---|---|
| D6 | 3 (D+) |
| D7 | 2 (D−) |
| GND | 4 (GND) |
| — | 1 (VBUS): **leave unconnected** |

The FM-1 runs on its own battery while connected this way, so charge it first.

**Firmware for the dongle:** download the XIAO RP2040 UF2 from the
[Transporter releases](https://github.com/kurogedelic/FM-1-transporter/releases).
Hold the XIAO's **B** (BOOT) button, plug it into the computer, and copy the
UF2 onto the `RPI-RP2` drive that appears.

**Loader file:** download `wl82loader.bin` from
[jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool). The Transporter
sends it to the FM-1's RAM. Keep it on your computer; don't add it to this
repository.

## 2. Back up the FM-1

1. Charge the FM-1 and switch it off.
2. Plug the dongle into the computer, and the FM-1's USB cable into the
   dongle's socket.
3. Open the [Transporter web app](https://kurogedelic.github.io/FM-1-transporter/web/)
   in Chrome or Edge and follow its steps: **Connect** (pick the dongle's
   *second* serial port), **Loader** (choose `wl82loader.bin`), then switch
   the FM-1 on and press **Check**. Stock V15 enters boot mode by itself.
   Check must identify an FM-1 (key `980F`, id `856014`). If it doesn't, stop.
4. Press **Back up**. It reads the whole 1 MiB flash twice and saves it. Keep
   this file in two places: it is your FM-1 exactly as it came.
5. Switch the FM-1 off and on again before using it over USB normally.

Projects, sounds and samples you made on the FM-1 are worth exporting too:
M-VAVE's [workbench](https://open.m-vave.com/fm1-workbench), **Projects &
sounds**.

## 3. First install: the original-UI build

This is M-VAVE's FM1 Open interface, built by this repository's Linux
toolchain. If it works, the build path is proven on real hardware before the
kid interface goes on.

1. Open the installer, **https://cds879.github.io/FM1-Kids-Firmware/**, in
   Chrome or Edge on a computer. It's rebuilt from every change to `main` that
   passes the emulator tests.
2. Connect the FM-1 with a USB-C data cable and switch it on.
3. Press **Original FM1 Open interface (hardware test)**. On the page that
   opens, choose **Connect device**, then **Device update**, and install. Allow
   MIDI access when Chrome asks. Coming from stock V15, the page asks you to
   confirm.
4. Keep the FM-1 powered and connected until the page says it is done.

**Check:** the FM1 Open screen appears (eight tracks), keys play, SEQ shows
steps, PLAY plays them, and SAVE then a power cycle keeps the project.

## 4. Install FM1 Kids

The same steps with **Install FM1 Kids**.

**Check:** FM1 KIDS 0.1 at startup, then picture tiles; keys play; SEQ makes a
beat that PLAY plays; the battery icon is green (with **+** on USB); a pattern
left alone for ten seconds is still there after a power cycle; holding GLO
shows the Bluetooth panel.

**Testing a build that isn't on `main` yet** (a pull request): download the
`fm1-build-…` artifact from its run under **Actions**, unzip it, run
`python3 -m http.server 8000` in its `out` folder, and open
`http://localhost:8000/FM1-Kids-Workbench.html`. Web MIDI needs a secure
page, and `localhost` counts as one.

## 5. Ways back

- **Stock V15:** every workbench page this repository builds also carries
  M-VAVE's V15 recovery image. Use its stock restore under **Device update**.
  M-VAVE's own [workbench](https://open.m-vave.com/fm1-workbench) and M-UPGRADE
  updater work too.
- **Other firmware:** install it with its own installer as usual. FM1 Kids
  identifies itself as `FM-1_8100001`, in M-VAVE's open-firmware range, so
  updaters recognise it. The SDK handles update messages before the kid
  interface runs, so the interface can't block them.
- **If an install is interrupted or the screen stays black:** use the
  dongle. In the Transporter web app, with the FM-1 switched off, press
  **Reconnect**, then switch it on. **Write** your step-2 backup to return to
  exactly where you started. Its command-line tool can also write a firmware
  package directly:
  ```sh
  python3 tools/fm1t.py --loader wl82loader.bin write --package FM-1_015.fwsc --ref backup.bin          # check
  python3 tools/fm1t.py --loader wl82loader.bin write --package FM-1_015.fwsc --ref backup.bin --write  # write
  ```
  (`FM-1_015.fwsc`, stock V15, is in `sdk/firmware/`.) If a write or verify
  fails, don't switch the FM-1 off; write again.

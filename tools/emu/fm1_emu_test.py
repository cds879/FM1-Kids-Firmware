#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Smoke-test an FM-1 firmware package in the fm1-emu emulator.

    python3 tools/emu/fm1_emu_test.py --emu PATH/fm1-live --fwsc FIRMWARE.fwsc --out DIR
        [--reference OTHER.fwsc]   also run OTHER and compare the two (informational)

Each scenario boots the firmware headless, plays a scripted set of key presses
and checks the screen and the audio. Results go to DIR/summary.md and
DIR/results.json, with a screenshot (PNG) and the audio (WAV) of each scenario.
Exits 1 if any check fails on --fwsc. Standard library only.

The emulator is github.com/joakimeriksson/fm1-emu (MIT). Passing here is strong
evidence, not proof: the FM-1's CPU was reverse-engineered for the emulator.
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import struct
import subprocess
import sys
import zlib

LCD = (370, 42, 480, 480)          # the 240x240 LCD, drawn 2x, inside fm1-live's panel picture
RATE = 44118                       # fm1-live's audio rate (stereo s16le, no header)
SILENT_PEAK = 64                   # below this the output is silence
LOUD_PEAK = 500                    # a played note goes well above this
# Power cuts: during the first flash write, one halfway, and the last one the test
# run makes (counted from a run without a cut, so every cut lands on a real write).

# name, emulated ms, key script (ms:KEY, ^ = release), what to check
SCENARIOS = [
    ("boot",  4000, "",                         "screen drawn, no stray sound"),
    ("note",  4000, "2000:C4@100,3500:C4^",     "holding C4 makes sound"),
    ("seq",   3000, "2000:SEQ,2100:SEQ^",       "the screen changes when SEQ is pressed"),
]


# ---------- small file helpers (PPM in, PNG / WAV out) ----------
def read_ppm(path):
    data = pathlib.Path(path).read_bytes()
    parts, pos = [], 0
    while len(parts) < 4:                       # P6 width height maxval, then one whitespace byte
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos)
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end])
        pos = end
    w, h = int(parts[1]), int(parts[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


def crop(img, box):
    w, _, px = img
    x, y, cw, ch = box
    return cw, ch, b"".join(px[(row * w + x) * 3:(row * w + x + cw) * 3] for row in range(y, y + ch))


def write_png(path, img):
    w, h, px = img
    raw = b"".join(b"\0" + px[r * w * 3:(r + 1) * w * 3] for r in range(h))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    pathlib.Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                                   + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_wav(path, raw):
    hdr = b"RIFF" + struct.pack("<I", 36 + len(raw)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, RATE, RATE * 4, 4, 16)
    pathlib.Path(path).write_bytes(hdr + b"data" + struct.pack("<I", len(raw)) + raw)


def colours(img):
    _, _, px = img
    return len({px[i:i + 3] for i in range(0, len(px), 3 * 7)})   # every 7th pixel is plenty


def peak(raw):
    n = len(raw) // 2
    return max((abs(v) for v in struct.unpack(f"<{n}h", raw[:n * 2])), default=0)


# ---------- running the emulator ----------
def run(emu, fwsc, ms, keys, work, flash=None, cut=None, frames=False):
    work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, FM1_BENCH_KEYS=keys, FM1_BENCH_WAV=str(work / "audio.raw"),
               FM1_BENCH_PANEL=str(work / "panel.ppm"))
    if frames:
        env["FM1_BENCH_FRAMES"] = f"{work / 'frames'},100"
    if cut:
        env["FM1_FLASH_CUT"] = str(cut)
    cmd = [str(emu), "--fwsc", str(fwsc), "--midi", "none", "--bench", str(ms)]
    if flash:
        cmd += ["--flash", str(flash)]
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=600)
    log = p.stdout + p.stderr
    (work / "emulator.log").write_text(log)
    problems = []
    if p.returncode != 0:
        problems.append(f"emulator exited {p.returncode}")
    if "halt:" in log:
        problems.append("the CPU halted: " + next(l for l in log.splitlines() if "halt:" in l))
    raw = (work / "audio.raw").read_bytes() if (work / "audio.raw").exists() else b""
    lcd = crop(read_ppm(work / "panel.ppm"), LCD) if (work / "panel.ppm").exists() else None
    if lcd is None:
        problems.append("no screen picture")
    return {"raw": raw, "lcd": lcd, "problems": problems, "log": log}


def frame_lcd(work, ms):
    return crop(read_ppm(work / "frames" / f"{ms // 100:06d}.ppm"), LCD)


def check(name, emu, fwsc, out):
    """Run one scenario; returns (checks, artefacts)."""
    _, ms, keys, _ = next(s for s in SCENARIOS if s[0] == name)
    work = out / name
    r = run(emu, fwsc, ms, keys, work, frames=(name == "seq"))
    checks = [("emulator ran cleanly", not r["problems"], "; ".join(r["problems"]) or "ok")]
    if r["lcd"]:
        n = colours(r["lcd"])
        checks.append(("screen drawn", n >= 4, f"{n} colours on the LCD"))
    pk = peak(r["raw"])
    if name == "boot":
        checks.append(("no stray sound", pk < SILENT_PEAK, f"peak {pk}"))
    if name == "note":
        checks.append(("note makes sound", pk >= LOUD_PEAK, f"peak {pk}"))
    if name == "seq":
        try:
            changed = frame_lcd(work, 1900)[2] != frame_lcd(work, 2800)[2]
            checks.append(("screen responds to SEQ", changed, "changed" if changed else "unchanged"))
        except (FileNotFoundError, IndexError) as e:
            checks.append(("screen responds to SEQ", False, f"no frames: {e}"))
    if r["lcd"]:
        write_png(out / f"{name}.png", r["lcd"])
    write_wav(out / f"{name}.wav", r["raw"])
    digest = {"lcd": hashlib.sha256(r["lcd"][2]).hexdigest()[:16] if r["lcd"] else None,
              "audio": hashlib.sha256(r["raw"]).hexdigest()[:16]}
    return checks, digest


def power_cuts(emu, fwsc, out):
    """Cut the power during a flash write, then boot from what is left: it must still start and play."""
    checks = []
    probe = run(emu, fwsc, 4000, "2000:C4,2500:C4^", out / "powercut-probe", flash=out / "powercut-probe" / "flash.bin")
    m = re.search(r"flash: (\d+) erases / programs", probe["log"])
    ops = int(m.group(1)) if m else 0
    if ops == 0:
        return [("power-cut test possible", False, "the firmware made no flash writes in 4 s")]
    for n in sorted({1, max(1, ops // 2), ops}):
        work = out / f"powercut-{n}"
        flash = work / "flash.bin"
        work.mkdir(parents=True, exist_ok=True)
        flash.unlink(missing_ok=True)
        first = run(emu, fwsc, 4000, "2000:C4,2500:C4^", work / "cut", flash=flash, cut=n)
        cut_hit = f"power cut at op {n}" in first["log"]
        again = run(emu, fwsc, 4000, "2000:C4@100,3500:C4^", work / "reboot", flash=flash)
        ok = not again["problems"] and again["lcd"] and colours(again["lcd"]) >= 4 and peak(again["raw"]) >= LOUD_PEAK
        detail = (f"cut during write {n} of {ops}" if cut_hit else f"no cut at write {n} of {ops}") + \
                 (f"; reboot: peak {peak(again['raw'])}" + ("; " + "; ".join(again["problems"]) if again["problems"] else ""))
        checks.append((f"boots after power cut at flash write {n}", bool(ok and cut_hit), detail))
    return checks


def test(emu, fwsc, out):
    out.mkdir(parents=True, exist_ok=True)
    results, digests = {}, {}
    for name, *_ in SCENARIOS:
        results[name], digests[name] = check(name, emu, fwsc, out)
    results["powercut"] = power_cuts(emu, fwsc, out)
    return results, digests


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emu", required=True)
    ap.add_argument("--fwsc", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--reference")
    ap.add_argument("--reference-label", help="how to name the reference in the summary")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    results, digests = test(a.emu, a.fwsc, out / "build")
    ref = test(a.emu, a.reference, out / "reference") if a.reference else None

    failed = [(s, c) for s, cs in results.items() for c in cs if not c[1]]
    lines = [f"## FM-1 emulator test: {'PASS' if not failed else 'FAIL'}", "",
             f"Firmware: `{pathlib.Path(a.fwsc).name}` (sha256 `{hashlib.sha256(pathlib.Path(a.fwsc).read_bytes()).hexdigest()[:16]}`)", "",
             "| Scenario | Check | Result | Detail |", "|---|---|---|---|"]
    for s, cs in results.items():
        for label, ok, detail in cs:
            lines.append(f"| {s} | {label} | {'✅' if ok else '❌'} | {detail} |")
    if ref:
        ref_results, ref_digests = ref
        label = a.reference_label or f"`{pathlib.Path(a.reference).name}`"
        lines += ["", f"### Compared with {label} (informational)", "",
                  "Same key presses on both. Identical screen and audio means the two images behave the same in these scenarios.", "",
                  "| Scenario | Screen | Audio | Reference checks |", "|---|---|---|---|"]
        for s in digests:
            same = lambda k: "identical" if digests[s][k] == ref_digests[s][k] else "differs"
            ref_ok = all(c[1] for c in ref_results[s])
            lines.append(f"| {s} | {same('lcd')} | {same('audio')} | {'pass' if ref_ok else 'FAIL'} |")
        ref_pc = all(c[1] for c in ref_results["powercut"])
        lines.append(f"| powercut | | | {'pass' if ref_pc else 'FAIL'} |")
    lines += ["", "Screenshots (PNG) and audio (WAV) for each scenario are in the run's artifact."]
    summary = "\n".join(lines) + "\n"
    (out / "summary.md").write_text(summary)
    (out / "results.json").write_text(json.dumps({"build": {s: [list(c) for c in cs] for s, cs in results.items()},
                                                  "digests": digests,
                                                  "reference": ref and ref[1]}, indent=1))
    print(summary)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

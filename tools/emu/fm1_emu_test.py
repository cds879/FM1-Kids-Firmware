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
NOTE_AT = 2000                     # ms: when the note scenario presses C4
LATENCY_LIMIT = 150                # ms; a sanity bound (stock V15 measures ~75 ms in the emulator)
# Power cuts: during the first flash write, one halfway, and the last one the test
# run makes (counted from a run without a cut, so every cut lands on a real write).

# name, emulated ms, key script (ms:KEY, ^ = release), what to check
SCENARIOS = [
    ("boot",  4000, "",                         "screen drawn, no stray sound"),
    ("note",  4000, "2000:C4@100,3500:C4^",     "holding C4 makes sound, quickly"),
    ("seq",   3000, "2000:SEQ,2100:SEQ^",       "the screen changes when SEQ is pressed"),
    # SEQ, then the white keys for steps 1, 5, 9 and 13, then PLAY: the pattern must play by itself
    ("beat",  5000, "1000:SEQ,1100:SEQ^,1200:F3,1300:F3^,1400:C4,1500:C4^,1600:G4,1700:G4^,1800:D5,1900:D5^,"
                    "2200:PLAY,2300:PLAY^",     "a pattern made with SEQ plays on its own"),
]
PLAYED_FROM = 2600                 # ms: the beat scenario has released every key by then


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
def samples(raw):
    n = len(raw) // 2
    return struct.unpack(f"<{n}h", raw[:n * 2])


def envelope(raw, ms=20):
    """Loudness every `ms` (RMS of both channels), from the first audible sample."""
    s = samples(raw)
    start = next((i for i, v in enumerate(s) if abs(v) > 1), len(s)) & ~1
    s, step = s[start:], RATE * 2 * ms // 1000
    return [(sum(v * v for v in s[i:i + step]) / step) ** 0.5 for i in range(0, len(s) - step + 1, step)], start // 2


def compare_audio(a_path, b_path):
    a, b = pathlib.Path(a_path).read_bytes(), pathlib.Path(b_path).read_bytes()
    if a == b:
        return "identical"
    ea, sa = envelope(a)
    eb, sb = envelope(b)
    loud_a, loud_b = any(v > 1 for v in ea), any(v > 1 for v in eb)
    if not loud_a and not loud_b:
        return "both silent"
    if loud_a != loud_b:
        return "differs (only one makes sound)"
    n = min(len(ea), len(eb))
    ea, eb = ea[:n], eb[:n]
    ma, mb = sum(ea) / n, sum(eb) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(ea, eb))
    den = (sum((x - ma) ** 2 for x in ea) * sum((y - mb) ** 2 for y in eb)) ** 0.5
    corr = num / den if den else 0.0
    level = (mb / ma) if ma else 0.0
    shift = f", starts {abs(sb - sa) / RATE * 1000:.1f} ms {'later' if sb > sa else 'earlier'}" if sb != sa else ""
    verdict = "same sound" if corr > 0.98 and 0.9 < level < 1.1 else "differs"
    return f"{verdict} (loudness match {corr:.3f}, level {level:.2f}x{shift})"


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
    r = run(emu, fwsc, ms, keys, work, frames=(name in ("seq", "note", "beat")))
    checks = [("emulator ran cleanly", not r["problems"], "; ".join(r["problems"]) or "ok")]
    if r["lcd"]:
        n = colours(r["lcd"])
        checks.append(("screen drawn", n >= 4, f"{n} colours on the LCD"))
    pk = peak(r["raw"])
    if name == "boot":
        checks.append(("no stray sound", pk < SILENT_PEAK, f"peak {pk}"))
    latency = None
    if name == "note":
        checks.append(("note makes sound", pk >= LOUD_PEAK, f"peak {pk}"))
        # frames/audio.raw starts at power-on, so its first sound dates the key press's effect
        boot_audio = work / "frames" / "audio.raw"
        if boot_audio.exists():
            s = samples(boot_audio.read_bytes())
            first = next((i for i, v in enumerate(s) if abs(v) > 8), None)
            if first is not None:
                latency = first // 2 * 1000 / RATE - NOTE_AT
        checks.append(("key to sound under 150 ms", latency is not None and 0 <= latency < LATENCY_LIMIT,
                       f"{latency:.1f} ms" if latency is not None else "no sound in the boot-aligned recording"))
    if name == "beat":
        boot_audio = work / "frames" / "audio.raw"
        s = samples(boot_audio.read_bytes()) if boot_audio.exists() else ()
        after = s[PLAYED_FROM * RATE // 1000 * 2:]
        loud = max((abs(v) for v in after), default=0)
        checks.append(("pattern plays with no key held", loud >= LOUD_PEAK, f"peak {loud} after {PLAYED_FROM} ms"))
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
              "audio": hashlib.sha256(r["raw"]).hexdigest()[:16], "raw": str(work / "audio.raw"), "latency": latency}
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


# ---------- FM1 Kids behaviours (--suite kids) ----------
# A pattern on steps 1, 5, 9, 13 made in BEAT mode (SEQ, then white keys).
PATTERN = "1000:SEQ,1100:SEQ^,1200:F3,1300:F3^,1400:C4,1500:C4^,1600:G4,1700:G4^,1800:D5,1900:D5^"
QUIET_PEAK = 400                   # a cleared or quieted loop stays below this (notes may still fade)


def window_peak(work, start_ms, end_ms):
    path = work / "frames" / "audio.raw"
    s = samples(path.read_bytes()) if path.exists() else ()
    seg = s[start_ms * RATE // 1000 * 2:end_ms * RATE // 1000 * 2]
    return max((abs(v) for v in seg), default=0)


def onsets(work, start_ms, end_ms):
    """Separate note starts: the 10 ms loudness jumps well above where it was 20 ms before."""
    path = work / "frames" / "audio.raw"
    s = samples(path.read_bytes()) if path.exists() else ()
    step = RATE // 100
    env = []
    for i in range(start_ms * RATE // 1000, end_ms * RATE // 1000 - step, step):
        seg = s[i * 2:(i + step) * 2:2]
        env.append((sum(v * v for v in seg) / max(1, len(seg))) ** 0.5)
    count, last = 0, -10
    for i in range(2, len(env)):
        if env[i] > 300 and env[i] > 1.8 * env[i - 2] and i - last >= 5:
            count, last = count + 1, i
    return count


def kid_run(emu, fwsc, out, name, ms, keys, flash=None):
    r = run(emu, fwsc, ms, keys, out / name, flash=flash, frames=True)
    if r["lcd"]:
        write_png(out / f"{name}.png", r["lcd"])
    write_wav(out / f"{name}.wav", r["raw"])
    return r, [("emulator ran cleanly", not r["problems"], "; ".join(r["problems"]) or "ok")]


def heard(work, start, end, loud=True):
    pk = window_peak(work, start, end)
    ok = pk >= LOUD_PEAK if loud else pk < QUIET_PEAK
    return ok, f"peak {pk} at {start / 1000:g}-{end / 1000:g} s"


def kid_checks(emu, fwsc, out):
    results = {}
    # Start over: FX asks, FX again clears; LFO (undo) brings the loop back.
    name = "startover"
    r, c = kid_run(emu, fwsc, out, name, 7000, PATTERN + ",2200:PLAY,2300:PLAY^,3000:FX,3100:FX^,3300:FX,3400:FX^,5000:LFO,5100:LFO^")
    c += [("loop plays", *heard(out / name, 2600, 3000)),
          ("FX twice clears it", *heard(out / name, 4200, 4900, loud=False)),
          ("LFO (undo) brings it back", *heard(out / name, 5600, 7000))]
    results[name] = c
    # Songs: ENV opens the list; a demo loads its sounds and patterns and plays;
    # MY SONG brings the kid's own loop back.
    name = "songs"
    r, c = kid_run(emu, fwsc, out, name, 10000, PATTERN + ",2200:PLAY,2300:PLAY^,3000:ENV,3100:ENV^,3300:e0:1,"
                   "3500:PLAY,3600:PLAY^,6500:e0:-1,6700:PLAY,6800:PLAY^")
    try:
        demo_lcd, mine_lcd = frame_lcd(out / name, 6000)[2], frame_lcd(out / name, 9500)[2]
    except (FileNotFoundError, IndexError):
        demo_lcd = mine_lcd = None
    c += [("my loop plays", *heard(out / name, 2600, 3000)),
          ("the demo song plays", *heard(out / name, 4200, 6400)),
          ("MY SONG comes back and plays", *heard(out / name, 7400, 10000)),
          ("the demo shows its own instruments", demo_lcd is not None and demo_lcd != mine_lcd, "screens differ")]
    results[name] = c
    # Keys page (EDIT twice): SCALE changes where keys snap; CHORDS plays three notes per key.
    name = "keys"
    keyruns = {}
    for label, turns in (("happy", ""), ("sad", "2000:EDIT,2100:EDIT^,2200:EDIT,2300:EDIT^,2500:e2:1,"),
                         ("chord", "2000:EDIT,2100:EDIT^,2200:EDIT,2300:EDIT^,2500:e3:1,")):
        r, c1 = kid_run(emu, fwsc, out, f"{name}-{label}", 4500, turns + "3000:F#4@100,4000:F#4^")
        audio = out / f"{name}-{label}" / "frames" / "audio.raw"
        keyruns[label] = (c1[0][1], audio.read_bytes()[3000 * RATE // 1000 * 4:4000 * RATE // 1000 * 4]
                          if audio.exists() else b"", window_peak(out / f"{name}-{label}", 3000, 4000))
    c = [("emulator ran cleanly", all(v[0] for v in keyruns.values()), "3 runs"),
         ("a key plays", *heard(out / f"{name}-happy", 3000, 4000)),
         ("SAD moves the key to another note", keyruns["sad"][1] != keyruns["happy"][1] and keyruns["sad"][2] > QUIET_PEAK,
          f"peak {keyruns['sad'][2]}"),
         ("CHORDS adds notes", keyruns["chord"][2] > keyruns["happy"][2] * 1.2,
          f"peak {keyruns['chord'][2]} vs {keyruns['happy'][2]} for one note")]
    results[name] = c
    # Song parts: in BEAT, ALGORITHM adds PART B as a copy of A. With B thinned
    # to one step, the song (A then B) has fewer notes than A looping alone.
    name = "parts"
    counts = {}
    for label, turns in (("a", ""), ("ab", ",2200:e1:1,2600:F3,2650:F3^,2800:C4,2850:C4^,3000:G4,3050:G4^")):
        r, c1 = kid_run(emu, fwsc, out, f"{name}-{label}", 13000, PATTERN + turns + ",4000:PLAY,4100:PLAY^")
        counts[label] = (c1[0][1], onsets(out / f"{name}-{label}", 4500, 12500))
    c = [("emulator ran cleanly", counts["a"][0] and counts["ab"][0], "2 runs"),
         ("part A loops", counts["a"][1] >= 8, f"{counts['a'][1]} notes in 8 s"),
         ("PART B plays after A", 0 < counts["ab"][1] <= counts["a"][1] * 0.8,
          f"{counts['ab'][1]} notes in 8 s vs {counts['a'][1]} for A alone")]
    results[name] = c
    # Animations: a played note floats up its tile and the tile dances; the screen settles after.
    name = "sparks"
    r, c = kid_run(emu, fwsc, out, name, 7000, "3000:C4@100,3500:C4^")
    try:
        f1, f2 = frame_lcd(out / name, 3150)[2], frame_lcd(out / name, 3400)[2]
        s1, s2 = frame_lcd(out / name, 6000)[2], frame_lcd(out / name, 6800)[2]
    except (FileNotFoundError, IndexError):
        f1 = f2 = s1 = s2 = None
    c += [("the tile moves while a note plays", f1 is not None and f1 != f2, "frames at 3.15 s and 3.4 s differ"),
          ("the screen settles afterwards", s1 is not None and s1 == s2, "frames at 6 s and 6.8 s match")]
    # The song playing moves the pictures too (no keys pressed).
    r, c1 = kid_run(emu, fwsc, out, f"{name}-song", 6000, PATTERN + ",2200:HOME,2300:HOME^,2500:PLAY,2600:PLAY^")
    try:
        frames = [frame_lcd(out / f"{name}-song", ms)[2] for ms in range(4000, 5600, 100)]
    except (FileNotFoundError, IndexError):
        frames = []
    c += [("the song makes the pictures move", len(set(frames)) >= 4, f"{len(set(frames))} different frames in 1.6 s")]
    results[name] = c
    # Gallery: each kind of animation, frame by frame (sparks-gallery.png; also
    # printed to the log as base64 so it can be looked at without the artifact).
    rows = []
    for label, turns in (("piano", ""), ("trumpet", "2000:e1:2,"), ("bells", "2000:e1:6,"), ("bass", "2000:e1:9,"),
                         ("robot", "2000:e1:10,"), ("dream", "2000:e1:11,"), ("game", "2000:e1:12,"), ("drums", "2000:e1:14,")):
        kid_run(emu, fwsc, out, f"{name}-{label}", 3800, turns + "3000:C4@110,3300:C4^")
        row = []
        for ms in (2900, 3000, 3100, 3200, 3300, 3400, 3600):
            try:
                row.append(crop(read_ppm(out / f"{name}-{label}" / "frames" / f"{ms // 100:06d}.ppm"),
                                (LCD[0], LCD[1] + 80, 130, 160)))
            except (FileNotFoundError, IndexError):
                row.append((130, 160, b"\0" * 130 * 160 * 3))
        rows.append(row)
    sw, sh = 130 * 7, 160 * len(rows)
    px = b"".join(b"".join(cell[2][r * 390:(r + 1) * 390] for cell in row) for row in rows for r in range(160))
    write_png(out / "sparks-gallery.png", (sw, sh, px))
    import base64
    print("GALLERY-PNG-BEGIN")
    print(base64.b64encode((out / "sparks-gallery.png").read_bytes()).decode())
    print("GALLERY-PNG-END")
    # Play along: a tune from the ENV list; only the right key moves it on.
    name = "learn"
    r, c = kid_run(emu, fwsc, out, name, 6000, "2000:ENV,2100:ENV^,2300:e0:17,2600:PLAY,2700:PLAY^,"
                   "4000:D4@100,4200:D4^,4500:C4@100,4700:C4^")

    def foot(ms):
        try:
            return crop(read_ppm(out / name / "frames" / f"{ms // 100:06d}.ppm"), (LCD[0], LCD[1] + 380, 480, 100))[2]
        except (FileNotFoundError, IndexError):
            return None
    c += [("the tune starts", foot(3900) is not None and foot(3900) != foot(2200), "play-along shown"),
          ("a wrong key does not move it on", foot(3900) == foot(4400), "same next note"),
          ("the right key moves it on", foot(4400) != foot(5200), "next note shown"),
          ("the keys still play", *heard(out / name, 4500, 4900))]
    results[name] = c
    # Quiet: SEL (SCL in the emulator) mutes the chosen instrument and unmutes it again.
    name = "quiet"
    r, c = kid_run(emu, fwsc, out, name, 6500, PATTERN + ",2200:PLAY,2300:PLAY^,3000:HOME,3100:HOME^,3200:SCL,3300:SCL^,4500:SCL,4600:SCL^")
    c += [("loop plays", *heard(out / name, 2600, 3100)),
          ("SEL makes it quiet", *heard(out / name, 3900, 4500, loud=False)),
          ("SEL again brings it back", *heard(out / name, 5000, 6500))]
    results[name] = c
    # Instrument picker: ALGORITHM changes the tile, and the new instrument plays.
    name = "picker"
    r, c = kid_run(emu, fwsc, out, name, 5000, "2000:e1:3,3500:C4@100,4500:C4^")
    try:
        changed = frame_lcd(out / name, 1900)[2] != frame_lcd(out / name, 3000)[2]
    except (FileNotFoundError, IndexError):
        changed = False
    c += [("ALGORITHM changes the picture", changed, "changed" if changed else "unchanged"),
          ("the new instrument plays", *heard(out / name, 3600, 4500))]
    results[name] = c
    # More instruments: PIANO, TRUMPET and BELLS share the DX7 engine but each
    # gets its own voice; SOUND then moves to another trumpet-family voice.
    name = "instruments"
    tune = "3500:C4@100,4500:C4^"
    runs = {}
    for label, turns in (("piano", ""), ("trumpet", "2000:e1:2,"), ("bells", "2000:e1:6,"),
                         ("trumpet-2", "2000:e1:2,2900:e4:1,")):
        r, c1 = kid_run(emu, fwsc, out, f"{name}-{label}", 5000, turns + tune)
        audio = out / f"{name}-{label}" / "frames" / "audio.raw"
        seg = audio.read_bytes()[3500 * RATE // 1000 * 4:4500 * RATE // 1000 * 4] if audio.exists() else b""
        try:
            lcd = frame_lcd(out / f"{name}-{label}", 4400)[2]   # after the name notice
        except (FileNotFoundError, IndexError):
            lcd = None
        runs[label] = (c1[0][1], seg, lcd)
    c = [("emulator ran cleanly", all(v[0] for v in runs.values()), "4 runs")]
    for label in ("piano", "trumpet", "bells"):
        c.append((f"{label} plays", *heard(out / f"{name}-{label}", 3600, 4500)))
    def differ(label, a, b):
        sound, screen = runs[a][1] != runs[b][1], runs[a][2] != runs[b][2]
        return (label, sound and screen, f"sound {'differs' if sound else 'same'}, screen {'differs' if screen else 'same'} vs {a}")
    c += [differ("TRUMPET has its own picture and voice", "piano", "trumpet"),
          differ("BELLS has its own picture and voice", "trumpet", "bells"),
          differ("SOUND moves to another trumpet voice", "trumpet", "trumpet-2")]
    results[name] = c
    # BASS uses the DX7 bass voices; SOUND runs on through them to the 303.
    name = "bass"
    for label, turns in (("bass", "2000:e1:9,"), ("bass-303", "2000:e1:9,2900:e4:16,")):
        r, c1 = kid_run(emu, fwsc, out, f"{name}-{label}", 5000, turns + tune)
        audio = out / f"{name}-{label}" / "frames" / "audio.raw"
        seg = audio.read_bytes()[3500 * RATE // 1000 * 4:4500 * RATE // 1000 * 4] if audio.exists() else b""
        try:
            lcd = frame_lcd(out / f"{name}-{label}", 4400)[2]
        except (FileNotFoundError, IndexError):
            lcd = None
        runs[label] = (c1[0][1], seg, lcd)
    c = [("emulator ran cleanly", runs["bass"][0] and runs["bass-303"][0], "2 runs"),
         ("bass plays", *heard(out / f"{name}-bass", 3600, 4500)),
         ("the 303 bass plays", *heard(out / f"{name}-bass-303", 3600, 4500)),
         differ("BASS has its own picture and voice", "piano", "bass"),
         differ("SOUND moves on to the 303 bass line", "bass", "bass-303")]
    # The new project's own bass tile (track 2) must already be the kid BASS:
    # choosing BASS for it again must not change its sound.
    for label, turns in (("tile", "1500:e0:1,"), ("tile-picked", "1500:e0:1,2000:e1:15,")):
        r, c1 = kid_run(emu, fwsc, out, f"{name}-{label}", 5000, turns + tune)
        audio = out / f"{name}-{label}" / "frames" / "audio.raw"
        runs[label] = (c1[0][1], audio.read_bytes()[3500 * RATE // 1000 * 4:4500 * RATE // 1000 * 4]
                       if audio.exists() else b"", None)
    def crossings(raw):                       # zero crossings of the left channel: a rough brightness
        import array
        x = array.array("h", raw)[0::2]
        return sum(1 for i in range(1, len(x)) if (x[i - 1] < 0) != (x[i] < 0))
    zt, zp, z3 = (crossings(runs[k][1]) for k in ("tile", "tile-picked", "bass-303"))
    c += [("the bass tile plays", *heard(out / f"{name}-tile", 3600, 4500)),
          ("the bass tile is already the kid BASS", zt > 0 and abs(zt - zp) <= max(zt, zp) // 20,
           f"{zt} zero crossings vs {zp} after picking BASS again ({z3} for the 303)")]
    results[name] = c
    # EDIT: the sound panel. REVERB and LONG keep a note ringing after the key is let go.
    name = "sound-edit"
    short = "3000:C4@100,3300:C4^"
    tails = {}
    for label, turns in (("dry", ""), ("reverb", "2000:EDIT,2100:EDIT^,2300:e2:15,"),
                         ("reverb-room", "2000:EDIT,2100:EDIT^,2300:e2:15,2400:e5:40,"),
                         ("long", "2000:EDIT,2100:EDIT^,2300:e4:30,")):
        r, c1 = kid_run(emu, fwsc, out, f"{name}-{label}", 9000 if label == "long" else 4500, turns + short)
        tails[label] = (c1[0][1], window_peak(out / f"{name}-{label}", 3400, 3800),
                        window_peak(out / f"{name}-{label}", 3800, 4400))
    def rings(label, run, w):
        ok = tails[run][w] > 2 * tails["dry"][w] + 50
        return (label, ok, f"peak {tails[run][w]} vs {tails['dry'][w]} dry, {'0.1-0.5' if w == 1 else '0.5-1.1'} s after the key")
    c = [("emulator ran cleanly", all(v[0] for v in tails.values()), "4 runs"),
         rings("REVERB rings on", "reverb", 1),
         rings("ROOM makes the reverb longer", "reverb-room", 2),
         rings("LONG rings on", "long", 2),
         ("LONG turned all the way still lets the note end",
          *heard(out / f"{name}-long", 8000, 9000, loud=False))]
    results[name] = c
    # Autosave: make a pattern, leave it alone, then power off and on: it is still there.
    name = "autosave"
    flash = out / name / "flash.bin"
    (out / name).mkdir(parents=True, exist_ok=True)
    flash.unlink(missing_ok=True)
    r, c = kid_run(emu, fwsc, out, name, 16000, PATTERN, flash=flash)
    m = re.search(r"flash: (\d+) erases / programs", r["log"])
    r2, c2 = kid_run(emu, fwsc, out, name + "-reboot", 4500, "2000:PLAY,2100:PLAY^", flash=flash)
    c += [("emulator ran cleanly after reboot", *c2[0][1:]),
          ("the pattern survives a reboot without SAVE", *heard(out / (name + "-reboot"), 2600, 4500))]
    c[-1] = (c[-1][0], c[-1][1], c[-1][2] + f"; {m.group(1) if m else 0} flash writes before")
    results[name] = c
    # Arpeggiator: tap ARP, hold one key: it plays repeated notes.
    name = "arp"
    r, c = kid_run(emu, fwsc, out, name, 4000, "1500:ARP,1600:ARP^,2000:C4@100,3600:C4^")
    n_arp = onsets(out / name, 1950, 3650)
    c += [("one held key plays several notes", n_arp >= 3, f"{n_arp} notes in 1.7 s")]
    results[name] = c
    # Timing: hold ARP and turn K1 (FAST): 1/16 plays clearly more notes than 1/4.
    name = "arp-timing"
    counts = {}
    for label, turns in (("slow", "1400:e2:-3"), ("fast", "1400:e2:-3,1500:e2:2")):
        r, c = kid_run(emu, fwsc, out, f"{name}-{label}", 4000,
                       f"1000:ARP,1100:ARP^,1300:ARP,{turns},1700:ARP^,2000:C4@100,3600:C4^")
        counts[label] = onsets(out / f"{name}-{label}", 1950, 3650)
    c += [("FAST 1/16 plays more notes than 1/4", counts["fast"] >= counts["slow"] + 3,
           f"1/4: {counts['slow']} notes, 1/16: {counts['fast']} notes")]
    results[name] = c
    # Latch: hold ARP (about a second) turns HOLD on; the notes carry on after the key is let go.
    name = "arp-latch"
    r, c = kid_run(emu, fwsc, out, name, 5000, "1000:ARP,2000:ARP^,2500:C4@100,2700:C4^")
    n_latch = onsets(out / name, 3000, 5000)
    c += [("notes keep going after the key is let go", n_latch >= 3, f"{n_latch} notes in 2 s after release")]
    results[name] = c
    # Battery: a low reading turns the battery icon red.
    name = "battery"
    r, c = kid_run(emu, fwsc, out, name, 3000, "100:a3:500")
    red = 0
    for ms in range(1500, 3000, 100):
        try:
            w, h, px = frame_lcd(out / name, ms)
        except (FileNotFoundError, IndexError):
            continue
        for y in range(28, 52):                     # the battery icon, drawn 2x
            for x in range(176, 220):
                i = (y * w + x) * 3
                red += px[i] > 200 and px[i + 1] < 120 and px[i + 2] < 120
    c += [("low battery shows a red battery", red > 20, f"{red} red pixels")]
    results[name] = c
    return results


def test(emu, fwsc, out, suite="generic"):
    out.mkdir(parents=True, exist_ok=True)
    results, digests = {}, {}
    for name, *_ in SCENARIOS:
        results[name], digests[name] = check(name, emu, fwsc, out)
    results["powercut"] = power_cuts(emu, fwsc, out)
    if suite == "kids":
        results.update(kid_checks(emu, fwsc, out))
    return results, digests


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emu", required=True)
    ap.add_argument("--fwsc", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--reference")
    ap.add_argument("--reference-label", help="how to name the reference in the summary")
    ap.add_argument("--suite", choices=["generic", "kids"], default="generic",
                    help="kids: also test FM1 Kids behaviours (start over, songs, learn, keys, parts, sparks, quiet, picker, instruments, sound edit, autosave, arpeggiator, battery)")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    results, digests = test(a.emu, a.fwsc, out / "build", a.suite)
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
                  "Same key presses on both. \"Same sound\" means the loudness over time matches once both are lined up at their first sound;",
                  "a small start shift is expected from a different compiler build (code runs at slightly different speeds).", "",
                  "| Scenario | Screen | Audio | Reference checks |", "|---|---|---|---|"]
        for s in digests:
            screen = "identical" if digests[s]["lcd"] == ref_digests[s]["lcd"] else "differs"
            ref_ok = all(c[1] for c in ref_results[s])
            audio = compare_audio(digests[s]['raw'], ref_digests[s]['raw'])
            if digests[s].get("latency") is not None and ref_digests[s].get("latency") is not None:
                audio += f"; key to sound {digests[s]['latency']:.1f} ms vs {ref_digests[s]['latency']:.1f} ms"
            lines.append(f"| {s} | {screen} | {audio} | {'pass' if ref_ok else 'FAIL'} |")
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

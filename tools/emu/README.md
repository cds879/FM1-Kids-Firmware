# Emulator tests

Every CI run boots the freshly built `.fwsc` in
[fm1-emu](https://github.com/joakimeriksson/fm1-emu) (MIT), an emulator of the
FM-1, and checks that:

- **boot:** the firmware starts, draws its screen, and makes no stray sound
- **note:** holding C4 makes sound
- **seq:** pressing SEQ changes the screen
- **powercut:** after power is cut during a flash write (the first, a middle
  one, and the last of the writes the firmware makes in the test), the next
  boot still starts and plays

The emulator is deterministic: the same firmware and key presses always give
byte-identical screen and audio. CI also runs M-VAVE's official FM1 Open build,
taken from the newest release of
[m-vave/fm1_open](https://github.com/m-vave/fm1_open) (or their workbench page), through the same scenarios and reports
whether the two behave identically. That comparison is informational, because
the official build can be a newer version than this repository.

Results are on the run's summary page. Screenshots and audio are in the
`fm1-emulator-<sha>` artifact.

Run it locally (build `fm1-live` from the emulator repo with
`cargo build --release -p fm1-live`):

```sh
python3 tools/emu/fm1_emu_test.py --emu PATH/fm1-live \
    --fwsc build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc --out build/emu
```

Key names for new scenarios (in `SCENARIOS`): the printed button names
(`PLAY`, `SEQ`, `HOME`, `REC`…), notes `F3`..`G5`, `^` to release, `@VEL` for
velocity, `eN:+3` to turn encoder N. Passing in the emulator is strong
evidence, not proof: the FM-1's CPU was reverse-engineered for it.

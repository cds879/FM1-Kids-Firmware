# FM1 Kids interface

A simple interface for young children that replaces M-VAVE's FM1 Open
screens. The sound engines, sequencer, saving and firmware updates are
M-VAVE's, unchanged.

## How to play

| Control | What it does |
|---|---|
| **SELECT** knob | Pick an instrument. Each tile has a picture: piano, bass, game (chiptune), guitar, fun (samples), drums |
| **Piano keys** | Play the instrument. On melodic instruments every key fits ("happy keys": C major pentatonic) |
| **SEQ** | BEAT mode: white keys switch the 16 steps on and off; black keys pick the drum (or note) to place |
| **HOME** | Back to the instrument tiles |
| **PLAY** | Start / stop |
| **REC** | Record what you play into the loop |
| **KNOB 1–4** | LOUD, BRIGHT, SOUND (a different sound for this instrument), SPEED |
| **EDIT** | Undo |
| **SAVE** | Save the song |
| hold **FX** | Clear this instrument's loop (EDIT brings it back) |
| **OCT− / OCT+** | Lower / higher |

The MASTER knob is capped at about three-quarters volume to protect young
ears (`KID_MASTER_LIMIT` in `kid_config.h`; 1023 removes the cap). Every
track loops 16 steps, one row of white keys. Choosing a muted instrument
unmutes it.

## How it is built

- `app/screen.c` is M-VAVE's interface. Five added lines include
  `kid/kid_screen.inc` instead when `FM1_KID_UI` is 1 (`kid_config.h`).
  Set it to 0 to build the original.
- The FM-1's flash is almost full: M-VAVE's build leaves about 345 bytes.
  So the kid interface *replaces* the original instead of sitting beside
  it, and must stay smaller. Each CI run reports the bytes left. Deep
  editing (FM operators, effects, MIDI) is still possible from M-VAVE's
  browser workbench.
- **Firmware updates are unaffected.** The SDK handles update messages
  before the application sees any SysEx, so neither interface can block
  them. M-VAVE's build checks refuse an image without the update code.
  Install other firmware the usual way, with the workbench or M-UPGRADE.
- Drawing is the band painter adapted from M-VAVE's screen: only parts of
  the screen whose contents changed are sent to the LCD.

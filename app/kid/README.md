# FM1 Kids interface

A simple interface for young children that replaces M-VAVE's FM1 Open
screens. The sound engines, sequencer, saving and firmware updates are
M-VAVE's, unchanged.

## How to play

| Control | What it does |
|---|---|
| **SELECT** knob | Pick an instrument. Each tile has a picture: piano, bass, game (chiptune), guitar, fun (samples), drums |
| **ALGORITHM** knob | Change the chosen tile's instrument; it switches when the knob rests |
| **Piano keys** | Play the instrument. On melodic instruments every key fits ("happy keys": C major pentatonic) |
| **SEQ** | BEAT mode: white keys switch the 16 steps on and off; black keys pick the drum (or note) to place. Other drums show as dots; melody steps show their note by colour and height |
| **HOME** | Back to the instrument tiles |
| **PLAY** | Start / stop |
| **REC** | Record what you play into the loop |
| **KNOB 1–4** | LOUD, BRIGHT (each engine's brightness), SOUND (shows the sound's name), SPEED |
| **ARP** | Quiet / unquiet the chosen instrument |
| **EDIT** | Undo |
| **SAVE** | Save now. The song also saves by itself once it has been left alone for a few seconds while stopped |
| **FX**, then **FX** | Start the chosen instrument over (EDIT brings it back) |
| **OCT− / OCT+** | Lower / higher |

The battery icon shows the stock firmware's three levels, a + while on USB
power, and flashes red with CHARGE ME! when nearly empty (the song saves
itself then too). A keyboard on MIDI channel 1 (USB, Bluetooth or the DIN
jack) plays the chosen instrument; channels 2–16 keep their usual tracks.
The firmware identifies itself as `FM-1_8100001` (FM1 Kids 0.1), still in
M-VAVE's open range, so the workbench can install other firmware over it.

**For parents, hold GLO** to open the Bluetooth MIDI panel. By default a
phone, tablet or computer connects to the FM-1. To use a Bluetooth MIDI
keyboard, hold GLO and press PLAY to search, turn SELECT to choose, and press
SAVE to connect. REC disconnects; HOME switches back to the default.

The MASTER knob is capped at about three-quarters volume to protect young
ears (`KID_MASTER_LIMIT` in `kid_config.h`; 1023 removes the cap). Every
track loops 16 steps, one row of white keys.

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

To put it on an FM-1, see [docs/FLASHING.md](../../docs/FLASHING.md).

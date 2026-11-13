# FM1 Kids interface

A simple interface for young children that replaces M-VAVE's FM1 Open
screens. The sound engines, sequencer, saving and firmware updates are
M-VAVE's, unchanged.

## How to play

| Control | What it does |
|---|---|
| **SELECT** knob | Pick a tile. Each tile shows its instrument's picture and name |
| **ALGORITHM** knob | Change the chosen tile's instrument: piano, organ, trumpet, flute, violin, choir, bells, xylophone, guitar, bass, robot, dream, game (chiptune), fun (samples) or drums. It switches when the knob rests |
| **Piano keys** | Play the instrument. On melodic instruments every key fits the scale (HAPPY, a C major pentatonic, to start; see EDIT) |
| *(fun)* | Every note played floats up its tile as a little coloured note; each tile's picture dances and shows a loudness bar while it sounds, also when the song plays it |
| **SEQ** | BEAT mode: white keys switch the 16 steps on and off; black keys pick the drum (or note) to place. Other drums show as dots; melody steps show their note by colour and height |
| **ALGORITHM** in BEAT | Choose the song part, A to D. Turning to a new part copies the part before it, ready to change. The song plays A, B, C, D in turn and BEAT follows along while it plays. A part with no steps left drops out of the song. In BEAT, PRESETS (or the black keys) picks the drum |
| **HOME** | Back to the instrument tiles |
| **PLAY** | Start / stop |
| **REC** | Record what you play into the loop |
| **KNOB 1–4** | LOUD, BRIGHT (each engine's brightness), SOUND (another version of the same instrument, with its name), SPEED. Turn fast for bigger steps |
| **ARP** | Arpeggiator. Tap: UP, DOWN, BOUNCE (up and down), RANDOM, OFF. Hold about a second: HOLD (notes keep going after the keys are let go). Hold and turn KNOB 1–4: FAST (1/4, 1/8, 1/16, 1/32 of a beat), OCTAVES (1–4), LENGTH (25–100%), SWING (0–50%). It follows SPEED. On drums it repeats the drum |
| **SEL** | Quiet / unquiet the chosen instrument |
| **EDIT** | Sound panel for the chosen instrument: KNOB 1–4 are REVERB, ECHO, LONG (how long notes ring after the key is let go) and ROOM (reverb size, shared by all instruments). Press EDIT again for **KEYS**: KNOB 1 is SCALE (HAPPY, SAD, DO RE MI, SPOOKY, DREAMY, ALL KEYS) and KNOB 2 is CHORDS (one key plays a whole chord, also when placing steps in BEAT mode). EDIT a third time (or HOME) closes it |
| **ENV** | Songs. Turn SELECT to choose MY SONG or one of the 16 demo songs, then press PLAY to load it (its instruments and patterns) and play. Everything in a demo can be changed. ENV again closes the list |
| **LFO** | Undo |
| **SAVE** | Save now. The song also saves by itself once it has been left alone for a few seconds while stopped |
| **FX**, then **FX** | Start the chosen instrument over (LFO brings it back) |
| **OCT− / OCT+** | Lower / higher |

The battery icon shows the stock firmware's three levels, a + while on USB
power, and flashes red with CHARGE ME! when nearly empty (the song saves
itself then too). A keyboard on MIDI channel 1 (USB, Bluetooth or the DIN
jack) plays the chosen instrument; channels 2–16 keep their usual tracks.
The firmware identifies itself as `FM-1_8100001` (FM1 Kids 0.7), still in
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

## Instruments

Several instruments share the DX7 engine and differ by their starting voice
from the 128 factory voices. They are set in `kind_sound` in
`kid_screen.inc`, so any of them can be swapped after listening on the FM-1:

| Instrument | Engine | Starting factory voice |
|---|---|---|
| PIANO | DX7 | 1 (piano) |
| ORGAN | DX7 | 2 (organ) |
| TRUMPET | DX7 | 65 (brass) |
| FLUTE | DX7 | 66 (woodwind) |
| VIOLIN | DX7 | 67 (strings) |
| CHOIR | DX7 | 68 (voice) |
| BELLS | DX7 | 121 (tubular bells) |
| XYLOPHONE | DX7 | 118 (kalimba) |
| ROBOT | DX7 | 3 (synth lead) |
| DREAM | DX7 | 4 (synth pad) |
| GUITAR | pluck, then DX7 | |
| BASS | DX7, then 303 | 35 (bass 1) |
| GAME | chiptune (PSG) | |
| FUN | sampler | |
| DRUMS | 808 drums | |

SOUND steps through every version of the same instrument, and the picture
always matches what is playing (`voice_look`):

| Picture | DX7 factory voices (1–128) |
|---|---|
| PIANO | the 8 pianos |
| ORGAN | the 8 organs |
| ROBOT | the 8 synth leads, and odd sounds (BOWOAN, MONO, SAW EM UP, TAMBOURINE, BRUSHES, PERCUSSION, WHAT IF, ...) |
| DREAM | the 8 synth pads, WAVES, DESCENT, ECHO |
| GUITAR | the 8 guitars and 8 DS guitars, after the pluck engine's sounds |
| BASS | the 8 basses and 8 synth basses, then the 303 bass line |
| TRUMPET | the 8 brass |
| FLUTE | the 8 woodwinds |
| VIOLIN | the 8 strings |
| CHOIR | the 8 voices |
| BELLS | FROGBELL, ELECT.GONG, TIBET, COWBELL, FINGERCYM, CLANG BELL, BI BEN, GAMALONG, TUB BELLS |
| XYLOPHONE | BLOCK, LOG DRUM, TIMPANI, STEEL DRUM, CONGA, KALIMBA, TOM TOMS, TIMBALI |

## Songs

ENV opens the song list: MY SONG and M-VAVE's 16 demo songs (NEON KEYS,
ACID ALLEY, CHIP PARADE, ...). PLAY loads the chosen one and starts it.

- MY SONG is the project slot the FM-1 started on (usually the first). It is
  saved before a demo loads, so it always comes back.
- A loaded demo is kept in the last project slot (16), so changes to it are
  saved too. Loading another demo replaces it. If slot 16 holds a project
  made with another firmware, the kid UI says SLOT 16 BUSY and leaves it alone.
- Demo patterns are four bars long and keep their length; BEAT mode shows and
  edits the first bar. Tracks the kid fills in loop one row of keys as before.

## Versions

`KID_VERSION` in `kid_config.h` goes up with every pull request (0.2, 0.3,
...); the FM-1 shows it on the start screen ("FM1 KIDS 0.2"). CI fails a
pull request that does not change it.

## Play along

After the demo songs, the ENV list has tunes to learn: TWINKLE, MARY LAMB,
BIRTHDAY, ODE TO JOY, HOT BUNS, JINGLE, ROW BOAT and MACDONALD (old folk
tunes, melody only). PLAY starts one: the key to press next lights up, the
screen shows the next note in its colour and a bar shows how far along it is.
Only the right key moves on; any key still plays. At the end it says YAY! and
goes back to normal. HOME stops early. While playing along the keys use DO RE
MI (every white key as printed) and chords are off; the earlier scale and
chords come back afterwards.

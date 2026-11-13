/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 Kids: a simple interface for young children, included by app/screen.c
 * when FM1_KID_UI is 1. It replaces M-VAVE's screen and keeps its entry points
 * (groove_screen_init/control/tap/mmc and the browser preview hooks).
 * The band painter and font are adapted from M-VAVE's screen.c.
 *
 * Controls
 *   SELECT knob      choose an instrument (track)
 *   ALGORITHM knob   change the chosen instrument: 15 to pick from (piano,
 *                    organ, trumpet, flute, violin, choir, bells, xylophone,
 *                    guitar, bass, robot, dream, game, fun, drums)
 *   piano keys       play it; melodic tracks snap to the scale (HAPPY to start)
 *   SEQ              BEAT mode: white keys switch steps on/off,
 *                    black keys pick the drum (or note) to place
 *   HOME             back to PLAY mode
 *   PLAY             start / stop      REC   record what you play
 *   K1..K4           LOUD, BRIGHT, SOUND, SPEED; turn fast for big steps.
 *                    SOUND keeps to versions of the same instrument
 *                    (another trumpet, another bell)
 *   ARP              arpeggiator: tap for UP, DOWN, BOUNCE, RANDOM, OFF;
 *                    hold to make it keep going after the keys are let go;
 *                    hold and turn K1..K4: FAST (1/4..1/32), OCTAVES (1..4),
 *                    LENGTH (gate 25..100%), SWING (0..50%); it follows SPEED
 *   SEL              quiet / unquiet this instrument (SCL in the code)
 *   EDIT             sound panel: K1..K4 = REVERB, ECHO, LONG (how long notes
 *                    ring), ROOM (reverb size, shared); EDIT again: KEYS,
 *                    K1 SCALE (HAPPY, SAD, DO RE MI, SPOOKY, DREAMY, ALL KEYS),
 *                    K2 CHORDS (one key plays a chord); EDIT again closes
 *   ENV              songs: turn SELECT to choose MY SONG or one of 16 demo
 *                    songs, PLAY loads it (sounds and patterns) and plays;
 *                    a demo's changes are kept in the last project slot
 *   LFO              undo              SAVE  save now (it also saves by itself)
 *   FX, then FX      start this instrument over (LFO brings it back)
 *   OCT- / OCT+      lower / higher
 *   hold GLO         parent Bluetooth panel: +PLAY search for MIDI keyboards,
 *                    +SELECT choose, +SAVE connect, +REC disconnect,
 *                    +HOME let a phone or computer connect instead (default)
 * A keyboard on MIDI channel 1 (USB, Bluetooth or DIN) plays the chosen
 * instrument; channels 2-16 keep their usual tracks.
 */
#include "fm1_device.h"
#include "instrument.h"
#include <string.h>

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
enum { OCT_DOWN, OCT_UP, FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC };
#define BUTTON(b) (1U << (b))
enum { KID_PLAY, KID_BEAT };
enum { E_BODY, E_HEAD, E_FOOT, E_ITEMS, E_COUNT = E_ITEMS + 17 };

static const uint16_t BG = RGB(20, 22, 48), INK = RGB(255, 255, 255), SOFT = RGB(150, 160, 200),
                      OFF = RGB(55, 60, 95), OFF4 = RGB(80, 86, 130), GO = RGB(60, 220, 90), STOP = RGB(255, 70, 70);
static const uint16_t kid_colors[8] = {RGB(255, 80, 80), RGB(255, 160, 40), RGB(250, 220, 40), RGB(80, 210, 90),
                                       RGB(40, 200, 220), RGB(70, 120, 255), RGB(170, 90, 240), RGB(255, 100, 190)};
static const uint8_t white_keys[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t black_keys[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
/* Happy keys: every pitch snaps down to C major pentatonic, so nothing clashes. */
/* Scales: the keys snap down to the scale's notes, so every key sounds good.
 * Chords stack every other note of the scale's 7-note family (its parent). */
enum { SCALES = 6 };
static const struct { const char *name; uint16_t notes, parent; } scales[SCALES] = {
    {"HAPPY",    0x295, 0xab5},   /* major pentatonic C D E G A; parent C major */
    {"SAD",      0x4a9, 0x5ad},   /* minor pentatonic C Eb F G Bb; parent C minor */
    {"DO RE MI", 0xab5, 0xab5},   /* major */
    {"SPOOKY",   0x5b3, 0x5b3},   /* C Db E F G Ab Bb */
    {"DREAMY",   0x555, 0x555},   /* whole tone */
    {"ALL KEYS", 0xfff, 0xab5}};  /* every key as it is; chords from C major */
static unsigned scale, chords;     /* EDIT page 2: the keys' scale, one-key chords */
/* Kid instruments: what the picker offers and the tiles show. Several use the
 * DX7 engine with a chosen factory voice; the others are whole engines. */
enum { I_PIANO, I_ORGAN, I_TRUMPET, I_FLUTE, I_VIOLIN, I_CHOIR, I_BELLS, I_XYLO,
       I_GUITAR, I_BASS, I_ROBOT, I_DREAM, I_GAME, I_FUN, I_DRUMS, I_COUNT };
static const char *const kind_name[I_COUNT] = {"PIANO", "ORGAN", "TRUMPET", "FLUTE", "VIOLIN", "CHOIR",
                                               "BELLS", "XYLOPHONE", "GUITAR", "BASS", "ROBOT", "DREAM",
                                               "GAME", "FUN", "DRUMS"};
/* Engine and starting DX7 voice for each (0xff: not DX7). Swap voices freely. */
static const struct { uint8_t engine, program; } kind_sound[I_COUNT] = {
    {GROOVE_DX7, 0},   {GROOVE_DX7, 1},    {GROOVE_DX7, 64},  {GROOVE_DX7, 65},   {GROOVE_DX7, 66},
    {GROOVE_DX7, 67},  {GROOVE_DX7, 120},  {GROOVE_DX7, 117}, {GROOVE_PLUCK, 0xff}, {GROOVE_DX7, 34},
    {GROOVE_DX7, 2},   {GROOVE_DX7, 3},    {GROOVE_PSG, 0xff}, {GROOVE_SAMPLE, 0xff}, {GROOVE_808, 0xff}};
/* A second engine SOUND moves on to: guitar goes pluck -> DX7 guitars,
 * bass goes DX7 basses -> the 303 bass line. */
static const uint8_t kind_other[I_COUNT] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, GROOVE_DX7, GROOVE_303, 0xff, 0xff, 0xff, 0xff, 0xff};
/* Pictures: 16x12 drawn 3x, or 24x18 drawn 2x (same size on screen); the
 * leftmost pixel is the highest used bit. Order follows the kid instruments. */
static const struct { uint8_t w, h; uint32_t rows[18]; } kind_icon[I_COUNT] = {
    /* PIANO */ {16, 12, {0x0000, 0xffff, 0x96b5, 0x96b5, 0x96b5, 0x96b5, 0xddb7, 0xddb7, 0xddb7, 0xffff, 0x0000, 0x0000}},
    /* ORGAN */ {24, 18, {0x003000, 0x01b600, 0x01b600, 0x01b600, 0x0db6c0, 0x0db6c0, 0x0db6c0, 0x6db6d8, 0x6db6d8,
                         0x6db6d8, 0x492490, 0x6db6d8, 0x6db6d8, 0xffffff, 0xffffff, 0xdb6db6, 0xdb6db6, 0xffffff}},
    /* TRUMPET */ {24, 18, {0x000000, 0x000000, 0x03fe00, 0x036c01, 0x036c07, 0x036c1f, 0x036c7f, 0xc36cff, 0xffffff,
                         0xc603ff, 0x06037f, 0x06033f, 0x03ff0f, 0x000003, 0x000000, 0x000000, 0x000000, 0x000000}},
    /* FLUTE */ {24, 18, {0x000000, 0x000000, 0x000006, 0x00001e, 0x00003e, 0x0000d8, 0x0003f0, 0x000ec0, 0x003700,
                         0x00dc00, 0x01f000, 0x06c000, 0x1f8000, 0x6e0000, 0x780000, 0x600000, 0x000000, 0x000000}},
    /* VIOLIN */ {24, 18, {0x006000, 0x006300, 0x006200, 0x006300, 0x006100, 0x01f880, 0x01f8c0, 0x03fc40, 0x03fc20,
                         0x017830, 0x017810, 0x037c08, 0x077e0c, 0x07fe04, 0x03fc02, 0x03fc02, 0x01f800, 0x000000}},
    /* CHOIR */ {24, 18, {0x000000, 0x03c000, 0x0ff000, 0x1ff802, 0x3ffc3a, 0x7ffe3e, 0x7ffe36, 0x73ce32, 0x7ffe36,
                         0x7ffe3e, 0x7c3e3e, 0x7c3e30, 0x7c3ef0, 0x3c3cf0, 0x1e78e0, 0x0ff000, 0x03c000, 0x000000}},
    /* BELLS */ {24, 18, {0x001800, 0x001800, 0x003c00, 0x007e00, 0x00ff00, 0x00ff00, 0x00ff00, 0x00ff00, 0x00ff00,
                         0x01ff80, 0x01ff80, 0x01ff80, 0x03ffc0, 0x07ffe0, 0x0ffff0, 0x003c00, 0x003c00, 0x001800}},
    /* XYLO */ {24, 18, {0x000007, 0x000007, 0x000007, 0x700003, 0x500006, 0x77000c, 0x750018, 0x777030, 0x775060,
                         0x7777c0, 0x777500, 0x777770, 0x777750, 0x777770, 0x777770, 0x555550, 0x777770, 0x000000}},
    /* GUITAR */ {24, 18, {0x000014, 0x00001e, 0x00003c, 0x000028, 0x000060, 0x0000c0, 0x003b00, 0x007f00, 0x01fe00,
              0x07cf00, 0x0f8600, 0x1f8e00, 0x1ffc00, 0x1ff800, 0x1ff000, 0x1ff000, 0x0fe000, 0x07c000}},
    /* BASS */ {16, 12, {0x0030, 0x0038, 0x002c, 0x0026, 0x0022, 0x0020, 0x0020, 0x0020, 0x03e0, 0x07e0, 0x07c0, 0x0380}},
    /* ROBOT */ {24, 18, {0x001800, 0x001800, 0x001800, 0x0ffff0, 0x0ffff0, 0x0ffff0, 0x0ffff0, 0x0e3c70, 0x3e3c7c,
                         0x3e3c7c, 0x3ffffc, 0x3ffffc, 0x3ffffc, 0x0edb70, 0x0edb70, 0x0ffff0, 0x0ffff0, 0x000000}},
    /* DREAM */ {24, 18, {0x000010, 0x000038, 0x0600fe, 0x1c007c, 0x3c0038, 0x38006c, 0x780082, 0x780000, 0x780000,
                         0xf80000, 0x7c0004, 0x7e000e, 0x7f0004, 0x3fc000, 0x3ff820, 0x1ff870, 0x07e020, 0x000000}},
    /* GAME */ {16, 12, {0x1010, 0x0820, 0x1ff0, 0x37d8, 0x7ffc, 0x5ff4, 0x5014, 0x0c60, 0x0000, 0x0000, 0x0000, 0x0000}},
    /* FUN */ {16, 12, {0x0100, 0x0100, 0x0380, 0xfff8, 0x7ff0, 0x3fe0, 0x1fc0, 0x3de0, 0x38e0, 0x6030, 0x0000, 0x0000}},
    /* DRUMS */ {16, 12, {0x8001, 0x4002, 0x2004, 0x1008, 0x3ffc, 0x4002, 0x7ffe, 0x5555, 0x5555, 0x5555, 0x7ffe, 0x0000}}};
/* Five columns per glyph, bit 0 at the top: digits, then A..Z (M-VAVE's font). */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const uint8_t punctuation[][6] = {{'!', 0, 0, 0x5f, 0, 0}, {'-', 8, 8, 8, 8, 8}, {'+', 8, 8, 0x3e, 8, 8},
                                         {'?', 2, 1, 0x51, 9, 6}, {'.', 0, 0x60, 0x60, 0, 0}, {'=', 0x14, 0x14, 0x14, 0x14, 0x14},
                                         {'#', 0x14, 0x7f, 0x14, 0x7f, 0x14}, {'<', 8, 0x14, 0x22, 0x41, 0},
                                         {'>', 0x41, 0x22, 0x14, 8, 0}, {':', 0, 0x36, 0x36, 0, 0},
                                         {'/', 0x60, 0x18, 6, 1, 0}, {'%', 0x63, 0x13, 8, 0x64, 0x63}};

/* ---------- state ---------- */
static unsigned track, mode, drum, recording, playing, play_step, retry_reset;
static unsigned beat_note = 60;
static int octave;
static uint32_t previous_buttons, previous_keys, notice_until, confirm_until;
static uint32_t booted_at, input_at, save_tried_at, warned_at, picked_at;
static const char *notice;
static uint16_t notice_color;
static uint8_t key_note[27], key_track[27], key_chord[27][2];   /* key_chord: extra chord notes */
static uint32_t shown[E_COUNT], led_keys, led_buttons;
static struct groove_view view;   /* the current track, refreshed every poll */
static struct fm1_ble_status ble;
static unsigned ble_pick, parent;  /* parent: GLO held, the Bluetooth panel is open */
static int ble_result;
static unsigned battery_avg, battery_level, on_usb, blink;   /* level 0..3 */
static unsigned pick_kind, picking;   /* ALGORITHM: the instrument being chosen */
static unsigned tidy_pending;         /* boot: give empty tracks their kid sound */
static unsigned songs_panel, song_pick; /* ENV: MY SONG and the 16 demo songs */
static unsigned my_slot;              /* the kid's own song; demos are kept in DEMO_SLOT */
enum { DEMO_SLOT = GROOVE_PROJECT_SLOTS - 1, SONGS = 1 + GROOVE_FACTORY_PROJECTS };
static unsigned sound_panel;          /* EDIT: REVERB, ECHO, LONG and ROOM on K1..K4 */
static int8_t knob_dir[7];            /* knob acceleration: turning fast takes bigger steps */
static uint8_t knob_speed[7];
static uint32_t knob_at[7];
static uint32_t arp_down_at;          /* ARP: tap cycles the style, a long hold toggles latch */
static unsigned arp_long, arp_turned, arp_panel;   /* arp_panel: ARP held, its knobs shown */
static const char *const arp_names[4] = {"ARP UP", "ARP DOWN", "ARP BOUNCE", "ARP RANDOM"};
static volatile unsigned keyboard_channel;   /* read by the MIDI task */
static uint8_t keyboard_held[128];            /* MIDI task only: channel+1 per held note */

/* ---------- band painter (adapted from screen.c) ---------- */
enum { PAINT_ROWS = 16, PAINT_BANDS = FM1_SCREEN_HEIGHT / PAINT_ROWS };
static uint16_t paint_pixels[FM1_SCREEN_WIDTH * PAINT_ROWS];
static uint8_t paint_left[PAINT_BANDS], paint_right[PAINT_BANDS];
static unsigned paint_top, paint_next, paint_x, paint_end;
static void paint_mark(unsigned x, unsigned y, unsigned w, unsigned h)
{
    for (unsigned b = y / PAINT_ROWS; b <= (y + h - 1) / PAINT_ROWS && b < PAINT_BANDS; ++b) {
        if (x < paint_left[b]) paint_left[b] = (uint8_t)x;
        if (x + w > paint_right[b]) paint_right[b] = (uint8_t)(x + w);
    }
}
static __attribute__((noinline)) void fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color)
{
    if (x >= paint_end || x + w <= paint_x || y >= paint_top + PAINT_ROWS || y + h <= paint_top) return;
    unsigned right = x + w < paint_end ? x + w : paint_end;
    if (x < paint_x) x = paint_x;
    unsigned first = y < paint_top ? paint_top : y;
    unsigned end = y + h < paint_top + PAINT_ROWS ? y + h : paint_top + PAINT_ROWS;
    unsigned stride = paint_end - paint_x;
    for (unsigned row = first; row < end; ++row)
        for (unsigned col = x; col < right; ++col)
            paint_pixels[(row - paint_top) * stride + col - paint_x] = color;
}
static void frame(unsigned x, unsigned y, unsigned w, unsigned h, unsigned t, uint16_t color)
{
    fill(x, y, w, t, color); fill(x, y + h - t, w, t, color);
    fill(x, y, t, h, color); fill(x + w - t, y, t, h, color);
}
static const uint8_t *glyph(char c)
{
    if (c >= '0' && c <= '9') return font[c - '0'];
    if (c >= 'A' && c <= 'Z') return font[10 + c - 'A'];
    if (c >= 'a' && c <= 'z') return font[10 + c - 'a'];
    for (unsigned i = 0; i < sizeof punctuation / sizeof punctuation[0]; ++i)
        if (punctuation[i][0] == (uint8_t)c) return punctuation[i] + 1;
    return NULL;
}
/* Text without a background box: only the lit pixels are painted. */
static void text(unsigned x, unsigned y, const char *s, unsigned scale, uint16_t color)
{
    if (!s || y >= paint_top + PAINT_ROWS || y + 7U * scale <= paint_top) return;
    for (; *s; ++s, x += 6U * scale) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        for (unsigned col = 0; col < 5; ++col)
            for (unsigned row = 0; row < 7; ++row)
                if (g[col] & (1U << row)) fill(x + col * scale, y + row * scale, scale, scale, color);
    }
}
static unsigned text_width(const char *s, unsigned scale) { return (unsigned)strlen(s) * 6U * scale - scale; }
static void text_center(unsigned x, unsigned w, unsigned y, const char *s, unsigned scale, uint16_t color)
{
    unsigned tw = text_width(s, scale);
    text(x + (w > tw ? (w - tw) / 2 : 0), y, s, scale, color);
}
/* An instrument picture, 48x36 on screen (or 32x24 with small). */
static void picture(unsigned x, unsigned y, unsigned k, unsigned small, uint16_t color)
{
    unsigned w = kind_icon[k].w, h = kind_icon[k].h;
    unsigned scale = (w == 24 ? 2U : 3U) - (small ? 1U : 0U);
    if (y >= paint_top + PAINT_ROWS || y + h * scale <= paint_top) return;
    for (unsigned r = 0; r < h; ++r)
        for (unsigned c = 0; c < w; ++c)
            if (kind_icon[k].rows[r] & (1UL << (w - 1U - c))) fill(x + c * scale, y + r * scale, scale, scale, color);
}
static void number(char out[8], unsigned value)
{
    char tmp[8]; unsigned n = 0;
    do { tmp[n++] = (char)('0' + value % 10U); value /= 10U; } while (value && n < 7);
    for (unsigned i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}
static uint16_t dim(uint16_t c) { return (uint16_t)((c >> 1) & 0x7bef); }

/* ---------- helpers ---------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* Milliseconds since t, signed: stored times are "| 1" (0 = never), so they
 * can sit 1 ms ahead of now. */
static int32_t since(uint32_t now, uint32_t t) { return (int32_t)(now - t); }
static int post(uint8_t command, uint8_t a, uint8_t b, unsigned bytes)
{
    const uint8_t message[3] = {command, a, b};
    if (!groove_midi_local(message, bytes)) return 1;
    retry_reset = 1;
    return 0;
}
static void say(const char *s, uint16_t color, uint32_t now)
{
    notice = s; notice_color = color; notice_until = now + 1500U;
}
static unsigned drum_track(unsigned t) { return groove_pad_count(t) != 0; }
static unsigned kind(unsigned t) { unsigned k = groove_source_type(t); return k < GROOVE_SOURCES ? k : 0; }
/* Which kid instrument a DX7 voice sounds like (the factory bank repeats in fours). */
static unsigned voice_look(unsigned p)
{
    static const uint8_t low[4] = {I_PIANO, I_ORGAN, I_ROBOT, I_DREAM}, high[4] = {I_TRUMPET, I_FLUTE, I_VIOLIN, I_CHOIR};
    /* 96..127, the odd ones: bells and metal, tuned wood and drums, dreamy, the rest robots. */
    static const uint8_t odd[32] = {
        I_ROBOT, I_ROBOT, I_XYLO,  I_DREAM, I_BELLS, I_BELLS, I_ROBOT, I_ROBOT,   /*  96 BOWOAN .. 103 MONO M */
        I_BELLS, I_XYLO,  I_BELLS, I_ROBOT, I_BELLS, I_XYLO,  I_ROBOT, I_DREAM,   /* 104 TIBET .. 111 DESCENT */
        I_BELLS, I_XYLO,  I_XYLO,  I_ROBOT, I_BELLS, I_XYLO,  I_BELLS, I_ROBOT,   /* 112 CLANG BELL .. 119 */
        I_BELLS, I_ROBOT, I_XYLO,  I_ROBOT, I_DREAM, I_XYLO,  I_ROBOT, I_ROBOT};  /* 120 TUB BELLS .. 127 */
    if (p < 32) return low[p % 4U];
    if (p < 64) return p % 4U < 2 ? I_GUITAR : I_BASS;
    if (p < 96) return high[p % 4U];
    return odd[(p - 96U) & 31U];
}
/* The kid instrument a track is playing now. */
static unsigned look(unsigned t)
{
    static const uint8_t by_engine[GROOVE_SOURCES] = {I_PIANO, I_BASS, I_DRUMS, I_GAME, I_GUITAR, I_FUN};
    unsigned k = kind(t);
    if (k != GROOVE_DX7) return by_engine[k];
    struct groove_view v;
    groove_get_view(t, 0, &v);
    return voice_look(v.program);
}
static unsigned shown_kind(unsigned t) { return picking && t == track ? pick_kind : look(t); }
static unsigned program_of(unsigned t)
{
    struct groove_view v;
    groove_get_view(t, 0, &v);
    return v.program;
}
static void release_keys(void);
static unsigned chord_notes(unsigned root, uint8_t out[3]);
static unsigned current_song(void);
static void draw_songs(void);
static int sound_values(int out[4]);
/* The DX7 voice of instrument i nearest p, searching one way (0x100: none). */
static unsigned dx7_voice(unsigned i, int p, int dir)
{
    for (; p >= 0 && p < 128; p += dir)
        if (voice_look((unsigned)p) == i) return (unsigned)p;
    return 0x100;
}
static void set_program(unsigned p) { groove_control_edit(track, (int)p - (int)program_of(track), 0, 0); }
/* Turn this track into kid instrument i: its engine, then its starting voice. */
static int set_look(unsigned i)
{
    if (kind(track) != kind_sound[i].engine && groove_source_set(track, kind_sound[i].engine)) return 1;
    if (kind_sound[i].program != 0xff) set_program(kind_sound[i].program);
    return 0;
}
/* SOUND: the next (or previous) version of the same instrument. GUITAR and BASS
 * have two engines; SOUND runs through one, then the other. */
static void next_voice(int steps)
{
    int dir = steps > 0 ? 1 : -1;
    for (; steps; steps -= dir) {
        unsigned i = look(track), k = kind(track), before = program_of(track);
        unsigned other = kind_other[i];
        if (k == GROOVE_DX7) {
            unsigned p = dx7_voice(i, (int)before + dir, dir);
            if (p > 127 && other != 0xff) {                /* past the last DX7 version */
                release_keys();
                if (groove_source_set(track, other)) return;
                continue;
            }
            if (p > 127) p = dx7_voice(i, dir > 0 ? 0 : 127, dir);   /* around again */
            if (p <= 127) set_program(p);
        } else {
            groove_control_edit(track, dir, 0, 0);
            if (program_of(track) != before || other != GROOVE_DX7) continue;
            release_keys();                                /* past the engine's last sound */
            if (groove_source_set(track, GROOVE_DX7)) return;
            unsigned p = dx7_voice(i, dir > 0 ? 0 : 127, dir);
            if (p <= 127) set_program(p);
        }
    }
}
/* Let go of one key's note and its chord notes; 0 when the queue is full. */
static int key_off(unsigned key)
{
    uint8_t status = (uint8_t)(0x80 + groove_midi_input_channel(key_track[key]));
    for (unsigned i = 0; i < 2; ++i)
        if (key_chord[key][i] != 0xff) {
            if (!post(status, key_chord[key][i], 0, 3)) return 0;
            key_chord[key][i] = 0xff;
        }
    if (!post(status, key_note[key], 0, 3)) return 0;
    key_note[key] = 0xff;
    return 1;
}
static void release_keys(void)
{
    for (unsigned key = 0; key < 27; ++key)
        if (key_note[key] != 0xff && !key_off(key)) return;
}
static unsigned step_on(unsigned t, unsigned step)
{
    struct groove_view v;
    groove_get_view(t, step, &v);
    return drum_track(t) ? (v.step.drums >> drum) & 1U : v.step.velocity != 0;
}
static void toggle_step(unsigned step)
{
    unsigned on = step_on(track, step);
    unsigned note = drum_track(track) ? groove_pad_note(track, drum) : beat_note;
    uint8_t notes[3];
    if (!on && chords && !drum_track(track)) groove_sequence_chord(track, step, notes, chord_notes(note, notes), 100);
    else groove_sequence_edit(track, step, note, on ? 0 : 100);
}
/* The scale note at or below a pitch. */
static unsigned snap(unsigned note, uint16_t set)
{
    unsigned pc = note % 12U, base = note - pc;
    while (!((set >> pc) & 1U)) { if (!pc) { pc = 12; base -= 12U; } --pc; }
    return base + pc;
}
/* The n-th scale note from middle C up (for BEAT's black keys). */
static unsigned scale_note(unsigned n)
{
    unsigned note = 60;
    for (;; ++note)
        if ((scales[scale].notes >> (note % 12U)) & 1U) { if (!n) return note; --n; }
}
/* A chord on a root: the next-but-one and the one after that in the parent scale. */
static unsigned chord_notes(unsigned root, uint8_t out[3])
{
    uint16_t set = scales[scale].parent;
    unsigned n = 0, note = root;
    out[n++] = (uint8_t)root;
    for (unsigned skip = 0; n < 3 && note < 127;) {
        ++note;
        if ((set >> (note % 12U)) & 1U && ++skip % 2U == 0) out[n++] = (uint8_t)note;
    }
    return n;
}
/* What a key plays on this track: drums keep their pad layout; tunes snap to the scale. */
static unsigned key_pitch(unsigned key)
{
    unsigned note = groove_key_note(track, key, octave);
    if (note >= 128 || drum_track(track)) return note;
    return snap(note, scales[scale].notes);
}
/* BRIGHT: the setting that brightens each engine most; DX7 uses feedback. */
struct bright { unsigned fm, part, param, max; };
static int bright_target(unsigned t, struct bright *b)
{
    b->fm = kind(t) == GROOVE_DX7;
    b->part = drum_track(t) ? drum : 0;
    if (b->fm) { b->param = 135; b->max = 7; return 1; }
    static const char *const names[] = {"Cutoff", "BRIGHT", "Tone", "Vel LPF", "NOISE", "Drive"};
    unsigned n = groove_tone_count(t, b->part);
    for (unsigned w = 0; w < sizeof names / sizeof names[0]; ++w)
        for (unsigned i = 0; i < n; ++i) {
            const fm1_param_t *p = groove_tone_parameter(t, b->part, i);
            if (p && p->max && !strcmp(p->name, names[w])) { b->param = i; b->max = p->max; return 1; }
        }
    return 0;
}
static int bright_value(unsigned t)   /* 0..100, or -1 when the engine has none */
{
    struct bright b;
    if (!bright_target(t, &b)) return -1;
    if (b.fm) {
        uint8_t patch[156];
        return groove_fm_copy(t, patch, NULL) ? -1 : (int)(patch[135] & 7U) * 100 / 7;
    }
    uint8_t v[GROOVE_TONE_PARAMS];
    return groove_tone_copy(t, b.part, v) <= (int)b.param ? -1 : (int)v[b.param] * 100 / (int)b.max;  /* returns the count */
}
static void bright_edit(int delta)
{
    struct bright b;
    if (!bright_target(track, &b)) return;
    if (b.fm) groove_fm_edit(track, b.param, delta);
    else groove_tone_edit(track, b.part, b.param, delta);
}
/* LONG: how long notes ring after the key is let go. DX7: every operator's
 * release rate; other engines: their release/decay/length setting. */
enum { LONG_SLOWEST = 30 };   /* DX7 release rate: about a few seconds at most */
static int long_target(unsigned t, struct bright *b)
{
    b->fm = kind(t) == GROOVE_DX7;
    b->part = drum_track(t) ? drum : 0;
    if (b->fm) { b->param = 3; b->max = 99; return 1; }
    static const char *const names[] = {"RELEASE", "Release", "DECAY", "Decay", "Length"};
    unsigned n = groove_tone_count(t, b->part);
    for (unsigned w = 0; w < sizeof names / sizeof names[0]; ++w)
        for (unsigned i = 0; i < n; ++i) {
            const fm1_param_t *p = groove_tone_parameter(t, b->part, i);
            if (p && p->max && !strcmp(p->name, names[w])) { b->param = i; b->max = p->max; return 1; }
        }
    return 0;
}
static int long_value(unsigned t)   /* 0..100, or -1 when the engine has none */
{
    struct bright b;
    if (!long_target(t, &b)) return -1;
    if (b.fm) {
        uint8_t patch[156];
        if (groove_fm_copy(t, patch, NULL)) return -1;
        unsigned sum = 0;
        for (unsigned op = 0; op < 6; ++op) {
            unsigned r = patch[op * 21U + 3U];
            sum += r > 99 ? 99U : r < LONG_SLOWEST ? LONG_SLOWEST : r;
        }
        return (int)((99U * 6U - sum) * 100U / ((99U - LONG_SLOWEST) * 6U));
    }
    uint8_t v[GROOVE_TONE_PARAMS];
    return groove_tone_copy(t, b.part, v) <= (int)b.param ? -1 : (int)v[b.param] * 100 / (int)b.max;
}
static void long_edit(int delta)
{
    struct bright b;
    if (!long_target(track, &b)) return;
    if (!b.fm) { groove_tone_edit(track, b.part, b.param, delta); return; }
    uint8_t patch[156];
    if (groove_fm_copy(track, patch, NULL)) return;
    for (unsigned op = 0; op < 6; ++op) {          /* a slower release rate rings longer */
        int r = patch[op * 21U + 3U] > 99 ? 99 : patch[op * 21U + 3U];
        int next = clampi(r - delta, r < LONG_SLOWEST ? r : LONG_SLOWEST, 99);  /* never a note that never ends */
        if (next != r) groove_fm_edit(track, op * 21U + 3U, next - r);
    }
}
/* Knob acceleration (as in M-VAVE's UI): a fast turn in one direction grows
 * the step up to 4x; a pause or a change of direction is fine again. */
static void knobs_read(const struct fm1_controls *c, uint32_t now)
{
    for (unsigned i = 0; i < 7; ++i) {
        int dir = (c->encoders[i] > 0) - (c->encoders[i] < 0);
        if (!dir) continue;
        knob_speed[i] = dir == knob_dir[i] && now - knob_at[i] <= 40U ? (uint8_t)clampi(knob_speed[i] + 1, 1, 4) : 1;
        knob_dir[i] = (int8_t)dir;
        knob_at[i] = now;
    }
}
static int fast(const struct fm1_controls *c, unsigned i)
{
    int v = c->encoders[i], amount = v < 0 ? -v : v;
    int gain = amount < 2 ? 1 : amount < 4 ? 2 : amount == 4 ? 3 : 4;
    if (gain < knob_speed[i]) gain = knob_speed[i];
    return v * gain;
}
static void sound_name(unsigned t, char out[12])
{
    static const char *const chips[4] = {"YM", "AY", "NOISE", "MIX"};
    struct groove_view v;
    groove_get_view(t, 0, &v);
    unsigned k = kind(t);
    out[0] = 0;
    if (k == GROOVE_DX7) {
        uint8_t patch[156];
        if (!groove_fm_copy(t, patch, NULL)) {
            memcpy(out, patch + 145, 10);
            out[10] = 0;
            for (int i = 9; i >= 0 && out[i] == ' '; --i) out[i] = 0;
        }
    } else if (k == GROOVE_303) strcpy(out, v.program ? "SQUARE" : "SAW");
    else if (k == GROOVE_PSG) strcpy(out, chips[v.program & 3U]);
    if (!out[0]) number(out, v.program + 1U);
}
/* Battery: the stock firmware's levels (raw ADC under 531, 531.., 561.., 591..). */
static void battery_poll(unsigned raw, uint32_t now)
{
    static const uint16_t step[3] = {531, 561, 591};
    on_usb = fm1_device_usb_connected() > 0;
    battery_avg = battery_avg ? (battery_avg * 7U + raw) / 8U : raw;
    unsigned level = battery_level;
    while (level < 3 && battery_avg >= step[level] + 4U) ++level;
    while (level > 0 && battery_avg + 4U < step[level - 1]) --level;
    battery_level = level;
    blink = !on_usb && !level ? (now / 500U) & 1U : 0;
    if (!on_usb && !level && (!warned_at || since(now, warned_at) >= 60000)) {
        warned_at = now | 1U;
        say("CHARGE ME!", STOP, now);
    }
}
/* Save by itself once a change has rested: stopped, untouched for 8 s (or the
 * battery is nearly empty). Saving briefly silences audio, so never while playing. */
static void autosave(uint32_t now)
{
    if (!groove_project_dirty() || playing || recording || now - booted_at < 5000U) return;
    if (now - input_at < 8000U && !(battery_level == 0 && !on_usb)) return;
    if (save_tried_at && since(now, save_tried_at) < 30000) return;
    save_tried_at = now | 1U;
    if (!groove_project_save_slot(groove_project_slot())) say("SAVED", GO, now);
}
/* MIDI task: a keyboard on channel 1 plays the chosen instrument. Each note
 * ends on the track it started on, even if the choice changes meanwhile. */
int groove_kid_route(const uint8_t *m, size_t n, uint8_t out[3])
{
    unsigned status = m[0] & 0xf0U;
    if (n < 2 || m[0] < 0x80U || status == 0xf0U || (m[0] & 15U)) return 0;
    unsigned ch = keyboard_channel & 15U, note = m[1] & 127U;
    if (status == 0x80U || status == 0x90U || status == 0xa0U) {
        if (status == 0x90U && n > 2 && m[2]) keyboard_held[note] = (uint8_t)(ch + 1U);
        else if (keyboard_held[note]) {
            ch = keyboard_held[note] - 1U;
            if (status != 0xa0U) keyboard_held[note] = 0;
        }
    }
    if (!ch) return 0;
    out[0] = (uint8_t)(status | ch);
    out[1] = m[1];
    out[2] = n > 2 ? m[2] : 0;
    return 1;
}

/* ---------- screen elements ---------- */
/* Each element has a rectangle and a signature of everything it shows. Only
 * elements whose signature changed are repainted. */
static int rect(unsigned e, unsigned *x, unsigned *y, unsigned *w, unsigned *h)
{
    if (e == E_HEAD) { *x = 0; *y = 0; *w = 240; *h = 40; return 1; }
    if (e == E_BODY) { *x = 0; *y = 40; *w = 240; *h = 150; return 1; }
    if (e == E_FOOT) { *x = 0; *y = 190; *w = 240; *h = 50; return 1; }
    unsigned i = e - E_ITEMS;
    if (mode == KID_PLAY) {
        if (i >= 8) return 0;
        *x = 4 + (i % 4U) * 59U; *y = 46 + (i / 4U) * 72U; *w = 55; *h = 68; return 1;
    }
    if (i == 16) { *x = 0; *y = 42; *w = 240; *h = 40; return 1; }   /* what the black keys picked */
    *x = 4 + (i % 8U) * 29U; *y = 88 + (i / 8U) * 50U; *w = 27; *h = 44; return 1;
}
static uint32_t mix(uint32_t s, uint32_t v) { return s * 131U + v; }
static uint32_t signature(unsigned e)
{
    uint32_t base = mode | track << 1;
    if (e == E_BODY) return mode;
    if (e == E_HEAD)
        return mix(mix(mix(mix(base | playing << 4 | recording << 5 | (uint32_t)(octave + 4) << 6, view.tempo),
                           (ble.state >= FM1_BLE_CONNECTED && ble.state <= FM1_BLE_READY)),
                       battery_level | on_usb << 2 | blink << 3), 7);
    if (e == E_FOOT) {
        if (parent) return 0x40000000U | ble.state | ble.role << 4 | ble.count << 6 | ble_pick << 10 |
                           (uint32_t)(ble_result & 7) << 14 | (uint32_t)ble.error << 17;
        if (confirm_until) return 0x20000000U | track;
        if (arp_panel)
            return 0x10000000U | track | groove_arp_enabled(track) << 3 | groove_arp_setting(track, 0) << 4 |
                   groove_arp_setting(track, 1) << 6 | groove_arp_setting(track, 2) << 8 |
                   groove_arp_setting(track, 3) << 10 | groove_arp_setting(track, 4) << 12 |
                   groove_arp_setting(track, 5) << 18;
        if (notice) return 0x80000000U | ((uint32_t)(uintptr_t)notice & 0x3fffffffU);
        if (songs_panel)
            return 0x70000000U | song_pick << 8 | current_song() << 16 | playing;
        if (sound_panel == 2) return 0x58000000U | scale << 4 | chords;
        if (sound_panel) {
            int v[4];
            sound_values(v);
            return (mix(mix(mix(mix(0x51U + track, (uint32_t)v[0]), (uint32_t)v[1]), (uint32_t)(v[2] + 1)), (uint32_t)v[3])
                    & 0x0fffffffU) | 0x60000000U;
        }
        uint32_t s = mix(mix(mix(base, view.volume), view.program), view.tempo);
        return mix(mix(s, (uint32_t)(bright_value(track) + 1)), kind(track)) & 0x1fffffffU;
    }
    unsigned i = e - E_ITEMS;
    if (mode == KID_PLAY) {
        if (i >= 8) return 0;
        struct groove_view v;
        groove_get_view(i, 0, &v);
        unsigned sounding = 0;
        for (unsigned k = 0; k < 27; ++k) sounding |= key_note[k] != 0xff && key_track[k] == i;
        return (i == track) | (uint32_t)v.muted << 4 | sounding << 5 | 1U << 6 |
               (uint32_t)(picking && i == track) << 7 | groove_arp_enabled(i) << 8 | groove_arp_setting(i, 5) << 9 |
               shown_kind(i) << 10;
    }
    if (i == 16) return base | drum << 4 | beat_note << 8 | look(track) << 16;
    struct groove_view v;
    groove_get_view(track, i, &v);
    uint32_t s = mix(mix(mix(base, v.step.drums), v.step.velocity ? v.step.note + 1U : 0), drum);
    return mix(s, playing && play_step == i) | 1U;
}
static void draw_battery(void)
{
    uint16_t c = on_usb ? GO : battery_level >= 2 ? GO : battery_level ? kid_colors[2] : STOP;
    if (blink) return;                                    /* nearly empty: it flashes */
    frame(88, 14, 20, 12, 2, c);
    fill(108, 17, 2, 6, c);
    unsigned bars = on_usb ? 3 : battery_level;
    for (unsigned b = 0; b < bars; ++b) fill(91 + b * 5U, 17, 4, 6, c);
    if (on_usb) text(95, 30, "+", 1, GO);
}
static void draw_head(void)
{
    fill(0, 0, 240, 40, BG);
    text(8, 9, recording ? "REC" : mode == KID_BEAT ? "BEAT" : "PLAY", 3, recording ? STOP : INK);
    draw_battery();
    /* Speed: one to five dots */
    unsigned dots = (unsigned)clampi(((int)view.tempo - 50) / 30 + 1, 1, 5);
    for (unsigned d = 0; d < 5; ++d) fill(120 + d * 13U, 16, 9, 9, d < dots ? kid_colors[1] : OFF);
    if (octave) text(124, 30, octave > 0 ? "HIGH" : "LOW", 1, SOFT);
    if (playing) {                                     /* a green play triangle */
        for (unsigned r = 0; r < 24; ++r) fill(206, 8 + r, r < 12 ? r + 1 : 24 - r, 1, GO);
    } else fill(204, 9, 22, 22, SOFT);                 /* a stop square */
    if (recording) fill(190, 15, 10, 10, STOP);
    if (ble.state >= FM1_BLE_CONNECTED && ble.state <= FM1_BLE_READY) text(172, 30, "BT", 1, kid_colors[5]);
}
/* Parent panel: Bluetooth MIDI. */
static void draw_parent(void)
{
    static const char *const states[] = {"OFF", "READY", "WAITING", "SEARCHING", "CONNECTING", "CHECKING",
                                         "CONNECTED", "CONNECTED", "LEAVING"};
    text(4, 194, "BLUETOOTH", 1, kid_colors[5]);
    text(64, 194, ble.state < 9 ? states[ble.state] : "?", 1, INK);
    struct fm1_bluetooth_status radio;
    fm1_bluetooth_get_status(&radio);
    if (radio.audio_connected) text(190, 194, "AUDIO", 1, GO);
    text(4, 206, ble.role == FM1_BLE_CENTRAL ? "FM-1 CONNECTS TO A KEYBOARD" : "A PHONE OR COMPUTER CONNECTS", 1, SOFT);
    struct fm1_ble_device d;
    if (ble.count && ble_pick < ble.count && !fm1_ble_get_device(ble_pick, &d)) {
        d.name[24] = 0;
        text(4, 218, d.name, 1, INK);
    } else text(4, 218, ble_result ? "TRY AGAIN" : "FM1 KIDS " KID_VERSION, 1, ble_result ? STOP : OFF4);
    text(4, 230, "PLAY FIND  SAVE JOIN  REC END", 1, OFF4);
}
/* While ARP is held: the arpeggiator's timing on K1..K4. */
static void draw_arp(void)
{
    static const char *const labels[4] = {"FAST", "OCTAVES", "LENGTH", "SWING"};
    static const char *const rates[4] = {"1/4", "1/8", "1/16", "1/32"};
    uint16_t a = kid_colors[2];
    text(4, 193, groove_arp_enabled(track) ? arp_names[groove_arp_setting(track, 1) & 3U] : "ARP OFF", 1, a);
    if (groove_arp_setting(track, 5)) text(100, 193, "HOLD", 1, a);
    text(150, 193, "TAP ARP: STYLE", 1, OFF4);
    unsigned drums = drum_track(track);
    for (unsigned k = 0; k < 4; ++k) {
        unsigned x = k * 60U, v = groove_arp_setting(track, k ? k + 1U : 0U);
        char value[8];
        text_center(x, 60, 205, labels[k], 1, SOFT);
        if (drums && (k == 1 || k == 2)) { text_center(x, 60, 218, "-", 2, OFF4); continue; }
        if (k == 0) strcpy(value, rates[v & 3U]);
        else if (k == 1) number(value, v + 1U);
        else if (k == 2) { number(value, (v + 1U) * 25U); strcat(value, "%"); }
        else { number(value, v); strcat(value, "%"); }
        text_center(x, 60, 218, value, 2, INK);
    }
}
/* EDIT: the sound panel, REVERB, ECHO, LONG and ROOM on K1..K4. */
static int sound_values(int out[4])
{
    uint8_t sends[2], fx[4];
    groove_sends_get(track, sends);
    groove_effects_get(fx);
    out[0] = sends[0] * 100 / 15;
    out[1] = sends[1] * 100 / 15;
    out[2] = long_value(track);
    out[3] = fx[0] > 100 ? 100 : fx[0];
    return 0;
}
static void draw_sound(void)
{
    static const char *const labels[4] = {"REVERB", "ECHO", "LONG", "ROOM"};
    int v[4];
    sound_values(v);
    text(4, 193, "SOUND EDIT", 1, kid_colors[4]);
    text(144, 193, "EDIT: KEYS", 1, OFF4);
    for (unsigned k = 0; k < 4; ++k) {
        unsigned x = k * 60U;
        text_center(x, 60, 205, labels[k], 1, SOFT);
        if (v[k] < 0) { text_center(x, 60, 218, "-", 2, OFF4); continue; }
        fill(x + 8, 220, 44, 10, OFF);
        fill(x + 8, 220, (unsigned)v[k] * 44U / 100U, 10, kid_colors[k * 2U % 8U]);
    }
}
/* EDIT twice: SCALE on K1, CHORDS on K2. */
static void draw_keys(void)
{
    text(4, 193, "KEYS", 1, kid_colors[2]);
    text(150, 193, "EDIT: DONE", 1, OFF4);
    text_center(0, 120, 205, "SCALE", 1, SOFT);
    text_center(0, 120, 217, scales[scale].name, 2, kid_colors[scale]);
    text_center(120, 120, 205, "CHORDS", 1, SOFT);
    text_center(120, 120, 217, chords ? "ON" : "OFF", 2, chords ? GO : OFF4);
}
static void draw_foot(void)
{
    fill(0, 190, 240, 50, BG);
    if (parent) { draw_parent(); return; }
    if (confirm_until) {
        text_center(0, 240, 196, "START OVER?", 3, STOP);
        text_center(0, 240, 224, "FX = YES   LFO = UNDO LATER", 1, SOFT);
        return;
    }
    if (arp_panel) { draw_arp(); return; }
    if (notice) { text_center(0, 240, 204, notice, 3, notice_color); return; }
    if (songs_panel) { draw_songs(); return; }
    if (sound_panel == 2) { draw_keys(); return; }
    if (sound_panel) { draw_sound(); return; }
    static const char *const labels[4] = {"LOUD", "BRIGHT", "SOUND", "SPEED"};
    for (unsigned k = 0; k < 4; ++k) {
        unsigned x = k * 60U;
        fill(x + 4, 192, 52, 3, kid_colors[(k * 2U + 1U) % 8U]);
        text_center(x, 60, 200, labels[k], 1, SOFT);
        int bar = k == 0 ? (int)view.volume * 100 / 127 : k == 1 ? bright_value(track) : -2;
        if (bar >= 0) {
            fill(x + 8, 216, 44, 10, OFF);
            fill(x + 8, 216, (unsigned)bar * 44U / 100U, 10, kid_colors[k ? 3 : 1]);
        } else if (bar == -1) text_center(x, 60, 214, "-", 2, OFF4);
        char value[12];
        if (k == 2) {
            sound_name(track, value);
            unsigned big = strlen(value) <= 4;
            text_center(x, 60, big ? 214 : 218, value, big ? 2 : 1, INK);
        } else if (k == 3) {
            number(value, view.tempo);
            text_center(x, 60, 214, value, 2, INK);
        }
    }
}
static void draw_tile(unsigned i, unsigned x, unsigned y, unsigned w, unsigned h)
{
    struct groove_view v;
    groove_get_view(i, 0, &v);
    uint16_t c = kid_colors[i];
    unsigned selected = i == track;
    fill(x, y, w, h, v.muted ? OFF : selected ? c : dim(c));
    if (selected) frame(x, y, w, h, 3, picking ? kid_colors[2] : INK);
    picture(x + 4, y + 6, shown_kind(i), 0, v.muted ? OFF4 : selected ? INK : SOFT);
    text_center(x, w, y + h - 16, v.muted ? "QUIET" : kind_name[shown_kind(i)], 1,
                v.muted ? SOFT : selected ? BG : INK);
    for (unsigned k = 0; k < 27; ++k)
        if (key_note[k] != 0xff && key_track[k] == i) { fill(x + w - 12, y + 6, 7, 7, INK); break; }
    if (groove_arp_enabled(i)) {                       /* arpeggiator: a little staircase */
        uint16_t b = groove_arp_setting(i, 5) ? kid_colors[2] : selected ? BG : INK;
        for (unsigned s = 0; s < 3; ++s) fill(x + w - 16 + s * 4U, y + h - 24 - s * 4U, 3, 3 + s * 4U, b);
    }
}
static void draw_pick(void)
{
    fill(0, 42, 240, 40, BG);
    uint16_t c = kid_colors[track];
    picture(4, 46, look(track), 1, c);
    const char *name = drum_track(track) ? groove_pad_name(track, drum) : NULL;
    char tune[8] = "NOTE ";
    if (!name) {
        static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        strcpy(tune + 5, names[beat_note % 12U]);
        name = tune;
    }
    text(44, 50, name, 3, c);
    text(44, 73, "BLACK KEYS: PICK  WHITE: STEPS", 1, OFF4);
}
/* Melody notes take the colour of their pitch and sit higher when higher. */
static const uint16_t note_colors[12] = {RGB(255, 80, 80), RGB(255, 80, 80), RGB(255, 160, 40), RGB(255, 160, 40),
                                         RGB(250, 220, 40), RGB(250, 220, 40), RGB(80, 210, 90), RGB(80, 210, 90),
                                         RGB(40, 200, 220), RGB(70, 120, 255), RGB(70, 120, 255), RGB(170, 90, 240)};
static void draw_cell(unsigned i, unsigned x, unsigned y, unsigned w, unsigned h)
{
    struct groove_view v;
    groove_get_view(track, i, &v);
    uint16_t off = i % 4U ? OFF : OFF4;
    if (drum_track(track)) {
        unsigned on = (v.step.drums >> drum) & 1U;
        fill(x, y, w, h, on ? kid_colors[track] : off);
        unsigned n = groove_pad_count(track) < 12U ? groove_pad_count(track) : 12U;
        for (unsigned d = 0; d < n; ++d)                 /* the rest of the kit: a dot per drum */
            if (d != drum && ((v.step.drums >> d) & 1U))
                fill(x + 3 + (d % 3U) * 8U, y + 4 + (d / 3U) * 10U, 5, 5, on ? BG : SOFT);
    } else if (v.step.velocity) {
        fill(x, y, w, h, note_colors[v.step.note % 12U]);
        unsigned lift = (unsigned)clampi((int)v.step.note - 48, 0, 36);   /* C3..C6 */
        fill(x + 3, y + h - 8 - lift * (h - 12) / 36U, w - 6, 4, INK);
    } else fill(x, y, w, h, off);
    if (playing && play_step == i) frame(x, y, w, h, 3, INK);
}
static void draw_element(unsigned e)
{
    unsigned x, y, w, h;
    if (!rect(e, &x, &y, &w, &h) || y >= paint_top + PAINT_ROWS || y + h <= paint_top) return;
    if (e == E_BODY) fill(x, y, w, h, BG);
    else if (e == E_HEAD) draw_head();
    else if (e == E_FOOT) draw_foot();
    else if (mode == KID_PLAY) draw_tile(e - E_ITEMS, x, y, w, h);
    else if (e - E_ITEMS == 16) draw_pick();
    else draw_cell(e - E_ITEMS, x, y, w, h);
}
/* Mark changed elements, then send up to a few ms of bands to the LCD. */
static void paint(void)
{
    for (unsigned e = 0; e < E_COUNT; ++e) {
        uint32_t s = signature(e);
        unsigned x, y, w, h;
        if (s != shown[e] && rect(e, &x, &y, &w, &h)) paint_mark(x, y, w, h);
        shown[e] = s;
    }
    uint32_t started = fm1_millis();
    for (unsigned visited = 0; visited < PAINT_BANDS; ++visited) {
        int pending = fm1_screen_pending();
        if (pending) { if (pending < 0) memset(shown, 0xff, sizeof shown); break; }
        unsigned band = paint_next;
        paint_next = (band + 1U) % PAINT_BANDS;
        if (paint_left[band] >= paint_right[band]) continue;
        paint_top = band * PAINT_ROWS;
        paint_x = paint_left[band];
        paint_end = paint_right[band];
        for (unsigned e = 0; e < E_COUNT; ++e) draw_element(e);
        if (fm1_screen_submit(paint_x, paint_top, paint_end - paint_x, PAINT_ROWS,
                              paint_pixels, (paint_end - paint_x) * PAINT_ROWS)) {
            memset(shown, 0xff, sizeof shown);
            break;
        }
        paint_left[band] = FM1_SCREEN_WIDTH;
        paint_right[band] = 0;
        if (fm1_millis() - started >= 4U) break;
    }
}
static void leds(void)
{
    uint32_t keys = 0, buttons = 0;
    if (mode == KID_BEAT) {
        for (unsigned i = 0; i < 16; ++i)
            if (step_on(track, i) ^ (playing && play_step == i)) keys |= 1U << white_keys[i];
        if (drum_track(track) && drum < 11) keys |= 1U << black_keys[drum];
    } else keys = previous_keys;
    buttons |= playing ? BUTTON(PLAY) : 0;
    buttons |= recording ? BUTTON(REC) : 0;
    buttons |= mode == KID_BEAT ? BUTTON(SEQ) : BUTTON(HOME);
    buttons |= view.muted ? BUTTON(SCL) : 0;
    buttons |= groove_arp_enabled(track) ? BUTTON(ARP) : 0;
    buttons |= confirm_until ? BUTTON(FX) : 0;
    buttons |= sound_panel ? BUTTON(EDIT) : 0;
    buttons |= songs_panel ? BUTTON(ENV) : 0;
    for (unsigned k = 0; k < 27; ++k)
        if (((keys ^ led_keys) >> k) & 1U) fm1_led_set(14 + k, (keys >> k) & 1U);
    for (unsigned b = 0; b < 14; ++b)
        if (((buttons ^ led_buttons) >> b) & 1U) fm1_led_set(b, (buttons >> b) & 1U);
    led_keys = keys; led_buttons = buttons;
}

/* At boot, an empty track on the 303 shows BASS but is the acid bass line;
 * give it the kid BASS sound (DX7 BASS 1) so the tile and the sound agree. A
 * track with notes keeps its sound, and SOUND can still reach the 303. */
static void tidy_tracks(uint32_t now)
{
    unsigned keep = track;
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        struct groove_view v;
        groove_get_view(t, 0, &v);
        if (!v.ready && now < 4000U) return;           /* try again once the project is in */
    }
    for (track = 0; track < GROOVE_TRACKS; ++track) {
        struct groove_view v;
        groove_get_view(track, 0, &v);
        if (v.ready && v.type == GROOVE_303 && !v.active_steps) set_look(I_BASS);
    }
    track = keep;
    tidy_pending = 0;
    my_slot = groove_project_slot() == DEMO_SLOT ? 0 : groove_project_slot();
}

/* Songs: 0 is MY SONG (my_slot), 1..16 the demo songs. A demo loads its
 * sounds and patterns, then lives in DEMO_SLOT so changes to it are kept
 * without touching MY SONG. */
static unsigned demo_named(const char name[13])
{
    for (unsigned i = 0; i < GROOVE_FACTORY_PROJECTS; ++i) {
        const char *d = groove_factory_name(i);
        size_t n = d ? strlen(d) : 0;
        if (n && !strncmp(name, d, n) && (n == 12 || name[n] == ' ' || !name[n])) return i + 1U;
    }
    return 0;
}
static unsigned current_song(void)
{
    if (groove_project_slot() != DEMO_SLOT) return 0;
    char name[13];
    groove_project_name(name);
    return demo_named(name);
}
static int load_song(unsigned song)
{
    release_keys();
    if (playing) { if (recording) groove_edit_record(0, track, 0); (void)post(0xfc, 0, 0, 1); }
    unsigned slot = groove_project_slot();
    /* Keep what is there first; MY SONG is always written so it can come back. */
    if ((groove_project_dirty() || slot == my_slot) && groove_project_save_slot(slot)) return -1;
    if (!song) return groove_project_load_slot(my_slot);
    char name[13];
    if (!groove_project_info(DEMO_SLOT, name) && !demo_named(name)) return -2;   /* someone else's project */
    if (groove_factory_load(song - 1U)) return -1;
    return groove_project_save_slot(DEMO_SLOT);
}
static const char *song_name(unsigned song)
{
    return song ? groove_factory_name(song - 1U) : "MY SONG";
}
static void draw_songs(void)
{
    char count[8];
    unsigned n = song_pick + 1U, i = 0;
    if (n >= 10) count[i++] = (char)('0' + n / 10U);
    count[i++] = (char)('0' + n % 10U); count[i++] = '/';
    count[i++] = '1'; count[i++] = '7'; count[i] = 0;
    text(4, 193, "SONGS", 1, kid_colors[6]);
    text(200, 193, count, 1, OFF4);
    text(4, 207, "<", 2, SOFT);
    text(226, 207, ">", 2, SOFT);
    text_center(0, 240, 207, song_name(song_pick), 2, song_pick == current_song() ? GO : INK);
    text_center(0, 240, 225, song_pick == current_song() ? "PLAY = PLAY   ENV = DONE" : "PLAY = LOAD AND PLAY", 1, OFF4);
}

/* ---------- entry points ---------- */
void groove_screen_init(void)
{
    track = mode = drum = recording = playing = play_step = retry_reset = 0;
    octave = 0; beat_note = 60;
    previous_buttons = previous_keys = confirm_until = 0;
    input_at = save_tried_at = warned_at = picked_at = 0;
    picking = pick_kind = 0;
    sound_panel = 0;
    tidy_pending = 1;
    songs_panel = song_pick = 0;
    scale = chords = 0;
    memset(knob_dir, 0, sizeof knob_dir); memset(knob_speed, 0, sizeof knob_speed); memset(knob_at, 0, sizeof knob_at);
    arp_down_at = 0; arp_long = arp_turned = arp_panel = 0;
    battery_avg = 0; battery_level = 3; on_usb = blink = 0;
    memset(key_note, 0xff, sizeof key_note);
    memset(key_chord, 0xff, sizeof key_chord);
    memset(shown, 0xff, sizeof shown);
    memset(paint_left, 0, sizeof paint_left);
    memset(paint_right, FM1_SCREEN_WIDTH, sizeof paint_right);   /* paint everything once */
    paint_next = 0;
    led_keys = led_buttons = 0;
    fm1_controls_init();
    for (unsigned key = 0; key < 41; ++key) fm1_led_set(key, 0);
    fm1_screen_init();
    booted_at = fm1_millis();
    say("FM1 KIDS " KID_VERSION, kid_colors[4], booted_at);
    notice_until = booted_at + 2000U;
    groove_get_view(track, 0, &view);
    paint();
}
static void select_track(unsigned t)
{
    if (t == track) return;
    release_keys();
    track = t; drum = 0; picking = 0;
    if (recording) groove_edit_record(1, track, 0);
}
void groove_screen_control(void)
{
    struct fm1_controls c;
    fm1_controls_read(&c);
    uint32_t now = fm1_millis();
    knobs_read(&c, now);
    groove_master_update(c.knob < KID_MASTER_LIMIT ? c.knob : KID_MASTER_LIMIT);
    uint32_t pressed = c.buttons & ~previous_buttons;
    uint32_t key_down = c.keys & ~previous_keys;
    unsigned turned = 0;
    for (unsigned e = 0; e < 7; ++e) turned |= c.encoders[e] != 0;
    if (pressed || key_down || turned) input_at = now;
    if (notice && (int32_t)(now - notice_until) >= 0) notice = NULL;
    if (retry_reset) {
        if (!post(0xff, 0, 0, 1)) return;
        retry_reset = 0;
        memset(key_note, 0xff, sizeof key_note);
        memset(key_chord, 0xff, sizeof key_chord);
    }
    battery_poll(c.battery, now);
    /* Every track loops one row of keys. */
    /* Every track loops one row of keys, except longer loops a demo song brings. */
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t)
        if (groove_sequence_length(t) != KID_STEPS) {
            struct groove_view v;
            groove_get_view(t, 0, &v);
            if (!(v.active_steps >> KID_STEPS)) groove_sequence_length_set(t, KID_STEPS);
        }

    fm1_ble_get_status(&ble);
    parent = (c.buttons & BUTTON(GLO)) != 0;
    if (parent) {
        unsigned count = ble.count < FM1_BLE_MAX_DEVICES ? ble.count : FM1_BLE_MAX_DEVICES;
        ble_pick = (unsigned)clampi((int)ble_pick + c.encoders[0], 0, count ? (int)count - 1 : 0);
        if (pressed & BUTTON(PLAY)) {
            if (ble.role != FM1_BLE_CENTRAL)
                ble_result = groove_settings_radio(groove_settings_radio_flags() | GROOVE_RADIO_ON, FM1_BLE_CENTRAL);
            else ble_result = fm1_ble_scan();
            ble_pick = 0;
        }
        if (pressed & BUTTON(SAVE)) ble_result = fm1_ble_connect(ble_pick);
        if (pressed & BUTTON(REC)) ble_result = fm1_ble_disconnect();
        if (pressed & BUTTON(HOME))
            ble_result = groove_settings_radio(groove_settings_radio_flags() | GROOVE_RADIO_ON, FM1_BLE_PERIPHERAL);
        previous_buttons = c.buttons;
        previous_keys = c.keys;
        paint();
        return;
    }
    if (songs_panel) {                                 /* ENV: SELECT, ALGORITHM or PRESETS browse the songs */
        int d = c.encoders[0] + c.encoders[1] + c.encoders[6];
        song_pick = (unsigned)clampi((int)song_pick + d, 0, SONGS - 1);
    } else if (c.encoders[0]) select_track((unsigned)clampi((int)track + c.encoders[0], 0, GROOVE_TRACKS - 1));
    arp_panel = (c.buttons & BUTTON(ARP)) != 0;
    if (arp_panel) {                                   /* ARP held: K1..K4 are its timing */
        static const uint8_t setting[4] = {0, 2, 3, 4};   /* rate, range, gate, swing */
        for (unsigned k = 0; k < 4; ++k)
            if (c.encoders[2 + k]) { groove_arp_edit(track, setting[k], c.encoders[2 + k]); arp_turned = 1; }
    } else if (sound_panel == 2) {                     /* EDIT twice: the keys */
        if (c.encoders[2]) { release_keys(); scale = (unsigned)((int)(scale + SCALES) + c.encoders[2] % SCALES) % SCALES; }
        if (c.encoders[3]) { release_keys(); chords = c.encoders[3] > 0; }
    } else if (sound_panel) {                          /* EDIT: the sound panel */
        if (c.encoders[2]) groove_sends_edit(track, 0, c.encoders[2]);
        if (c.encoders[3]) groove_sends_edit(track, 1, c.encoders[3]);
        if (c.encoders[4]) long_edit(fast(&c, 4));
        if (c.encoders[5]) groove_effects_edit(0, fast(&c, 5));
    } else {
        if (c.encoders[2]) groove_control_edit(track, 0, fast(&c, 2), 0);
        if (c.encoders[3]) bright_edit(kind(track) == GROOVE_DX7 ? c.encoders[3] : fast(&c, 3));
        if (c.encoders[4]) next_voice(c.encoders[4]);
        if (c.encoders[5]) groove_tempo_edit(fast(&c, 5));
    }
    if (!songs_panel && mode == KID_BEAT && (c.encoders[1] || c.encoders[6]) && drum_track(track))
        drum = (unsigned)clampi((int)drum + c.encoders[1] + c.encoders[6], 0, (int)groove_pad_count(track) - 1);
    /* ALGORITHM in PLAY: turn to choose a new instrument; it changes once the knob rests. */
    if (!songs_panel && mode == KID_PLAY && c.encoders[1]) {
        if (!picking) pick_kind = look(track);
        pick_kind = (unsigned)(((int)pick_kind + c.encoders[1] % (int)I_COUNT + (int)I_COUNT) % (int)I_COUNT);
        picking = 1; picked_at = now;
        say(kind_name[pick_kind], kid_colors[track], now);
    }
    if (picking && since(now, picked_at) >= 700) {
        picking = 0;
        if (pick_kind != look(track) || kind(track) != kind_sound[pick_kind].engine) {
            release_keys();
            if (set_look(pick_kind)) say("OOPS", STOP, now);
            drum = 0;
        }
    }

    groove_get_view(track, 0, &view);
    playing = view.playing;
    play_step = view.play_step;
    recording = groove_edit_recording();
    keyboard_channel = groove_midi_input_channel(track);
    unsigned old_mode = mode;
    /* Start over: FX asks, FX again clears; anything else says no. */
    if (confirm_until) {
        if (pressed & BUTTON(FX)) {
            confirm_until = 0;
            say(groove_edit_clear(track, 0, 2) ? "OOPS" : "CLEARED", kid_colors[0], now);
            pressed &= ~BUTTON(FX);
        } else if (pressed || key_down || turned || (int32_t)(now - confirm_until) >= 0) {
            confirm_until = 0;
            pressed = 0; key_down = 0;
        }
    } else if (pressed & BUTTON(FX)) {
        confirm_until = (now + 4000U) | 1U;
        pressed &= ~BUTTON(FX);
    }
    if (pressed & BUTTON(HOME)) { mode = KID_PLAY; sound_panel = songs_panel = 0; }
    if (pressed & BUTTON(ENV)) {
        songs_panel = !songs_panel;
        if (songs_panel) { song_pick = current_song(); sound_panel = 0; }
    }
    if ((pressed & BUTTON(PLAY)) && songs_panel && song_pick != current_song()) {
        int r = load_song(song_pick);
        pressed &= ~BUTTON(PLAY);
        if (r) say(r == -2 ? "SLOT 16 BUSY" : "OOPS", STOP, now);
        else {
            track = 0; drum = 0; mode = KID_PLAY;
            say(song_name(song_pick), kid_colors[6], now);
            (void)post(0xfa, 0, 0, 1);
        }
    }
    if (pressed & BUTTON(SEQ)) mode = mode == KID_BEAT ? KID_PLAY : KID_BEAT;
    if (pressed & (BUTTON(OCT_DOWN) | BUTTON(OCT_UP))) {
        release_keys();
        octave = clampi(octave + (pressed & BUTTON(OCT_UP) ? 1 : -1), -2, 2);
    }
    if (pressed & BUTTON(PLAY)) {
        if (playing) { if (recording) groove_edit_record(0, track, 0); (void)post(0xfc, 0, 0, 1); }
        else (void)post(0xfa, 0, 0, 1);
    }
    if (pressed & BUTTON(REC)) {
        groove_edit_record(!recording, track, 0);
        if (!recording && !playing) (void)post(0xfa, 0, 0, 1);
    }
    if (pressed & BUTTON(SAVE)) {
        int failed = groove_project_save_slot(groove_project_slot());
        say(failed ? "OOPS" : "SAVED!", failed ? STOP : GO, now);
    }
    if (pressed & BUTTON(EDIT)) { sound_panel = (sound_panel + 1U) % 3U; songs_panel = 0; }
    if (pressed & BUTTON(LFO)) say(groove_edit_undo() ? "NO UNDO" : "UNDO!", kid_colors[4], now);
    /* ARP: a tap steps UP, DOWN, BOUNCE, RANDOM, OFF; holding it toggles latch. */
    if (pressed & BUTTON(ARP)) { arp_down_at = now | 1U; arp_long = arp_turned = 0; }
    if ((c.buttons & BUTTON(ARP)) && arp_down_at && !arp_long && !arp_turned && since(now, arp_down_at) >= 800) {
        unsigned latch = !groove_arp_setting(track, 5);
        arp_long = 1;
        if (!groove_arp_enabled(track)) groove_arp_set(track, 1);
        groove_arp_edit(track, 5, latch ? 1 : -1);
        say(latch ? "HOLD ON" : "HOLD OFF", kid_colors[2], now);
    }
    if ((previous_buttons & ~c.buttons & BUTTON(ARP)) && arp_down_at && !arp_long && !arp_turned) {
        unsigned style = groove_arp_setting(track, 1);
        if (!groove_arp_enabled(track)) {
            groove_arp_set(track, 1);
            groove_arp_edit(track, 1, -3);               /* start at UP */
            say(arp_names[0], kid_colors[2], now);
        } else if (style < 3) {
            groove_arp_edit(track, 1, 1);
            say(arp_names[style + 1], kid_colors[2], now);
        } else {
            groove_arp_set(track, 0);
            say("ARP OFF", SOFT, now);
        }
    }
    if (!(c.buttons & BUTTON(ARP))) arp_down_at = 0;
    if (pressed & BUTTON(SCL)) {
        groove_mix_toggle(track, 0);
        groove_get_view(track, 0, &view);
        say(view.muted ? "QUIET" : "ON", view.muted ? SOFT : GO, now);
    }
    if (mode != old_mode) release_keys();

    /* BEAT: white keys toggle steps; black keys pick what to place, and sound it. */
    uint32_t live = c.keys;
    if (mode == KID_BEAT) {
        for (unsigned i = 0; i < 16; ++i) {
            if (key_down & (1U << white_keys[i])) toggle_step(i);
            live &= ~(1U << white_keys[i]);
        }
        for (unsigned b = 0; b < 11; ++b)
            if (key_down & (1U << black_keys[b])) {
                if (drum_track(track)) { if (b < groove_pad_count(track)) drum = b; }
                else beat_note = (unsigned)clampi((int)scale_note(b) + 12 * octave, 0, 127);
            }
    }
    for (unsigned key = 0; key < 27; ++key) {
        unsigned held = (live >> key) & 1U;
        unsigned note = key_pitch(key);
        if (mode == KID_BEAT) {
            unsigned b = 0;
            while (b < 11 && black_keys[b] != key) ++b;
            note = b == 11 ? 128 : drum_track(track) ? (b < groove_pad_count(track) ? groove_pad_note(track, b) : 128)
                                                     : (unsigned)clampi((int)scale_note(b) + 12 * octave, 0, 127);
        }
        if (key_note[key] != 0xff && (!held || key_track[key] != track) && !key_off(key)) break;
        if (held && key_note[key] == 0xff && note < 128) {
            uint8_t status = (uint8_t)(0x90 + groove_midi_input_channel(track)), notes[3];
            if (!post(status, (uint8_t)note, 100, 3)) break;
            key_note[key] = (uint8_t)note;
            key_track[key] = (uint8_t)track;
            if (!drum_track(track)) beat_note = note;
            if (chords && !drum_track(track))
                for (unsigned i = 1, n = chord_notes(note, notes); i < n; ++i)
                    if (post(status, notes[i], 90, 3)) key_chord[key][i - 1] = notes[i];
        }
    }
    if (tidy_pending) tidy_tracks(now);
    autosave(now);
    previous_buttons = c.buttons;
    previous_keys = c.keys;
    leds();
    paint();
}
void groove_screen_tap(uint32_t now)
{
    if (groove_tempo_tap(now) > 0) say("TAP!", kid_colors[1], now);
}
void groove_screen_mmc(unsigned command)
{
    unsigned transport = command == 1 || command == 9 ? 0xfc : command == 2 || command == 3 || command == 6 ? 0xfb : 0;
    if (!transport && command != 7) return;
    groove_edit_record(command == 6, track, 0);
    if (transport) (void)post((uint8_t)transport, 0, 0, 1);
}

#ifdef FM1_HOST_TEST
/* Browser preview hooks: the workbench page runs this same interface. */
int fm1_device_usb_connected(void) { return 0; }
static char web_knobs[4][2][16];
static struct { int32_t x, y, w, h, action, index, owner, raw; char label[16], value[16]; } web_targets[1];
void groove_screen_web_fine(unsigned fine) { (void)fine; }
int groove_screen_preset(unsigned slot) { return groove_factory_load(slot); }
unsigned groove_screen_preset_position(void) { return 1U << 8; }
int groove_screen_open(unsigned target) { (void)target; return 0; }
unsigned groove_screen_web_context(void) { return mode | track << 4; }
unsigned groove_screen_web_mask(void) { return 0; }
const void *groove_screen_web_targets(void) { return web_targets; }
unsigned groove_screen_web_target_count(void) { return 0; }
int groove_screen_web_adjust(unsigned context, unsigned action, unsigned index, unsigned owner, int delta)
{
    (void)context; (void)action; (void)index; (void)owner; (void)delta;
    return -1;
}
const char *groove_screen_web_knobs(void)
{
    static const char *const labels[4] = {"LOUD", "BRIGHT", "SOUND", "SPEED"};
    for (unsigned k = 0; k < 4; ++k) {
        memset(web_knobs[k], 0, sizeof web_knobs[k]);
        memcpy(web_knobs[k][0], labels[k], strlen(labels[k]));
    }
    return &web_knobs[0][0][0];
}
unsigned groove_screen_web_first(void) { return 0; }
int groove_screen_web_step(unsigned step, unsigned toggle)
{
    if (step >= KID_STEPS) return -1;
    if (toggle) toggle_step(step);
    return 0;
}
void groove_screen_snapshot(uint8_t state[8])
{
    memset(state, 0, 8);
    state[0] = (uint8_t)track;
    state[3] = (uint8_t)drum;
    state[4] = (uint8_t)recording;
    state[7] = (uint8_t)(octave + 24);
}
#endif

/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1 Kids: a simple interface for young children, included by app/screen.c
 * when FM1_KID_UI is 1. It replaces M-VAVE's screen and keeps its entry points
 * (groove_screen_init/control/tap/mmc and the browser preview hooks).
 * The band painter and font are adapted from M-VAVE's screen.c.
 *
 * Controls
 *   SELECT knob      choose an instrument (track)
 *   piano keys       play it; melodic tracks use "happy keys" (pentatonic)
 *   SEQ              BEAT mode: white keys switch steps on/off,
 *                    black keys pick the drum (or note) to place
 *   HOME             back to PLAY mode
 *   PLAY             start / stop      REC   record what you play
 *   K1..K4           LOUD, BRIGHT, SOUND, SPEED
 *   EDIT             undo              SAVE  save the song
 *   hold FX          clear this track (undo brings it back)
 *   OCT- / OCT+      lower / higher
 */
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
static const uint8_t happy[12] = {0, 0, 2, 2, 4, 4, 4, 7, 7, 9, 9, 9};
static const uint8_t happy_notes[11] = {60, 62, 64, 67, 69, 72, 74, 76, 79, 81, 84};
static const char *const kind_name[GROOVE_SOURCES] = {"PIANO", "BASS", "DRUMS", "GAME", "GUITAR", "FUN"};
/* Pictures: 16x12 drawn 3x, or 24x18 drawn 2x (same size on screen); the
 * leftmost pixel is the highest used bit. Order follows enum groove_source. */
static const struct { uint8_t w, h; uint32_t rows[18]; } kind_icon[GROOVE_SOURCES] = {
    {16, 12, {0x0000, 0xffff, 0x96b5, 0x96b5, 0x96b5, 0x96b5, 0xddb7, 0xddb7, 0xddb7, 0xffff, 0x0000, 0x0000}},
    {16, 12, {0x0030, 0x0038, 0x002c, 0x0026, 0x0022, 0x0020, 0x0020, 0x0020, 0x03e0, 0x07e0, 0x07c0, 0x0380}},
    {16, 12, {0x8001, 0x4002, 0x2004, 0x1008, 0x3ffc, 0x4002, 0x7ffe, 0x5555, 0x5555, 0x5555, 0x7ffe, 0x0000}},
    {16, 12, {0x1010, 0x0820, 0x1ff0, 0x37d8, 0x7ffc, 0x5ff4, 0x5014, 0x0c60, 0x0000, 0x0000, 0x0000, 0x0000}},
    {24, 18, {0x000014, 0x00001e, 0x00003c, 0x000028, 0x000060, 0x0000c0, 0x003b00, 0x007f00, 0x01fe00,
              0x07cf00, 0x0f8600, 0x1f8e00, 0x1ffc00, 0x1ff800, 0x1ff000, 0x1ff000, 0x0fe000, 0x07c000}},
    {16, 12, {0x0100, 0x0100, 0x0380, 0xfff8, 0x7ff0, 0x3fe0, 0x1fc0, 0x3de0, 0x38e0, 0x6030, 0x0000, 0x0000}}};
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
                                         {'>', 0x41, 0x22, 0x14, 8, 0}, {':', 0, 0x36, 0x36, 0, 0}};

/* ---------- state ---------- */
static unsigned track, mode, drum, recording, playing, play_step, retry_reset;
static unsigned beat_note = 60;
static int octave;
static uint32_t previous_buttons, previous_keys, fx_down_at, notice_until;
static const char *notice;
static uint16_t notice_color;
static uint8_t key_note[27], key_track[27];
static uint32_t shown[E_COUNT], led_keys, led_buttons;
static struct groove_view view;   /* the current track, refreshed every poll */

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
static void release_keys(void)
{
    for (unsigned key = 0; key < 27; ++key)
        if (key_note[key] != 0xff) {
            if (!post((uint8_t)(0x80 + groove_midi_input_channel(key_track[key])), key_note[key], 0, 3)) return;
            key_note[key] = 0xff;
        }
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
    groove_sequence_edit(track, step, note, on ? 0 : 100);
}
/* What a key plays on this track: drums keep their pad layout; tunes use happy keys. */
static unsigned key_pitch(unsigned key)
{
    unsigned note = groove_key_note(track, key, octave);
    if (note >= 128 || drum_track(track)) return note;
    return note - note % 12U + happy[note % 12U];
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
static uint32_t signature(unsigned e)
{
    uint32_t base = mode | track << 1;
    if (e == E_BODY) return mode;
    if (e == E_HEAD) return base | playing << 4 | recording << 5 | (uint32_t)view.tempo << 6 | (uint32_t)(octave + 4) << 20;
    if (e == E_FOOT) {
        if (notice) return 0x80000000U | (uint32_t)(uintptr_t)notice;
        return base | (uint32_t)view.volume << 4 | (uint32_t)view.program << 12 | (uint32_t)(view.tempo & 0xff) << 20;
    }
    unsigned i = e - E_ITEMS;
    if (mode == KID_PLAY) {
        if (i >= 8) return 0;
        struct groove_view v;
        groove_get_view(i, 0, &v);
        unsigned sounding = 0;
        for (unsigned k = 0; k < 27; ++k) sounding |= key_note[k] != 0xff && key_track[k] == i;
        return (i == track) | kind(i) << 1 | (uint32_t)v.muted << 4 | sounding << 5 | 1U << 6;
    }
    if (i == 16) return base | drum << 4 | beat_note << 8 | kind(track) << 16;
    return base | step_on(track, i) << 4 | (playing && play_step == i) << 5 | drum << 6 | 1U << 12;
}
static void draw_head(void)
{
    fill(0, 0, 240, 40, BG);
    text(8, 9, recording ? "REC" : mode == KID_BEAT ? "BEAT" : "PLAY", 3, recording ? STOP : INK);
    /* Speed: one to five dots */
    unsigned dots = (unsigned)clampi(((int)view.tempo - 50) / 30 + 1, 1, 5);
    for (unsigned d = 0; d < 5; ++d) fill(120 + d * 13U, 16, 9, 9, d < dots ? kid_colors[1] : OFF);
    if (octave) text(124, 30, octave > 0 ? "HIGH" : "LOW", 1, SOFT);
    if (playing) {                                     /* a green play triangle */
        for (unsigned r = 0; r < 24; ++r) fill(206, 8 + r, r < 12 ? r + 1 : 24 - r, 1, GO);
    } else fill(204, 9, 22, 22, SOFT);                 /* a stop square */
    if (recording) fill(190, 15, 10, 10, STOP);
}
static void draw_foot(void)
{
    fill(0, 190, 240, 50, BG);
    if (notice) { text_center(0, 240, 204, notice, 3, notice_color); return; }
    static const char *const labels[4] = {"LOUD", "BRIGHT", "SOUND", "SPEED"};
    for (unsigned k = 0; k < 4; ++k) {
        unsigned x = k * 60U;
        fill(x + 4, 192, 52, 3, kid_colors[(k * 2U + 1U) % 8U]);
        text_center(x, 60, 200, labels[k], 1, SOFT);
        char value[8] = "";
        if (k == 0) {
            fill(x + 8, 216, 44, 10, OFF);
            fill(x + 8, 216, view.volume * 44U / 127U, 10, kid_colors[1]);
        } else if (k == 2) {
            number(value, view.program + 1U);
        } else if (k == 3) {
            number(value, view.tempo);
        } else text_center(x, 60, 214, "<>", 2, OFF4);
        if (value[0]) text_center(x, 60, 214, value, 2, INK);
    }
}
static void draw_tile(unsigned i, unsigned x, unsigned y, unsigned w, unsigned h)
{
    struct groove_view v;
    groove_get_view(i, 0, &v);
    uint16_t c = kid_colors[i];
    unsigned selected = i == track;
    fill(x, y, w, h, selected ? c : v.muted ? OFF : dim(c));
    if (selected) frame(x, y, w, h, 3, INK);
    picture(x + 4, y + 6, kind(i), 0, selected ? INK : v.muted ? OFF4 : SOFT);
    text_center(x, w, y + h - 16, kind_name[kind(i)], 1, selected ? BG : INK);
    for (unsigned k = 0; k < 27; ++k)
        if (key_note[k] != 0xff && key_track[k] == i) { fill(x + w - 12, y + 6, 7, 7, INK); break; }
}
static void draw_pick(void)
{
    fill(0, 42, 240, 40, BG);
    uint16_t c = kid_colors[track];
    picture(4, 46, kind(track), 1, c);
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
static void draw_cell(unsigned i, unsigned x, unsigned y, unsigned w, unsigned h)
{
    unsigned on = step_on(track, i);
    fill(x, y, w, h, on ? kid_colors[track] : i % 4U ? OFF : OFF4);
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
    for (unsigned k = 0; k < 27; ++k)
        if (((keys ^ led_keys) >> k) & 1U) fm1_led_set(14 + k, (keys >> k) & 1U);
    for (unsigned b = 0; b < 14; ++b)
        if (((buttons ^ led_buttons) >> b) & 1U) fm1_led_set(b, (buttons >> b) & 1U);
    led_keys = keys; led_buttons = buttons;
}

/* ---------- entry points ---------- */
void groove_screen_init(void)
{
    track = mode = drum = recording = playing = play_step = retry_reset = 0;
    octave = 0; beat_note = 60; notice = NULL;
    previous_buttons = previous_keys = fx_down_at = 0;
    memset(key_note, 0xff, sizeof key_note);
    memset(shown, 0xff, sizeof shown);
    memset(paint_left, 0, sizeof paint_left);
    memset(paint_right, FM1_SCREEN_WIDTH, sizeof paint_right);   /* paint everything once */
    paint_next = 0;
    led_keys = led_buttons = 0;
    fm1_controls_init();
    for (unsigned key = 0; key < 41; ++key) fm1_led_set(key, 0);
    fm1_screen_init();
    groove_get_view(track, 0, &view);
    paint();
}
static void select_track(unsigned t)
{
    if (t == track) return;
    release_keys();
    track = t; drum = 0;
    struct groove_view v;
    groove_get_view(track, 0, &v);
    if (v.muted) groove_mix_toggle(track, 0);          /* kids never wonder why it is silent */
    if (recording) groove_edit_record(1, track, 0);
}
void groove_screen_control(void)
{
    struct fm1_controls c;
    fm1_controls_read(&c);
    uint32_t now = fm1_millis();
    groove_master_update(c.knob < KID_MASTER_LIMIT ? c.knob : KID_MASTER_LIMIT);
    uint32_t pressed = c.buttons & ~previous_buttons;
    uint32_t key_down = c.keys & ~previous_keys;
    if (notice && (int32_t)(now - notice_until) >= 0) notice = NULL;
    if (retry_reset) {
        if (!post(0xff, 0, 0, 1)) return;
        retry_reset = 0;
        memset(key_note, 0xff, sizeof key_note);
    }
    /* Every track loops one row of keys. */
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t)
        if (groove_sequence_length(t) != KID_STEPS) groove_sequence_length_set(t, KID_STEPS);

    if (c.encoders[0]) select_track((unsigned)clampi((int)track + c.encoders[0], 0, GROOVE_TRACKS - 1));
    if (c.encoders[2]) groove_control_edit(track, 0, c.encoders[2], 0);
    if (c.encoders[3]) groove_control_edit(track, 0, 0, c.encoders[3]);
    if (c.encoders[4]) groove_control_edit(track, c.encoders[4], 0, 0);
    if (c.encoders[5]) groove_tempo_edit(c.encoders[5]);
    if ((c.encoders[1] || c.encoders[6]) && drum_track(track))
        drum = (unsigned)clampi((int)drum + c.encoders[1] + c.encoders[6], 0, (int)groove_pad_count(track) - 1);

    groove_get_view(track, 0, &view);
    playing = view.playing;
    play_step = view.play_step;
    recording = groove_edit_recording();
    unsigned old_mode = mode;
    if (pressed & BUTTON(HOME)) mode = KID_PLAY;
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
    if (pressed & BUTTON(EDIT)) say(groove_edit_undo() ? "NO UNDO" : "UNDO!", kid_colors[4], now);
    if (pressed & BUTTON(FX)) fx_down_at = now | 1U;
    if (!(c.buttons & BUTTON(FX))) fx_down_at = 0;
    else if (fx_down_at && now - fx_down_at >= 1500U) {
        fx_down_at = 0;
        say(groove_edit_clear(track, 0, 2) ? "OOPS" : "CLEARED", kid_colors[0], now);
    } else if (fx_down_at && !notice) say("HOLD...", SOFT, now);
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
                else beat_note = (unsigned)clampi(happy_notes[b] + 12 * octave, 0, 127);
            }
    }
    for (unsigned key = 0; key < 27; ++key) {
        unsigned held = (live >> key) & 1U;
        unsigned note = key_pitch(key);
        if (mode == KID_BEAT) {
            unsigned b = 0;
            while (b < 11 && black_keys[b] != key) ++b;
            note = b == 11 ? 128 : drum_track(track) ? (b < groove_pad_count(track) ? groove_pad_note(track, b) : 128)
                                                     : (unsigned)clampi(happy_notes[b] + 12 * octave, 0, 127);
        }
        if (key_note[key] != 0xff && (!held || key_track[key] != track)) {
            if (!post((uint8_t)(0x80 + groove_midi_input_channel(key_track[key])), key_note[key], 0, 3)) break;
            key_note[key] = 0xff;
        }
        if (held && key_note[key] == 0xff && note < 128) {
            if (!post((uint8_t)(0x90 + groove_midi_input_channel(track)), (uint8_t)note, 100, 3)) break;
            key_note[key] = (uint8_t)note;
            key_track[key] = (uint8_t)track;
            if (!drum_track(track)) beat_note = note;
        }
    }
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

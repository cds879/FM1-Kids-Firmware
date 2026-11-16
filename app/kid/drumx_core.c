/* SPDX-License-Identifier: GPL-3.0-only
 * Experimental FM1 application-side adaptation of JIANT FM DRUM-X.
 * Copyright (C) 2026 Juan Ignacio Antonucci, Hügelton Instruments
 * JIANT reference: firmware/src/drumx_voice.c (GPL-3.0-only).
 * We retain JIANT's 8-lane A/B kit structure and factory values; this
 * standalone fixed-point renderer is newly adapted for 44.1kHz/Q23 FM1
 * output and is NOT yet bit-for-bit or sound-for-sound identical to JIANT.
 */
#include "kid_config.h"
#if FM1_KID_UI
#include "drumx_core.h"
#include "fm1.h"
#include <stdint.h>
#include <string.h>

struct patch { uint8_t mode, a[5], b[5]; };
/* The eight original JIANT factory voices: pitch, sweep, decay, noise, color.
 * Modes encode SINE/FM/METAL/BELL and LP/BP/HP as in JIANT DRUM-X. */
static const struct patch factory[KDX_LANES] = {
    {16, {9,70,72,4,20}, {14,100,88,10,60}},
    {5, {40,30,52,80,90}, {46,18,64,104,70}},
    {4, {60,0,46,127,84}, {54,0,58,127,76}},
    {10, {100,0,22,96,116}, {108,0,14,112,122}},
    {10, {100,0,62,96,112}, {108,0,74,112,118}},
    {16, {30,40,66,10,30}, {36,60,76,20,50}},
    {27, {63,10,14,16,46}, {70,24,20,30,60}},
    {14, {64,0,44,34,50}, {68,0,54,46,62}}
};
static const int16_t sintab[65] = {0, 3212, 6393, 9512, 12539, 15446, 18204, 20787, 23170, 25329, 27245, 28898, 30273, 31356, 32137, 32609, 32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329, 23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212, 0, -3212, -6393, -9512, -12539, -15446, -18204, -20787, -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609, -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329, -23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212, 0};
static const uint16_t note256[12] = {
    4186,4435,4698,4978,5274,5588,5920,6272,6645,7040,7459,7902
};
struct hit {
    uint32_t phase, modphase, increment, end_env, env;
    int32_t noise_lp, previous_noise;
    uint32_t rng;
    uint8_t alive, velocity, lane;
};
static struct { struct hit hit[KDX_LANES]; struct patch kit[KDX_LANES]; } state;
static volatile uint8_t controls[5], enabled;
static struct { uint8_t lane, velocity; } pending[16];
static volatile uint8_t read_at, write_at;
static inline int32_t sine(uint32_t phase)
{
    unsigned n = phase >> 26, frac = (phase >> 18) & 255U;
    int32_t a = sintab[n], b = sintab[n + 1U];
    return a + (((b - a) * (int32_t)frac) >> 8);
}
static unsigned blend(unsigned a, unsigned b, unsigned m)
{ return (a * (127U - m) + b * m + 63U) / 127U; }
/* MIDI 24..119, JIANT's 3/4-semitone-step PITCH control. */
static uint32_t pitch_increment(unsigned pitch)
{
    unsigned midi = 24U + pitch * 95U / 127U;
    unsigned octave = midi / 12U, semitone = midi % 12U;
    uint64_t hz256 = (uint64_t)note256[semitone] << octave;
    uint64_t inc = (hz256 << 24) / 44100U;
    return inc > UINT32_MAX ? UINT32_MAX : (uint32_t)inc;
}
static unsigned param(unsigned lane, unsigned index)
{
    unsigned m = controls[KDX_MORPH];
    return blend(state.kit[lane].a[index], state.kit[lane].b[index], m);
}
void fm1_kdx_reset(void)
{
    memset(&state, 0, sizeof state);
    memcpy(state.kit, factory, sizeof factory);
    controls[KDX_MORPH] = 0;
    controls[KDX_PITCH] = 64;
    controls[KDX_DECAY] = 64;
    controls[KDX_CHAOS] = 0;
    controls[KDX_GAIN] = 80;
    read_at = write_at = 0;
    enabled = 0;
}
void fm1_kdx_enable(unsigned value) { enabled = !!value; }
void fm1_kdx_control(unsigned control, unsigned value)
{ if (control < 5) controls[control] = (uint8_t)(value > 127U ? 127U : value); }
unsigned fm1_kdx_control_get(unsigned control)
{ return control < 5 ? controls[control] : 0; }
void fm1_kdx_note(unsigned lane, unsigned velocity)
{
    if (lane >= KDX_LANES || velocity > 127U) return;
    unsigned next = (write_at + 1U) & 15U;
    if (next == read_at) return; /* bounded, no audio-IRQ allocation */
    pending[write_at].lane = (uint8_t)lane;
    pending[write_at].velocity = (uint8_t)velocity;
    /* The FM1 compiler does not provide __atomic 1-byte helpers. Both
     * cursors are naturally atomic on this single-core device; a compiler
     * barrier publishes the slot before its write cursor. */
    __asm__ volatile("" ::: "memory");
    write_at = (uint8_t)next;
}
static void start_hit(unsigned lane, unsigned velocity)
{
    struct hit *v = &state.hit[lane];
    v->lane = (uint8_t)lane;
    v->phase = v->modphase = 0;
    v->increment = pitch_increment(param(lane, 0));
    v->env = 0x1000000U;
    v->end_env = 0;
    v->noise_lp = v->previous_noise = 0;
    v->rng = 0x9e3779b9U ^ (lane * 0x45d9f3bU) ^ v->increment;
    v->velocity = (uint8_t)velocity;
    v->alive = 1;
}
/* Audio-IRQ only. No dynamic allocation, locks, divisions in the per-sample
 * path, or reliance on mutable SDK internal voice structures. */
FM1_AUDIO_CODE void fm1_kdx_mix(int32_t *stereo, unsigned frames)
{
    if (!stereo || !enabled) return;
    uint8_t stop = write_at;
    __asm__ volatile("" ::: "memory");
    while (read_at != stop) {
        unsigned i = read_at;
        if (pending[i].velocity) start_hit(pending[i].lane, pending[i].velocity);
        else if (pending[i].lane < KDX_LANES) state.hit[pending[i].lane].alive = 0;
        read_at = (uint8_t)((i + 1U) & 15U);
    }
    for (unsigned lane = 0; lane < KDX_LANES; ++lane) {
        struct hit *v = &state.hit[lane];
        if (!v->alive) continue;
        unsigned morph_pitch = param(lane, 0);
        unsigned pitch = (unsigned)((int)morph_pitch + ((int)controls[KDX_PITCH] - 64) / 3);
        if (pitch > 127U) pitch = 127U;
        uint32_t inc = pitch_increment(pitch);
        unsigned decay = param(lane, 2);
        decay = (decay + controls[KDX_DECAY]) / 2U;
        /* 3 ms - 1.5 s, intentionally conservative with short block tails. */
        unsigned len = 132U + decay * 511U;
        uint32_t loss = (v->env / len) * frames;
        if (loss < 100U) loss = 100U;
        if (loss > v->env) loss = v->env;
        unsigned nfrac = param(lane, 3);
        unsigned color = param(lane, 4);
        unsigned chaos = controls[KDX_CHAOS];
        unsigned mix = (nfrac + chaos / 2U > 127U ? 127U : nfrac + chaos / 2U);
        unsigned filter = 1U + ((127U - color) >> 4);
        unsigned mode = state.kit[lane].mode & 3U;
        uint32_t ph = v->phase, pm = v->modphase, env = v->env;
        uint32_t dec = loss / (frames ? frames : 1U);
        for (unsigned j = 0; j < frames; ++j) {
            int32_t wave = sine(ph);
            if (mode) {
                uint32_t ratio = mode == 1 ? 4096U : mode == 2 ? 6021U : 11305U;
                uint32_t mod = (uint32_t)((int64_t)sine(pm) * (int32_t)(color + chaos / 3U) * 1024);
                wave = sine(ph + mod);
                pm += (uint32_t)(((uint64_t)inc * ratio) >> 12);
            }
            v->rng ^= v->rng << 13; v->rng ^= v->rng >> 17; v->rng ^= v->rng << 5;
            int32_t noise = (int32_t)(v->rng >> 16) - 32768;
            v->noise_lp += (noise - v->noise_lp) >> filter;
            int32_t filtered = ((state.kit[lane].mode >> 2) & 3U) == 2 ? noise - v->noise_lp
                              : ((state.kit[lane].mode >> 2) & 3U) == 1
                                    ? v->noise_lp - v->previous_noise : v->noise_lp;
            v->previous_noise = v->noise_lp;
            int32_t blend_sample = (wave * (int32_t)(127U - mix) + filtered * (int32_t)mix) / 127;
            int32_t out = (int32_t)(((int64_t)blend_sample * (int32_t)(env >> 8)) >> 10);
            out = (int32_t)(((int64_t)out * v->velocity * controls[KDX_GAIN]) >> 16);
            /* Keep the native Q23 output gain. Previous /4 attenuation made
             * a solo kick inaudible in the device emulator; the summing
             * stage below still saturates safely on simultaneous hits. */
            for (unsigned ch = 0; ch < 2; ++ch) {
                int64_t sum = (int64_t)stereo[j * 2U + ch] + out;
                stereo[j * 2U + ch] = sum > 8388607 ? 8388607 : sum < -8388608 ? -8388608 : (int32_t)sum;
            }
            ph += inc;
            env = env > dec ? env - dec : 0;
        }
        v->phase = ph; v->modphase = pm; v->env = env;
        if (env < 64U) v->alive = 0;
    }
}

#endif /* FM1_KID_UI: original FM1 Open firmware remains unchanged */

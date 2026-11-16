/* SPDX-License-Identifier: GPL-3.0-only
 * Experimental JIANT DRUM-X-inspired audio adapter for FM1 Kids.
 * The parameter layout/factory voices are derived from JIANT FM's
 * GPLv3-only drumx_voice.c (Juan Ignacio Antonucci and Hügelton).
 * This is a fresh adaptation to the FM1 application-level output path,
 * NOT a byte-identical port or a licensed native DSP library replacement.
 */
#ifndef FM1_KIDS_DRUMX_H
#define FM1_KIDS_DRUMX_H
#include <stdint.h>
enum { KDX_KICK, KDX_SNARE, KDX_CLAP, KDX_HAT_C, KDX_HAT_O,
       KDX_TOM, KDX_RIM, KDX_BELL, KDX_LANES };
enum { KDX_MORPH, KDX_PITCH, KDX_DECAY, KDX_CHAOS, KDX_GAIN };
void fm1_kdx_reset(void);
void fm1_kdx_enable(unsigned enabled);
void fm1_kdx_note(unsigned pad, unsigned velocity);
void fm1_kdx_control(unsigned control, unsigned value);
unsigned fm1_kdx_control_get(unsigned control);
void fm1_kdx_mix(int32_t *stereo, unsigned frames);
#endif

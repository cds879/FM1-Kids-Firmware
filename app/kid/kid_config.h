/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_KID_CONFIG_H
#define FM1_KID_CONFIG_H
/* 1: the FM1 Kids interface (app/kid/kid_screen.inc) replaces M-VAVE's screen.
 * 0: M-VAVE's original FM1 Open interface. The flash is nearly full, so only
 * one of the two fits in a build. Firmware updates are handled by the SDK
 * before either interface runs, so both keep the update path. */
#ifndef FM1_KID_UI
#define FM1_KID_UI 1
#endif

/* Highest MASTER knob reading passed on (0..1023): caps the loudness for
 * young ears. 1023 turns the cap off. */
#ifndef KID_MASTER_LIMIT
#define KID_MASTER_LIMIT 760U
#endif

/* Shown at startup and in the parent panel. The update identity (what the
 * workbench and updater see) is FM1_UPDATE_PRODUCT in app/version.h. */
#define KID_VERSION "0.1"

/* Pattern length the kid interface keeps every track at (one row of keys). */
#define KID_STEPS 16U
#endif

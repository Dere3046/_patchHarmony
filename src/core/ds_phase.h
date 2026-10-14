// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_DS_PHASE_H
#define DROID_LKM_DS_PHASE_H

/*
 * The two halves that used to be their own ko. droid_lkm_compat.ko carried the
 * OPPO ghost fixup, droid_lkm_misc.ko carried devtmpfs and the fake user
 * namespace, and both linked a hook engine of their own: three engines, three
 * patch ledgers, one kernel text. They are phases of the one module now.
 *
 * Each is linked in only where the Makefile's probe says its objects compile,
 * so the CONFIG_ name here is the same one the Makefile defines and a target
 * without them gets the empty stubs below instead of a link error.
 *
 * Order matters and droid_lkm_init() is the only caller:
 *   compat wants the hook engine, so it runs after hk_init() and after the
 *   policy has been applied.
 *   misc hangs its namespace kinds off the /proc work droid_lkm_proc_init()
 *   sets up, so it runs after that.
 */

#ifdef CONFIG_DROID_LKM_COMPAT
int droid_lkm_compat_phase_init(void);
void droid_lkm_compat_phase_exit(void);
#else
static inline int droid_lkm_compat_phase_init(void) { return 0; }
static inline void droid_lkm_compat_phase_exit(void) { }
#endif

#ifdef CONFIG_DROID_LKM_MISC
int droid_lkm_misc_phase_init(void);
void droid_lkm_misc_phase_exit(void);
#else
static inline int droid_lkm_misc_phase_init(void) { return 0; }
static inline void droid_lkm_misc_phase_exit(void) { }
#endif

#endif /* DROID_LKM_DS_PHASE_H */

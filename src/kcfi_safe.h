// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * Clang Control Flow Integrity compatibility.
 *
 * Linux 6.1+ enables -fsanitize=kcfi when CONFIG_CFI_CLANG=y.
 * The standard kernel definition of __nocfi in older headers only suppresses
 * traditional "cfi" and omits "kcfi". Redefine __nocfi to suppress both
 * so functions tagged with __nocfi will not emit fatal brk #0x8228 traps
 * when branching into runtime-generated trampoline thunks.
 *
 * For kernels before 6.1, "kcfi" is unknown to older Clang compilers,
 * so we only apply "cfi".
 */

#ifndef DROID_LKM_KCFI_SAFE_H
#define DROID_LKM_KCFI_SAFE_H

#include <linux/version.h>

#ifdef __clang__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
#undef __nocfi
#define __nocfi __attribute__((__no_sanitize__("cfi", "kcfi")))
#else
#undef __nocfi
#define __nocfi __attribute__((__no_sanitize__("cfi")))
#endif
#endif

#endif /* DROID_LKM_KCFI_SAFE_H */

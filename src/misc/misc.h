// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_MISC_H
#define DROID_LKM_MISC_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "core.h"
#include "ds_compat.h"

#define DROID_LKM_MISC_TAG "droid_lkm_misc"

#define droid_lkm_misc_info(fmt, ...) pr_info("[" DROID_LKM_MISC_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_misc_warn(fmt, ...) pr_warn("[" DROID_LKM_MISC_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_misc_err(fmt, ...) pr_err("[" DROID_LKM_MISC_TAG "] " fmt, ##__VA_ARGS__)

extern bool droid_lkm_misc_verbose;
#define droid_lkm_misc_dbg(fmt, ...)                                                 \
	do {                                                                         \
		if (droid_lkm_misc_verbose)                                          \
			pr_info("[" DROID_LKM_MISC_TAG "/dbg] " fmt, ##__VA_ARGS__); \
	} while (0)

/*
 * the GKI gate rejects part of the export list, so unexported and risky symbols
 * are resolved by name. the lookup itself is the shared one: it falls back to the
 * library's own kallsyms walk when the kernel hides an address, and it refuses a
 * hit inside this module that is not one of the module's own wrappers -- a shim
 * defined under a kernel name would otherwise resolve to itself
 */
static inline unsigned long __nocfi droid_lkm_misc_sym(const char *name)
{
	return ds_compat_sym(name);
}

#endif

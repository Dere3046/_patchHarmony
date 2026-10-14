// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_MISC_H
#define DROID_LKM_MISC_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>

#include "core.h"
#include "ds.h"

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
 * the GKI gate rejects part of the export list, so unexported and risky symbols are resolved by name.
 *
 * the resolver falls back to the module symbol tables when the kernel does not carry a
 * name, and this module defines shims under the kernel's own names (lookup_one_len and
 * vfs_unlink are two that the port resolves by name), so a name the kernel lost comes
 * back as the shim defined here. droid_lkm_sym() has always refused an address that
 * lands in this module for exactly that reason; misc resolves the same kind of names and
 * had no such rule, which hands a caller its own shim instead of the kernel symbol.
 *
 * what it has to refuse is a name the module carries for the kernel, so the test is the
 * prefix the module's own symbols share -- its tag -- and not the misc tag. the misc half
 * also looks up droid_lkm_ns_kind_register, an export of the main half, by name; that
 * resolved inside the module, matched the narrower tag, and came back 0. the device log
 * shows the cost: "userns unavailable: -19" on a kernel where the symbol was right there.
 */
static inline unsigned long __nocfi droid_lkm_misc_sym(const char *name)
{
	unsigned long addr = kallrecon_klp(name);

	if (addr && within_module(addr, THIS_MODULE) &&
	    strncmp(name, DROID_LKM_TAG, sizeof(DROID_LKM_TAG) - 1))
		return 0;
	return addr;
}

#endif

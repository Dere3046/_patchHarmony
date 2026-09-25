// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DROID_LKM_H
#define DROID_LKM_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "core.h"

#define DROID_LKM_TAG "droid_lkm"

#define droid_lkm_info(fmt, ...) pr_info("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_warn(fmt, ...) pr_warn("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_err(fmt, ...) pr_err("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)

extern bool droid_lkm_verbose;
#define droid_lkm_dbg(fmt, ...)                                                      \
	do {                                                                   \
		if (droid_lkm_verbose)                                                \
			pr_info("[" DROID_LKM_TAG "/dbg] " fmt, ##__VA_ARGS__);        \
	} while (0)

// kallsyms bootstrap, module symbols via module_kallsyms fallback
static inline unsigned long __nocfi droid_lkm_sym(const char *name)
{
	return kallrecon_klp(name);
}

void droid_lkm_reset_task_ns_refs(void);
bool droid_lkm_any_task_ns_refs(void);

void droid_lkm_task_ipc_deferred_run(void);
void droid_lkm_keepalive_pin(void);
void droid_lkm_keepalive_sync(void);
void droid_lkm_keepalive_release(void);

#endif

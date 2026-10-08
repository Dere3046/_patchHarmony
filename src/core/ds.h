// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DROID_LKM_H
#define DROID_LKM_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>

#include "core.h"
#include "hk_patch.h"
#include "hk_inline.h"

struct hk_inline;

#define DROID_LKM_TAG "droid_lkm"

/* ipc side, refuses to load when the target kernel has no locked unmap entry */
int droid_lkm_munmap_init(void);

#define droid_lkm_info(fmt, ...) pr_info("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_warn(fmt, ...) pr_warn("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_err(fmt, ...) pr_err("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)

extern bool droid_lkm_verbose;
#define droid_lkm_dbg(fmt, ...)                                                      \
	do {                                                                   \
		if (droid_lkm_verbose)                                                \
			pr_info("[" DROID_LKM_TAG "/dbg] " fmt, ##__VA_ARGS__);        \
	} while (0)

/*
 * kallsyms bootstrap, module symbols via module_kallsyms fallback
 *
 * the shims in this module are deliberately defined under the kernel's own
 * names, and that fallback searches the module tables too, so a kernel that
 * does not carry the name hands back the shim defined here instead. that is not
 * the kernel symbol: a shim whose body calls the resolved pointer calls itself.
 * the hook wrappers are this module's own symbols by design and carry the
 * module prefix, everything else that lands inside the module is a shim
 */
static inline unsigned long __nocfi droid_lkm_sym(const char *name)
{
	unsigned long addr = kallrecon_klp(name);

	if (addr && within_module(addr, THIS_MODULE) &&
	    strncmp(name, DROID_LKM_TAG, sizeof(DROID_LKM_TAG) - 1))
		return 0;
	return addr;
}

void droid_lkm_reset_task_ns_refs(void);
bool droid_lkm_any_task_ns_refs(void);

void droid_lkm_task_ipc_deferred_run(void);
void droid_lkm_keepalive_pin(void);
void droid_lkm_keepalive_sync(void);
void droid_lkm_keepalive_release(void);

#endif

/*
 * every text write of this module goes through the kernel's own patch
 * primitive. the fixmap slot path stores through an alias we compute ourselves
 * and MediaTek kernel protection trips on that store, which is what took the
 * device down. the path is pinned in hk_cfg.write at init, so no call site can
 * fall back to the slot path by accident
 */
static inline int droid_lkm_patch_write(void *dst, unsigned long val)
{
	return hk_patch_write(dst, val);
}

/*
 * inline hooks rewrite the entry of a live kernel function. the device owner
 * suspects the hypervisor refuses that store on MTK, so they are off by default
 * while everything else (syscall table, ids, sysctls) stays on. inline_hook=1
 * brings them back. a refused hook is not fatal: every caller already degrades
 */
extern bool droid_lkm_inline_hooks_on;

int droid_lkm_do_inline_hook(struct hk_inline *h, const char *sym,
			     const char *wrap);
int droid_lkm_hook_install(struct hk_inline *h, const char *sym,
			   const char *wrap);
int droid_lkm_hook_install_critical(struct hk_inline *h, const char *sym,
				    const char *wrap);
bool droid_lkm_hook_critical_ok(void);
bool droid_lkm_degrade_enabled(void);
void droid_lkm_hook_report(void);
void droid_lkm_hook_policy_apply(void);


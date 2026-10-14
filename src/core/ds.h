// SPDX-License-Identifier: GPL-2.0-only

#include <linux/version.h>
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

/* reached indirectly so the entry needs a bti landing pad on a real device */
#define DROID_LKM_ENTRY __attribute__((target("branch-protection=bti")))

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
	/*
	 * one entry for data writes: the kernel fixmap slot first then the library
	 * slot and the caller hears about it when neither lands
	 */
	struct hk_write w = { .nofault = true, .verify = true };
	int ret;

	w.slot = HK_SLOT_KERNEL;
	ret = hk_write_run(dst, &val, sizeof(val), &w);
	if (!ret)
		return 0;

	w.slot = HK_SLOT_LIBRARY;
	return hk_write_run(dst, &val, sizeof(val), &w);
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
#include "hk_redir.h"

int droid_lkm_hook_install_auto(struct hk_inline *h, struct hk_redir *r,
				const char *sym, const char *wrap);
int droid_lkm_hook_install(struct hk_inline *h, const char *sym,
			   const char *wrap);
int droid_lkm_hook_install_critical(struct hk_inline *h, const char *sym,
				    const char *wrap);
bool droid_lkm_hook_critical_ok(void);
bool droid_lkm_degrade_enabled(void);
void droid_lkm_hook_report(void);
void droid_lkm_hook_policy_apply(void);

/*
 * 6.18 moved the namespace type out of proc_ns_operations and into
 * ns_common::ns_type. every place that hands a namespace its operations goes
 * through this, so no call site carries the difference
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define droid_lkm_ns_set_ops(_ns, _ops, _type)                                 \
	do {                                                                   \
		(_ns)->ops = (_ops);                                           \
		(_ns)->ns_type = (_type);                                      \
	} while (0)
#else
#define droid_lkm_ns_set_ops(_ns, _ops, _type)                                 \
	do {                                                                   \
		(_ns)->ops = (_ops);                                           \
	} while (0)
#endif

/*
 * 6.18 hands MODULE_INFO the namespace as a string, before it stringified the
 * token itself, so the call sites name the namespace once and this picks the
 * form the kernel takes
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define DROID_LKM_STR(_x)	#_x
#define DROID_LKM_GKI_NS(_ns)	ANDROID_GKI_##_ns
#define DROID_LKM_IMPORT_NS(_ns)                                               \
	MODULE_IMPORT_NS(#_ns);                                                \
	MODULE_IMPORT_NS(DROID_LKM_STR(DROID_LKM_GKI_NS(_ns)))
#else
#define DROID_LKM_IMPORT_NS(_ns)	MODULE_IMPORT_NS(_ns)
#endif

/*
 * the kernel declares these itself before 6.18 while the shims here define
 * them on every version, so the prototypes live where both can see them
 */
struct dentry;
struct dentry_operations;
struct dentry *lookup_one_len(const char *name, struct dentry *base, int len);
void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op);

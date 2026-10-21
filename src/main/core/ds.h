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
#include "ds_compat.h"
#include "ds_hook.h"
#include "kcfi_safe.h"

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
	/* the compat layer owns the shim guard, see ds_compat_sym */
	return ds_compat_sym(name);
}

void droid_lkm_reset_task_ns_refs(void);
bool droid_lkm_any_task_ns_refs(void);

void droid_lkm_task_ipc_deferred_run(void);
void droid_lkm_keepalive_pin(void);
void droid_lkm_keepalive_sync(void);
void droid_lkm_keepalive_release(void);

/* the compat layer owns the write cascade, so there is one implementation */
static inline int droid_lkm_patch_write(void *dst, unsigned long val)
{
	return ds_compat_write(dst, val);
}


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

#endif /* DROID_LKM_H */

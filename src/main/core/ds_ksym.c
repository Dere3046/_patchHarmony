// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/sched/signal.h>
#include <linux/nsproxy.h>

#include "ds.h"
#include "ds_shapes.h"
#include "ds_ksym.h"

struct droid_lkm_ksym droid_lkm_ks = { };
int *droid_lkm_ks_sysctl_overcommit_memory;
rwlock_t *droid_lkm_ks_tasklist_lock;
const struct proc_ns_operations *droid_lkm_ks_ipcns_operations;

/*
 * the module's kernel symbol table, handed to the compat layer at load. the
 * engine is shared with the other modules, this list is what makes it main's
 */
static const struct ds_compat_sym droid_lkm_syms[] = {
#define DROID_LKM_SYM_ENTRY(field, name) { name, (void **)&droid_lkm_ks.field },
	DROID_LKM_SYM_LIST(DROID_LKM_SYM_ENTRY)
#undef DROID_LKM_SYM_ENTRY
	{ "sysctl_overcommit_memory", (void **)&droid_lkm_ks_sysctl_overcommit_memory },
	{ "tasklist_lock", (void **)&droid_lkm_ks_tasklist_lock },
	{ "ipcns_operations", (void **)&droid_lkm_ks_ipcns_operations },
};

struct user_namespace *droid_lkm_get_user_ns(struct user_namespace *ns)
{
#ifdef CONFIG_USER_NS
	return get_user_ns(ns);
#else
	return droid_lkm_ks.init_user_ns;
#endif
}

struct user_namespace *droid_lkm_current_user_ns(void)
{
#ifdef CONFIG_USER_NS
	return current_cred()->user_ns;
#else
	return droid_lkm_ks.init_user_ns;
#endif
}

__nocfi noinline int droid_lkm_ksym_init(void)
{
	ds_compat_bind(droid_lkm_syms, ARRAY_SIZE(droid_lkm_syms));

	if (droid_lkm_verbose) {
		unsigned long raw_text = kallrecon_klp("_text");

		droid_lkm_dbg("ksym raw: klnum=%u klp=%px _text=0x%lx in_mod=%d _stext=0x%lx _end=0x%lx table=0x%lx/0x%lx\n",
			      klnum_val, kallrecon_klp, raw_text,
			      within_module(raw_text, THIS_MODULE),
			      kallrecon_klp("_stext"), kallrecon_klp("_end"),
			      droid_lkm_ks.text_start, droid_lkm_ks.text_end);
	}

	if (!droid_lkm_ks.proc_alloc_inum || !droid_lkm_ks.proc_free_inum ||
	    !droid_lkm_ks.disable_pid_allocation || !droid_lkm_ks.group_send_sig_info ||
	    !droid_lkm_ks.kernel_wait4) {
		droid_lkm_err("required thunks unresolved\n");
		return -ENODATA;
	}

	return 0;
}

/*
 * the hook engine turns a hooked address into a physical one: _text/_end tell an
 * image address from a module one, kimage_voffset converts the former and
 * vmalloc_to_pfn the latter, __set_fixmap opens the writable alias the copy goes
 * through. the engine resolves them itself and falls back silently, so a kernel
 * that hides _end (a data symbol, gone when CONFIG_KALLSYMS_ALL=n) has every
 * image page translated by vmalloc_to_pfn, which cannot walk a block mapping and
 * returns an unrelated pfn. refuse the inline hooks here instead
 */
bool droid_lkm_text_patch_ready(void)
{
	const struct {
		unsigned long addr;
		const char *name;
	} need[] = {
		{ droid_lkm_ks.text_start, "_text" },
		{ droid_lkm_ks.text_end, "_end" },
		{ droid_lkm_ks.kimage_voffset, "kimage_voffset" },
		{ droid_lkm_ks.vmalloc_to_pfn, "vmalloc_to_pfn" },
		{ droid_lkm_ks.set_fixmap, "__set_fixmap" },
	};
	int i;
	int n = 0;

	for (i = 0; i < ARRAY_SIZE(need); i++)
		if (!need[i].addr)
			n++;
	if (!n)
		return true;

	droid_lkm_warn("text patch path is missing, function hooks take the kprobe route:");
	for (i = 0; i < ARRAY_SIZE(need); i++)
		if (!need[i].addr)
			pr_cont(" %s", need[i].name);
	pr_cont("\n");
	return false;
}

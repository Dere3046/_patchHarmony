// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

/*
 * one place that decides how this module hooks things. the knobs below are the
 * whole policy: whether inline hooks run at all, which write path the engine
 * uses, and whether the engine may open a fixmap slot of its own. anything that
 * changes how we hook belongs here rather than at a call site
 */
#include "ds.h"
#include "hk_patch.h"

bool droid_lkm_inline_hooks_on;

static bool droid_lkm_inline_hook = true;
module_param_named(inline_hook, droid_lkm_inline_hook, bool, 0444);
MODULE_PARM_DESC(inline_hook, "install inline hooks, on by default");

/*
 * 0 keeps the engine on the kernel's own patch primitive, which owns the fixmap
 * slot, the frame and the locking. 1 hands the engine our own fixmap writer:
 * that store goes through an alias this module computes from its build headers,
 * and on the MTK device every engine write then faulted in hk_patch_slot_cb.
 * only pick 1 to reproduce that, never in production
 */
static uint droid_lkm_write_path;
module_param_named(write_path, droid_lkm_write_path, uint, 0444);
MODULE_PARM_DESC(write_path, "0 kernel patch primitive (default), 1 the module's own fixmap writer");


/*
 * the entry before the engine. hk_inline_hook() rewrites the first bytes of a
 * live function so they jump into the wrapper, and what it can drive is a plain
 * entry. on a +lto kernel some of these symbols are thunks instead: the engine
 * then patches the end of a branch chain, its own trampoline loops back into the
 * entry it just patched, and a call recurses until the kernel stack is gone.
 *
 * that is a panic, and with guard=1 armed -- which is the default -- the panic is
 * answered and returned from rather than taking the device down, so the symptom
 * on the device is a frozen screen and a watchdog reboot with no trace anywhere
 * in reach. nothing in the log points at the hook that did it.
 *
 * compat/ghost.c has asked this question since it was written, which is why
 * find_task_by_vpid is the one target that cannot take the device down that way.
 * every other install site went in blind. one call here turns "recurses until the
 * stack is gone" into a refusal that names the symbol and the shape the engine
 * reported, and the feature that wanted the hook degrades instead.
 *
 * a probe that cannot answer is not a refusal: the engine may still drive an
 * entry it cannot describe, which is how ghost.c reads it too
 */
int droid_lkm_hook_entry_ok(const char *sym)
{
	struct hk_inline_probe probe;

	memset(&probe, 0, sizeof(probe));
	if (hk_inline_probe(sym, &probe))
		return 0;

	if (probe.state == HK_INLINE_PLAIN)
		return 0;

	droid_lkm_warn("inline hook refused: %s entry state %d (%s) target 0x%lx, the engine cannot drive that shape\n",
		       sym, probe.state, probe.reason ? probe.reason : "?",
		       probe.target);
	return -EOPNOTSUPP;
}

/*
 * the same question, plus the policy, for the install sites that do not go
 * through droid_lkm_do_inline_hook(). those are the guard and the whole misc
 * half, and they used to call hk_inline_hook() directly: inline_hook=0 turned
 * off the core hooks and left every one of theirs installed, which is a
 * misleading answer to give while bisecting a device that a hook is taking down
 */
int droid_lkm_inline_hook_checked(struct hk_inline *h, const char *sym, const char *wrap)
{
	if (!droid_lkm_inline_hooks_on) {
		pr_warn_once("[droid_lkm] inline hooks are off (inline_hook=0), %s not hooked\n", sym);
		return -EOPNOTSUPP;
	}

	if (droid_lkm_hook_entry_ok(sym))
		return -EOPNOTSUPP;

	return hk_inline_hook(h, sym, wrap);
}

int droid_lkm_do_inline_hook(struct hk_inline *h, const char *sym,
			     const char *wrap)
{
	return droid_lkm_inline_hook_checked(h, sym, wrap);
}

void droid_lkm_hook_policy_apply(void)
{
	droid_lkm_inline_hooks_on = droid_lkm_inline_hook;
	hk_patch_set_write(droid_lkm_write_path ? hk_write_fixmap : NULL);
	droid_lkm_info("hook policy: inline_hook=%d write_path=%u\n",
		       droid_lkm_inline_hooks_on, droid_lkm_write_path);
}

// every install lands here so the report can name the ones that failed
#define DROID_LKM_HOOK_RECORDS		8

static struct {
	const char *sym;
	bool installed;
	int err;
} droid_lkm_hook_records[DROID_LKM_HOOK_RECORDS];
static unsigned int droid_lkm_hook_record_count;

// container critical hooks refuse the load
// the rest refuse their own feature
static bool droid_lkm_hook_critical_missing;

static bool droid_lkm_degrade;
module_param_named(degrade, droid_lkm_degrade, bool, 0444);
MODULE_PARM_DESC(degrade,
	"1 keeps running when a container critical hook cannot install, 0 refuses the load");

int droid_lkm_hook_install_critical(struct hk_inline *h, const char *sym,
				    const char *wrap)
{
	int ret = droid_lkm_hook_install(h, sym, wrap);

	if (ret)
		droid_lkm_hook_critical_missing = true;

	return ret;
}

bool droid_lkm_hook_critical_ok(void)
{
	return !droid_lkm_hook_critical_missing;
}

bool droid_lkm_degrade_enabled(void)
{
	return droid_lkm_degrade;
}

int droid_lkm_hook_install(struct hk_inline *h, const char *sym,
			   const char *wrap)
{
	unsigned int i;
	int ret = droid_lkm_do_inline_hook(h, sym, wrap);

	for (i = 0; i < droid_lkm_hook_record_count; i++) {
		if (droid_lkm_hook_records[i].sym == sym)
			break;
	}
	if (i == droid_lkm_hook_record_count) {
		if (droid_lkm_hook_record_count == DROID_LKM_HOOK_RECORDS)
			return ret;
		droid_lkm_hook_record_count++;
	}

	droid_lkm_hook_records[i].sym = sym;
	droid_lkm_hook_records[i].installed = !ret && h->orig;
	droid_lkm_hook_records[i].err = ret;

	return ret;
}

void droid_lkm_hook_report(void)
{
	unsigned int i, installed = 0;

	for (i = 0; i < droid_lkm_hook_record_count; i++) {
		if (droid_lkm_hook_records[i].installed)
			installed++;
		droid_lkm_info("hook: %s %s (%d)\n", droid_lkm_hook_records[i].sym,
			       droid_lkm_hook_records[i].installed ?
			       "installed" : "NOT installed",
			       droid_lkm_hook_records[i].err);
	}
	droid_lkm_info("hooks: inline=%d %u/%u installed\n",
		       droid_lkm_inline_hooks_on, installed,
		       droid_lkm_hook_record_count);
}

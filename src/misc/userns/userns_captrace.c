// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * Capability decision trace.
 *
 * cap_capable() denies every check that targets a namespace above the
 * caller's own. That level test is what makes a fake user namespace an
 * isolation boundary, and it is also what hides the decisions the kernel
 * still compiles against current_user_ns() or against an owner it filled in
 * through the get_user_ns() stub: those arrive here with the initial
 * namespace as the target and are refused for a container task, no matter
 * which capability it holds.
 *
 * The trace prints each refusal with the name of the function that asked, so
 * a decision that still needs translating shows up as a log line instead of
 * a guess. Diagnostic only, opt in with captrace=1.
 */

#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/atomic.h>
#include <linux/limits.h>

#include "misc.h"
#include "misc_ksym.h"
#include "ds.h"
#include "userns.h"
#include "hk_inline.h"

bool droid_lkm_userns_captrace_on;

static struct hk_inline droid_lkm_userns_cap_hook;
static int (*droid_lkm_userns_cap_orig)(const struct cred *cred,
					struct user_namespace *targ_ns,
					int cap, unsigned int opts);
static atomic_t droid_lkm_userns_cap_busy = ATOMIC_INIT(0);

/*
 * the caller address is read first, before anything else runs, and the busy flag keeps the
 * trace out of its own recursion
 */
__nocfi noinline int droid_lkm_userns_cap_wrap(const struct cred *cred,
					       struct user_namespace *targ_ns,
					       int cap, unsigned int opts)
{
	unsigned long caller = (unsigned long)__builtin_return_address(0);
	int (*orig)(const struct cred *, struct user_namespace *, int,
		    unsigned int);
	int ret;

	/*
	 * a call landing here between the patch and the trampoline is refused, so the window can
	 * never become a call through a null pointer
	 */
	orig = droid_lkm_userns_cap_orig;
	if (!orig)
		return -EPERM;

	ret = orig(cred, targ_ns, cap, opts);

	if (ret && cred && cred->user_ns != droid_lkm_userns_init_uns &&
	    !atomic_xchg(&droid_lkm_userns_cap_busy, 1)) {
		char name[128] = "<unknown>";
		/*
		 * frame 0 is the lsm wrapper (security_capable), which names nothing useful.
		 * one frame further is the function that actually asked, and that is the one
		 * worth reading. it is only safe to walk there when the kernel was built with
		 * frame pointers, so say so instead of guessing when it was not.
		 */
#ifdef CONFIG_FRAME_POINTER
		unsigned long outer = (unsigned long)__builtin_return_address(1);
		char oname[128] = "<unknown>";

		sym_name_at(outer, oname, sizeof(oname));
#else
		unsigned long outer = 0;
		const char *oname = "<no frame pointers>";
#endif

		sym_name_at(caller, name, sizeof(name));
		droid_lkm_misc_info("cap denied: cap=%d targ=%s caller=%s @0x%lx by=%s @0x%lx\n",
			cap,
			targ_ns == droid_lkm_userns_init_uns ? "init" : "other",
			name, caller, oname, outer);
		atomic_set(&droid_lkm_userns_cap_busy, 0);
	}

	return ret;
}

int droid_lkm_userns_captrace_init(void)
{
	int ret;

	if (!droid_lkm_userns_captrace_on)
		return 0;

	if (!droid_lkm_misc_ks.cap_capable) {
		droid_lkm_misc_warn("cap trace: cap_capable not found\n");
		return -ENOENT;
	}

	if (!droid_lkm_userns_init_uns)
		return -ENOENT;

	ret = droid_lkm_inline_hook_checked(&droid_lkm_userns_cap_hook, "cap_capable",
				    "droid_lkm_userns_cap_wrap");
	if (ret) {
		droid_lkm_misc_warn("cap trace: hook failed: %d\n", ret);
		return ret;
	}

	/* orig has to be the trampoline, never the symbol */
	droid_lkm_userns_cap_orig = (void *)droid_lkm_userns_cap_hook.orig;
	droid_lkm_misc_info("cap trace on\n");
	return 0;
}

void droid_lkm_userns_captrace_exit(void)
{
	if (!droid_lkm_userns_cap_orig)
		return;

	hk_inline_unhook(&droid_lkm_userns_cap_hook);
	droid_lkm_userns_cap_orig = NULL;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

/*
 * The blast wall.
 *
 * A panic on this device costs a reboot and leaves nothing behind: /sys/fs/pstore
 * is not there, and the ring buffer that held the trace goes with the reset. A
 * module that takes the kernel down therefore says only that it did.
 *
 * This answers panic() instead of letting it through. The message is rendered
 * to the log, the stack is dumped, and control returns to the caller.
 *
 * Returning is the point. oops_end() calls panic() when panic_on_oops is set
 * and falls through to make_task_dead() when it is not, so a panic that returns
 * lands on that same make_task_dead(): the faulting task dies and the device
 * lives on with the trace still in dmesg. That is what a kernel booted with
 * panic_on_oops=0 would already do, reached without touching the boot config.
 *
 * Two limits worth knowing before trusting it.
 *
 *   - It is armed from droid_lkm_init(), so it can only cover what runs after
 *     it. Everything earlier in that function -- kallsyms recovery, the symbol
 *     table, layout, caps, the engine itself -- is the code the previous build
 *     already ran on this device, so the uncovered part is the part that is
 *     known to work. A panic from there still reboots.
 *
 *   - A reset taken by the hypervisor is not a panic. Nothing in software runs
 *     for that one, this hook included, which is why it is worth trying the
 *     panic_on_oops and panic sysctls first: they cost nothing and they tell
 *     the two cases apart.
 *
 * guard=0 puts the real panic back, which is what to use once the fault is
 * understood: a wall that never falls is a device that never reboots.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched/debug.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#include <linux/stdarg.h>
#else
#include <stdarg.h>
#endif

#include "ds.h"
#include "ds_ksym.h"
#include "hk_inline.h"

static bool droid_lkm_guard_on = true;
module_param_named(guard, droid_lkm_guard_on, bool, 0444);
MODULE_PARM_DESC(guard, "answer panic() with a backtrace instead of taking the device down, default 1");

static struct hk_inline droid_lkm_guard_hook;
static bool droid_lkm_guard_armed;
static unsigned int droid_lkm_guard_hits;

/*
 * the entry panic() is redirected to. variadic like the original, so the caller
 * passes the same arguments it always did and the format string can be rendered
 * here; the original is never called, its message would only repeat what the
 * oops path printed on its way in
 */
__nocfi noinline void droid_lkm_guard_swallow(const char *fmt, ...)
{
	va_list args;
	char buf[256];

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	droid_lkm_guard_hits++;

	pr_emerg("[droid_lkm_guard] panic #%u answered, not taken down: %s\n",
		 droid_lkm_guard_hits, buf);
	dump_stack();
	pr_emerg("[droid_lkm_guard] end of trace, control returns to the caller\n");
}

int droid_lkm_guard_arm(void)
{
	int ret;

	if (droid_lkm_guard_armed)
		return 0;

	if (!droid_lkm_guard_on) {
		droid_lkm_info("guard off, a panic takes the device down\n");
		return 0;
	}

	/*
	 * the engine refuses to translate an image address when the text patch
	 * path is incomplete, and a wrong translation is a write to a page that
	 * belongs to someone else. arming the wall through that path would be
	 * the exact fault it exists to report
	 */
	if (!droid_lkm_text_patch_ready()) {
		droid_lkm_warn("guard: the text patch path is incomplete, not arming\n");
		return -EOPNOTSUPP;
	}

	ret = hk_inline_hook(&droid_lkm_guard_hook, "panic",
			     "droid_lkm_guard_swallow");
	if (ret) {
		droid_lkm_warn("guard: hooking panic failed: %d, a panic will still reboot\n",
			       ret);
		return ret;
	}

	droid_lkm_guard_armed = true;
	droid_lkm_info("guard armed: panic prints and returns, guard=0 restores it\n");

	return 0;
}

void droid_lkm_guard_disarm(void)
{
	if (!droid_lkm_guard_armed)
		return;

	droid_lkm_guard_armed = false;
	hk_inline_unhook(&droid_lkm_guard_hook);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * This was droid_lkm_misc.ko: the container scoped features the stock GKI was
 * built without, devtmpfs and a fake user namespace. It carries no hook engine,
 * no kallsyms base and no module_init of its own now -- droid_lkm_init() calls
 * it once the engine is up and once the /proc work its namespace kinds hang off
 * has run.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "core.h"
#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "xt_reg.h"
#include "devtmpfs/devtmpfs.h"
#include "userns/userns.h"
#include "ds_phase.h"

bool droid_lkm_misc_verbose;

static bool droid_lkm_misc_xt_enable = true;
static bool droid_lkm_misc_userns_enable = true;
static bool droid_lkm_misc_devtmpfs_enable = true;

/* misc_verbose and not verbose: the main module already owns that name */
module_param_named(misc_verbose, droid_lkm_misc_verbose, bool, 0444);
MODULE_PARM_DESC(misc_verbose, "log symbol probes and every registered item");

module_param_named(xt, droid_lkm_misc_xt_enable, bool, 0444);
MODULE_PARM_DESC(xt, "register the ported xt matches and targets");

module_param_named(userns, droid_lkm_misc_userns_enable, bool, 0444);
MODULE_PARM_DESC(userns, "offer the /proc/<pid>/ns/user entry and the fake user namespace");

module_param_named(devtmpfs, droid_lkm_misc_devtmpfs_enable, bool, 0444);
MODULE_PARM_DESC(devtmpfs, "offer the devtmpfs filesystem the kernel was built without");

module_param_named(captrace, droid_lkm_userns_captrace_on, bool, 0444);
MODULE_PARM_DESC(captrace, "log every capability decision refused for a container task");

static bool droid_lkm_misc_userns_up;
static bool droid_lkm_misc_up;

/*
 * write 1 to drop devtmpfs' internal mount, which is what keeps a module reference alive
 * and rmmod refused
 */
static int droid_lkm_misc_release_set(const char *val, const struct kernel_param *kp)
{
	bool on;

	if (kstrtobool(val, &on) || !on)
		return -EINVAL;

	return droid_lkm_misc_devtmpfs_release();
}

static const struct kernel_param_ops droid_lkm_misc_release_ops = {
	.set	= droid_lkm_misc_release_set,
};

module_param_cb(release, &droid_lkm_misc_release_ops, NULL, 0200);
MODULE_PARM_DESC(release, "write 1 to drop the devtmpfs internal mount before unloading");

int droid_lkm_misc_phase_init(void)
{
	int ret;

	droid_lkm_misc_info("phase up\n");
	droid_lkm_misc_ksym_probe();

	ret = droid_lkm_misc_ksym_init();
	if (ret) {
		/*
		 * a thunk this half needs is not in this kernel. report and stay
		 * out of the way: devtmpfs and the fake user namespace are
		 * additions, and they must not take ipcns down with them now
		 * that they are linked into the same ko
		 */
		droid_lkm_misc_report("misc", "unsupported", "thunk table incomplete");
		return 0;
	}
	droid_lkm_misc_up = true;

	ret = droid_lkm_misc_proc_init();
	if (ret)
		droid_lkm_misc_warn("control plane unavailable: %d\n", ret);

	if (droid_lkm_misc_xt_enable) {
		ret = droid_lkm_misc_xt_init();
		if (ret)
			droid_lkm_misc_warn("xt registration failed: %d\n", ret);
	} else {
		droid_lkm_misc_report("xt", "off", "xt=0");
	}

	if (droid_lkm_misc_devtmpfs_enable) {
		ret = droid_lkm_misc_devtmpfs_init();
		if (ret)
			droid_lkm_misc_warn("devtmpfs unavailable: %d\n", ret);
	} else {
		droid_lkm_misc_report("devtmpfs", "off", "devtmpfs=0");
	}

	if (droid_lkm_misc_userns_enable) {
		ret = droid_lkm_userns_init();
		if (ret)
			droid_lkm_misc_warn("userns unavailable: %d\n", ret);
		else
			droid_lkm_misc_userns_up = true;
	} else {
		droid_lkm_misc_report("userns", "off", "userns=0");
	}

	return 0;
}

void droid_lkm_misc_phase_exit(void)
{
	if (!droid_lkm_misc_up)
		return;

	droid_lkm_misc_up = false;

	if (droid_lkm_misc_userns_up) {
		droid_lkm_userns_exit();
		droid_lkm_misc_userns_up = false;
	}
	droid_lkm_misc_devtmpfs_exit();
	droid_lkm_misc_xt_exit();
	droid_lkm_misc_proc_exit();
	droid_lkm_misc_info("phase down\n");
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/init.h>

#include "core.h"
#include "hk.h"
#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "xt_reg.h"
#include "devtmpfs/devtmpfs.h"
#include "userns/userns.h"

bool droid_lkm_misc_verbose;

static unsigned long __nocfi droid_lkm_misc_hk_resolve(const char *name)
{
	return droid_lkm_misc_sym(name);
}


static const struct hk_cfg droid_lkm_misc_hk_cfg = {
	.resolve = droid_lkm_misc_hk_resolve,
};

static bool droid_lkm_misc_xt_enable = true;
static bool droid_lkm_misc_userns_enable = true;
static bool droid_lkm_misc_devtmpfs_enable = true;

module_param_named(verbose, droid_lkm_misc_verbose, bool, 0444);
MODULE_PARM_DESC(verbose, "log symbol probes and every registered item");

module_param_named(xt, droid_lkm_misc_xt_enable, bool, 0444);
MODULE_PARM_DESC(xt, "register the ported xt matches and targets");

module_param_named(userns, droid_lkm_misc_userns_enable, bool, 0444);
MODULE_PARM_DESC(userns, "offer the /proc/<pid>/ns/user entry and the fake user namespace");

module_param_named(devtmpfs, droid_lkm_misc_devtmpfs_enable, bool, 0444);
MODULE_PARM_DESC(devtmpfs, "offer the devtmpfs filesystem the kernel was built without");

module_param_named(captrace, droid_lkm_userns_captrace_on, bool, 0444);
MODULE_PARM_DESC(captrace, "log every capability decision refused for a container task");

static bool droid_lkm_misc_userns_up;
static bool droid_lkm_misc_hk_up;

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

static int __init droid_lkm_misc_init(void)
{
	int ret;

	find_kallsyms_base();
	if (!klnum_val || !kallrecon_klp) {
		droid_lkm_misc_err("kallsyms recovery failed\n");
		return -ENODATA;
	}

	droid_lkm_misc_info("loaded\n");
	droid_lkm_misc_ksym_probe();

	ret = droid_lkm_misc_ksym_init();
	if (ret) {
		droid_lkm_misc_err("thunk table incomplete: %d\n", ret);
		return ret;
	}

	ret = hk_init(&droid_lkm_misc_hk_cfg);
	if (ret) {
		droid_lkm_misc_err("hk_init failed: %d\n", ret);
		return ret;
	}
	droid_lkm_misc_hk_up = true;

	ret = droid_lkm_misc_proc_init();
	if (ret)
		droid_lkm_misc_warn("control plane unavailable: %d\n", ret);

	if (droid_lkm_misc_xt_enable) {
		ret = droid_lkm_misc_xt_init();
		if (ret)
			goto err_proc;
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

err_proc:
	droid_lkm_misc_proc_exit();
	if (droid_lkm_misc_hk_up) {
		hk_exit();
		droid_lkm_misc_hk_up = false;
	}
	return ret;
}

static void __exit droid_lkm_misc_exit(void)
{
	if (droid_lkm_misc_userns_up) {
		droid_lkm_userns_exit();
		droid_lkm_misc_userns_up = false;
	}
	droid_lkm_misc_devtmpfs_exit();
	droid_lkm_misc_xt_exit();
	droid_lkm_misc_proc_exit();
	if (droid_lkm_misc_hk_up) {
		hk_exit();
		droid_lkm_misc_hk_up = false;
	}
	droid_lkm_misc_info("unloaded\n");
}

module_init(droid_lkm_misc_init);
module_exit(droid_lkm_misc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("container scoped kernel features missing on the stock GKI build");

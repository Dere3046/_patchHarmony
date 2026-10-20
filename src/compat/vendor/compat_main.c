// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "core.h"
#include "ds_compat.h"
#include "ds_hook.h"
#include "hk.h"
#include "ghost.h"
#include "ddl_hit.h"
#include "vendor_syms.h"

static unsigned long __nocfi dlc_hk_resolve(const char *name)
{
	return ds_compat_sym(name);
}

static const struct hk_cfg dlc_hk_cfg = {
	.resolve = dlc_hk_resolve,
};

static int __init droid_lkm_vendor_init(void)
{
	int ret;

	ret = ds_compat_init("droid_lkm_vendor", "dlc_");
	if (ret)
		return ret;
	dlc_vendor_syms_init();

	ret = hk_init(&dlc_hk_cfg);
	if (ret) {
		pr_err("[droid_lkm_vendor] hk_init failed: %d\n", ret);
		return ret;
	}

	ds_hook_policy_apply();

	// the fixup is why this module exists
	// a silent miss leaves vendor callers with NULL
	ret = dlc_ghost_init();

	/* vendor ddl history bounds, data only */
	(void)dlc_ddl_hit_init();
	if (ret) {
		pr_err("[droid_lkm_vendor] ghost: refusing to load, hook not installed (%d); ghost=0 turns the fixup off\n",
		       ret);
		hk_exit();
		return ret;
	}
	pr_info("[droid_lkm_vendor] ready\n");
	return 0;
}

static void __exit droid_lkm_vendor_exit(void)
{
	dlc_ddl_hit_exit();
	dlc_ghost_exit();
	ds_hook_exit();
	hk_exit();
}

module_init(droid_lkm_vendor_init);
module_exit(droid_lkm_vendor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dere3046");
MODULE_DESCRIPTION("vendor compatibility fixups");

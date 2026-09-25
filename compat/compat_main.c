// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "core.h"
#include "hk.h"
#include "ghost.h"
#ifdef CONFIG_DROID_LKM_SELFTEST
#include "selftest.h"
#endif

static unsigned long __nocfi dlc_hk_resolve(const char *name)
{
	return kallrecon_klp ? kallrecon_klp(name) : 0;
}

static const struct hk_cfg dlc_hk_cfg = {
	.resolve = dlc_hk_resolve,
};

static int __init droid_lkm_compat_init(void)
{
	int ret;

	find_kallsyms_base();
	if (!klnum_val || !kallrecon_klp) {
		pr_err("[droid_lkm_compat] kallsyms recovery failed\n");
		return -ENODATA;
	}

	ret = hk_init(&dlc_hk_cfg);
	if (ret) {
		pr_err("[droid_lkm_compat] hk_init failed: %d\n", ret);
		return ret;
	}

	dlc_ghost_init();

#ifdef CONFIG_DROID_LKM_SELFTEST
	dlc_selftest_init();
#endif
	pr_info("[droid_lkm_compat] ready\n");
	return 0;
}

static void __exit droid_lkm_compat_exit(void)
{
#ifdef CONFIG_DROID_LKM_SELFTEST
	dlc_selftest_exit();
#endif
	dlc_ghost_exit();
	hk_exit();
}

module_init(droid_lkm_compat_init);
module_exit(droid_lkm_compat_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dere3046");
MODULE_DESCRIPTION("OPPO vendor quirk fixups");

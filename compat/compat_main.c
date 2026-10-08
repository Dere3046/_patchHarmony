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

bool dlc_inline_hooks_on;
static bool dlc_inline_hook = true;
module_param_named(inline_hook, dlc_inline_hook, bool, 0444);
MODULE_PARM_DESC(inline_hook, "install inline hooks, on by default");

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

	/* the vendor fixup is the inline hook, so the policy travels with it */
	dlc_inline_hooks_on = dlc_inline_hook;
	pr_info("[droid_lkm_compat] hook policy: inline_hook=%d\n",
		dlc_inline_hooks_on);

	// the fixup is why this module exists
	// a silent miss leaves vendor callers with NULL
	ret = dlc_ghost_init();
	if (ret) {
		pr_err("[droid_lkm_compat] ghost: refusing to load, hook not installed (%d); ghost=0 turns the fixup off\n",
		       ret);
		hk_exit();
		return ret;
	}

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

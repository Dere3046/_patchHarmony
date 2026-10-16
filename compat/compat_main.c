// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "core.h"
#include "ds.h"
#include "ds_phase.h"
#include "ghost.h"
#ifdef CONFIG_DROID_LKM_SELFTEST
#include "selftest.h"
#endif

/*
 * this was droid_lkm_compat.ko, with a kallsyms base, a hook engine and an
 * inline hook policy of its own. linked into droid_lkm.ko it has none of those:
 * the base is recovered once by the main module, the engine is already up when
 * this runs, and the one policy droid_lkm_hook_policy_apply() settled is the
 * one the fixup rides, which is why there is no inline_hook parameter here any
 * more -- two module_params can not share a name, and the main module owns it.
 */
bool dlc_inline_hooks_on;

static bool droid_lkm_compat_up;

int droid_lkm_compat_phase_init(void)
{
	int ret;

	dlc_inline_hooks_on = droid_lkm_inline_hooks_on;
	pr_info("[droid_lkm_compat] hook policy: inline_hook=%d\n",
		dlc_inline_hooks_on);

	/*
	 * the fixup is why this half exists and a silent miss leaves vendor
	 * callers with NULL, which is why it used to refuse to load. in this
	 * module a refusal would take ipcns and the userns half down with a
	 * vendor quirk, so it reports instead and the caller keeps the module
	 */
	ret = dlc_ghost_init();
	if (ret) {
		pr_warn("[droid_lkm_compat] the vendor fixup did not install (%d), those callers keep the kernel's NULL; ghost=0 turns the attempt off\n",
			ret);
		return ret;
	}

	droid_lkm_compat_up = true;
#ifdef CONFIG_DROID_LKM_SELFTEST
	dlc_selftest_init();
#endif
	pr_info("[droid_lkm_compat] ready\n");
	return 0;
}

void droid_lkm_compat_phase_exit(void)
{
	if (!droid_lkm_compat_up)
		return;

	droid_lkm_compat_up = false;
#ifdef CONFIG_DROID_LKM_SELFTEST
	dlc_selftest_exit();
#endif
	dlc_ghost_exit();
}

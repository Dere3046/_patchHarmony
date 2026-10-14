// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/types.h>
#include <linux/netfilter/x_tables.h>

#include "misc.h"
#include "xt_reg.h"
#include "misc_proc.h"

/*
 * registration manifest, every ported match or target file exposes an init and exit pair and
 * adds one line here
 */
struct droid_lkm_misc_xt_item {
	const char *name;
	int (*init)(void);
	void (*exit)(void);
};

extern int droid_lkm_misc_xt_addrtype_init(void);
extern void droid_lkm_misc_xt_addrtype_exit(void);

static const struct droid_lkm_misc_xt_item droid_lkm_misc_xt_items[] = {
	{ "addrtype", droid_lkm_misc_xt_addrtype_init, droid_lkm_misc_xt_addrtype_exit },
};

static unsigned int droid_lkm_misc_xt_registered;

/*
 * which entries actually armed. the count above is what the report prints, but
 * teardown has to walk the items, not the count: an item that failed its init
 * is skipped and the one after it still arms, so indexing exit() by the count
 * would call exit on an item that never ran init and never call it on the one
 * that did. harmless with a single item, wrong the moment a second one lands
 */
static bool droid_lkm_misc_xt_started[ARRAY_SIZE(droid_lkm_misc_xt_items)];

int droid_lkm_misc_xt_init(void)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(droid_lkm_misc_xt_items); i++) {
		ret = droid_lkm_misc_xt_items[i].init();
		if (ret) {
			/*
			 * one match that can not arm is that match, not the ko.
			 * a target kernel missing its symbols drops it here and
			 * the report below says unsupported, without taking the
			 * rest of the module down with it
			 */
			droid_lkm_misc_warn("xt %s skipped: %d\n",
				droid_lkm_misc_xt_items[i].name, ret);
			continue;
		}
		droid_lkm_misc_xt_started[i] = true;
		droid_lkm_misc_xt_registered++;
		droid_lkm_misc_info("xt registered: %s\n",
			droid_lkm_misc_xt_items[i].name);
	}

	droid_lkm_misc_report("xt", droid_lkm_misc_xt_registered ? "ready" : "unsupported",
			      droid_lkm_misc_xt_registered ?
			      "xt_register_matches available" :
			      "no match could be registered");

	return 0;
}

void droid_lkm_misc_xt_exit(void)
{
	unsigned int i;

	for (i = ARRAY_SIZE(droid_lkm_misc_xt_items); i-- > 0;) {
		if (!droid_lkm_misc_xt_started[i])
			continue;
		droid_lkm_misc_xt_started[i] = false;
		droid_lkm_misc_xt_registered--;
		droid_lkm_misc_xt_items[i].exit();
	}
}

unsigned int droid_lkm_misc_xt_count(void)
{
	return droid_lkm_misc_xt_registered;
}

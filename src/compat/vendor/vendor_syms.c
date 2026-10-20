// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>

#include "ds_compat.h"
#include "vendor_syms.h"

struct dlc_vendor_syms dlc_vendor_syms;

/* the vendor module's kernel symbol table, fed to the compat layer at load */
static struct ds_compat_sym dlc_vendor_table[] = {
	{ "init_task", (void **)&dlc_vendor_syms.init_task },
	{ "__module_address", (void **)&dlc_vendor_syms.module_address },
	{ "kern_path", (void **)&dlc_vendor_syms.kern_path },
	{ "path_put", (void **)&dlc_vendor_syms.path_put },
	{ "pid_max", (void **)&dlc_vendor_syms.pid_max },
	{ "ddl_sdata", (void **)&dlc_vendor_syms.ddl_sdata },
	{ "update_ddl_hit_history", (void **)&dlc_vendor_syms.ddl_update },
};

void dlc_vendor_syms_init(void)
{
	ds_compat_bind(dlc_vendor_table, ARRAY_SIZE(dlc_vendor_table));
}

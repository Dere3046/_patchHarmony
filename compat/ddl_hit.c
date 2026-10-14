// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/pid.h>

#include "core.h"
#include "ddl_hit.h"

/* fixes the crash the oplus scheduler module causes on screen off */

struct dlc_ddl_entry {
	char comm[TASK_COMM_LEN];
	u64 hit;
};

static struct dlc_ddl_entry *dlc_ddl_table;
static struct dlc_ddl_entry *dlc_ddl_orig;
static void **dlc_ddl_slot;

static bool dlc_ddl_is_pointer(unsigned long val)
{
	return val > 0xffff000000000000UL && (val & 0x7) == 0;
}

int dlc_ddl_hit_init(void)
{
	unsigned long sym = kallsyms_name_to_addr("ddl_sdata");
	unsigned long func = kallsyms_name_to_addr("update_ddl_hit_history");
	size_t count = PID_MAX_LIMIT + 1;
	void *slot;
	void *table;

	if (!sym || !func)
		return 0;

	slot = (void *)sym;
	if (!dlc_ddl_is_pointer((unsigned long)READ_ONCE(*(void **)slot))) {
		pr_warn("[droid_lkm_compat] ddl history is an array so it cannot be replaced, set pid_max_cap to keep the index inside it\n");
		return 0;
	}

	table = kvzalloc(count * sizeof(struct dlc_ddl_entry), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	dlc_ddl_table = table;
	dlc_ddl_slot = slot;
	dlc_ddl_orig = READ_ONCE(*(void **)slot);
	WRITE_ONCE(*(void **)slot, table);
	pr_info("[droid_lkm_compat] ddl history table replaced with %zu entries\n",
		count);
	return 0;
}

void dlc_ddl_hit_exit(void)
{
	if (!dlc_ddl_slot || !dlc_ddl_table)
		return;

	WRITE_ONCE(*dlc_ddl_slot, dlc_ddl_orig);
	kvfree(dlc_ddl_table);
	dlc_ddl_table = NULL;
	dlc_ddl_orig = NULL;
	dlc_ddl_slot = NULL;
}

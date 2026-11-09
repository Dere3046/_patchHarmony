// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Dere3046
 */

#ifndef DLC_VENDOR_SYMS_H
#define DLC_VENDOR_SYMS_H

#include <linux/types.h>

struct pid_namespace;
struct task_struct;

struct dlc_vendor_syms {
	unsigned long init_task;
	unsigned long module_address;
	void *ddl_sdata;
	void *ddl_update;
	struct task_struct *(*find_task_by_pid_ns)(pid_t nr, struct pid_namespace *ns);
	struct pid_namespace *init_pid_ns;
};

extern struct dlc_vendor_syms dlc_vendor_syms;

void dlc_vendor_syms_init(void);

#endif

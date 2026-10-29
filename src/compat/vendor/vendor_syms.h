// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DLC_VENDOR_SYMS_H
#define DLC_VENDOR_SYMS_H

#include <linux/types.h>

struct path;
struct pid_namespace;

struct dlc_vendor_syms {
	unsigned long init_task;
	unsigned long module_address;
	int (*kern_path)(const char *name, unsigned int flags,
			 struct path *path);
	void (*path_put)(const struct path *path);
	int *pid_max;
	void *ddl_sdata;
	void *ddl_update;
	struct task_struct *(*find_task_by_pid_ns)(pid_t nr, struct pid_namespace *ns);
	struct pid_namespace *init_pid_ns;
};

extern struct dlc_vendor_syms dlc_vendor_syms;

void dlc_vendor_syms_init(void);

#endif

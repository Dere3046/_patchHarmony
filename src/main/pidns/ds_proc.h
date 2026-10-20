// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DROID_LKM_PROC_H
#define DROID_LKM_PROC_H

#include <linux/types.h>

struct proc_ns_operations;
struct task_struct;

/*
 * Namespace kinds the main module does not implement itself, registered by an
 * optional module (the user namespace lives in droid_lkm_misc). The callback
 * picks the operations for one task, so the owner can be a fake namespace, the
 * host placeholder, or nothing at all.
 */
struct droid_lkm_ns_kind_reg {
	const char *name;
	const struct proc_ns_operations *(*ops_for)(struct task_struct *task);
};

int droid_lkm_ns_kind_register(const struct droid_lkm_ns_kind_reg *kind);
void droid_lkm_ns_kind_unregister(const struct droid_lkm_ns_kind_reg *kind);

int droid_lkm_proc_init(void);
void droid_lkm_proc_exit(void);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DS_IPC_SYSCTL_H
#define DS_IPC_SYSCTL_H

#include <linux/types.h>
#include <linux/sysctl.h>
#include <linux/uidgid.h>

struct ipc_namespace;


bool droid_lkm_sysctl_ready(void);
void droid_lkm_sysctl_setup_set(struct ctl_table_set *set,
				struct ctl_table_root *root,
				int (*is_seen)(struct ctl_table_set *));
void droid_lkm_sysctl_retire_set(struct ctl_table_set *set);
struct ctl_table_header *droid_lkm_sysctl_register_table(
	struct ctl_table_set *set, const char *path, struct ctl_table *table,
	size_t table_size);
int droid_lkm_sysctl_in_egroup_p(kgid_t grp);

int droid_lkm_ipc_sysctls_init(void);
void droid_lkm_ipc_sysctls_exit(void);
bool droid_lkm_ipc_sysctls_ok(void);
bool droid_lkm_ipc_sysctls_setup(struct ipc_namespace *ns);
void droid_lkm_ipc_sysctls_retire(struct ipc_namespace *ns);

#endif

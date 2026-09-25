// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_IPCNS_H
#define DROID_LKM_IPCNS_H

#include <linux/types.h>
#include <linux/ipc_namespace.h>
#include <linux/ns_common.h>
#include <linux/proc_ns.h>

extern const struct proc_ns_operations droid_lkm_ipcns_ops;

int droid_lkm_ipcns_init(void);
void droid_lkm_ipcns_exit(void);


struct ipc_namespace *droid_lkm_ipcns_create(void);

struct ipc_namespace *droid_lkm_ipcns_get(struct ipc_namespace *ns);
void droid_lkm_ipcns_put(struct ipc_namespace *ns);

bool droid_lkm_ipcns_is_ours(struct ipc_namespace *ns);


bool droid_lkm_ipcns_is_host(struct ipc_namespace *ns);


bool droid_lkm_ipcns_busy(void);


bool droid_lkm_ipcns_task_is_host(struct task_struct *task);


bool droid_lkm_ipcns_in_use(struct ipc_namespace *ns);


struct ipc_namespace *droid_lkm_ipcns_current(void);
struct ipc_namespace *droid_lkm_ipcns_task_ns(struct task_struct *task);

#endif /* DROID_LKM_IPCNS_H */

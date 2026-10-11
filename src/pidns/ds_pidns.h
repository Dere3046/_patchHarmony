// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_PIDNS_H
#define DROID_LKM_PIDNS_H

#include <linux/types.h>
#include <linux/version.h>
#include <linux/refcount.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/ns_common.h>
#include <linux/proc_ns.h>

/*
 * ns_common carries its own refcount from 5.15 on, and 5.10 keeps the count of
 * a pid namespace in its kref. both are the counter the kernel itself would use
 * for this object, so one accessor serves every caller.
 */
static inline refcount_t *droid_lkm_pidns_refcount(struct pid_namespace *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return &ns->ns.__ns_ref;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	return &ns->ns.count;
#else
	return &ns->kref.refcount;
#endif
}

extern const struct proc_ns_operations droid_lkm_pidns_ops;
extern const struct proc_ns_operations droid_lkm_pidns_for_children_ops;

int droid_lkm_pidns_init(void);

bool droid_lkm_pidns_skip_do_exit(void);
void droid_lkm_pidns_exit(void);

struct pid_namespace *droid_lkm_pidns_create(struct pid_namespace *parent);

struct pid_namespace *droid_lkm_pidns_create_for_current(void);

struct pid_namespace *droid_lkm_pidns_get(struct pid_namespace *ns);
void droid_lkm_pidns_put(struct pid_namespace *ns);

bool droid_lkm_pidns_is_ours(struct pid_namespace *ns);

struct pid_namespace *droid_lkm_pidns_task_for_children(struct task_struct *task);

bool droid_lkm_pidns_busy(void);

int droid_lkm_pidns_reboot(struct pid_namespace *ns, int cmd);

void droid_lkm_pidns_queue_zap(struct pid_namespace *ns);

void droid_lkm_pidns_sysctl_init(void);
void droid_lkm_pidns_sysctl_exit(void);

#endif

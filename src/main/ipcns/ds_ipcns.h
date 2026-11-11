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

#include "ds_ti.h"

extern const struct proc_ns_operations droid_lkm_ipcns_ops;

int droid_lkm_ipcns_init(void);
void droid_lkm_ipcns_exit(void);


struct ipc_namespace *droid_lkm_ipcns_create(void);

struct ipc_namespace *droid_lkm_ipcns_get(struct ipc_namespace *ns);
void droid_lkm_ipcns_put(struct ipc_namespace *ns);

bool droid_lkm_ipcns_is_ours(struct ipc_namespace *ns);


bool droid_lkm_ipcns_is_host(struct ipc_namespace *ns);
struct ipc_namespace *droid_lkm_ipcns_host_ns(void);


bool droid_lkm_ipcns_busy(void);


bool droid_lkm_ipcns_task_is_host(struct task_struct *task);


bool droid_lkm_ipcns_in_use(struct ipc_namespace *ns);


struct ipc_namespace *droid_lkm_ipcns_current(void);
struct ipc_namespace *droid_lkm_ipcns_task_ns(struct task_struct *task);

u32 droid_lkm_ipcns_off(enum droid_lkm_field_id id);

/* sem, msg and shm share one array in that order */
#define DROID_LKM_IPC_IDS_COUNT	3

/*
 * a namespace the kernel allocated follows the running kernel layout, the
 * module's own follow this build, so members go through here
 */
void *droid_lkm_ipcns_field(struct ipc_namespace *ns,
			    enum droid_lkm_field_id id);
struct ipc_ids *droid_lkm_ipcns_ids(struct ipc_namespace *ns, int which);
u32 droid_lkm_ipcns_ids_size(void);

#define droid_lkm_ns_int(_ns, _id)                                             \
	(*(int *)droid_lkm_ipcns_field((_ns), (_id)))
#define droid_lkm_ns_uint(_ns, _id)                                            \
	(*(unsigned int *)droid_lkm_ipcns_field((_ns), (_id)))
#define droid_lkm_ns_ulong(_ns, _id)                                           \
	(*(unsigned long *)droid_lkm_ipcns_field((_ns), (_id)))
#define droid_lkm_ns_size(_ns, _id)                                            \
	(*(size_t *)droid_lkm_ipcns_field((_ns), (_id)))
#define droid_lkm_ns_int_ptr(_ns, _id)                                         \
	((int *)droid_lkm_ipcns_field((_ns), (_id)))
#define droid_lkm_ns_ptr(_ns, _id, _type)                                      \
	(*(_type **)droid_lkm_ipcns_field((_ns), (_id)))

struct user_namespace;
struct vfsmount;

static inline struct user_namespace *
droid_lkm_ipcns_user_ns(struct ipc_namespace *ns)
{
	return droid_lkm_ns_ptr(ns, DROID_LKM_F_IPC_NS_USER_NS,
				struct user_namespace);
}

static inline void droid_lkm_ipcns_set_user_ns(struct ipc_namespace *ns,
					       struct user_namespace *user_ns)
{
	droid_lkm_ns_ptr(ns, DROID_LKM_F_IPC_NS_USER_NS,
			 struct user_namespace) = user_ns;
}

static inline struct vfsmount *droid_lkm_ipcns_mq_mnt(struct ipc_namespace *ns)
{
	return droid_lkm_ns_ptr(ns, DROID_LKM_F_IPC_NS_MQ_MNT, struct vfsmount);
}

static inline void droid_lkm_ipcns_set_mq_mnt(struct ipc_namespace *ns,
					      struct vfsmount *mnt)
{
	droid_lkm_ns_ptr(ns, DROID_LKM_F_IPC_NS_MQ_MNT, struct vfsmount) = mnt;
}

#endif /* DROID_LKM_IPCNS_H */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/pid_namespace.h>
#include <linux/pid.h>
#include <linux/seq_file.h>

#include "ds.h"
#include "ds_ti.h"
#include "ds_compat.h"

// build offset kept so a kernel that disagrees with the headers is visible
#define DROID_LKM_FIELD(_id, _type, _member)                                   \
	[_id] = { .type = #_type, .member = #_member,                          \
		  .build = offsetof(struct _type, _member) },

static struct ds_compat_field droid_lkm_fields[DROID_LKM_FIELD_COUNT] = {
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_IDS, ipc_namespace, ids)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MNT, ipc_namespace, mq_mnt)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_USER_NS, ipc_namespace, user_ns)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_UCOUNTS, ipc_namespace, ucounts)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_NS, ipc_namespace, ns)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_IDR, pid_namespace, idr)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_LEVEL, pid_namespace, level)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_PARENT, pid_namespace, parent)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_PID_CACHEP, pid_namespace, pid_cachep)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_CHILD_REAPER, pid_namespace, child_reaper)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_USER_NS, pid_namespace, user_ns)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_UCOUNTS, pid_namespace, ucounts)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_PID_ALLOCATED, pid_namespace, pid_allocated)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_NS, pid_namespace, ns)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_IPC_NS, nsproxy, ipc_ns)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_PID_NS_FOR_CHILDREN, nsproxy, pid_ns_for_children)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_UTS_NS, nsproxy, uts_ns)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_MNT_NS, nsproxy, mnt_ns)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_NET_NS, nsproxy, net_ns)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_PID, task_struct, pid)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_TGID, task_struct, tgid)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_COMM, task_struct, comm)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_NSPROXY, task_struct, nsproxy)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_THREAD_PID, task_struct, thread_pid)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_GROUP_LEADER, task_struct, group_leader)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_REAL_PARENT, task_struct, real_parent)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_SIGNAL, task_struct, signal)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_SIGHAND, task_struct, sighand)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_MM, task_struct, mm)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_FLAGS, task_struct, flags)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_SE, task_struct, se)
	DROID_LKM_FIELD(DROID_LKM_F_SEQ_FILE_BUF, seq_file, buf)
	DROID_LKM_FIELD(DROID_LKM_F_SEQ_FILE_COUNT, seq_file, count)
	DROID_LKM_FIELD(DROID_LKM_F_SEQ_FILE_SIZE, seq_file, size)
	DROID_LKM_FIELD(DROID_LKM_F_PID_LEVEL, pid, level)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NUMBERS, pid, numbers)
	DROID_LKM_FIELD(DROID_LKM_F_UPID_NR, upid, nr)
	DROID_LKM_FIELD(DROID_LKM_F_UPID_NS, upid, ns)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_QUEUES_MAX, ipc_namespace, mq_queues_max)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MSG_MAX, ipc_namespace, mq_msg_max)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MSGSIZE_MAX, ipc_namespace, mq_msgsize_max)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MSG_DEFAULT, ipc_namespace, mq_msg_default)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MSGSIZE_DEFAULT, ipc_namespace, mq_msgsize_default)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SEM_CTLS, ipc_namespace, sem_ctls)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MSG_CTLMAX, ipc_namespace, msg_ctlmax)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MSG_CTLMNB, ipc_namespace, msg_ctlmnb)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MSG_CTLMNI, ipc_namespace, msg_ctlmni)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SHM_CTLMAX, ipc_namespace, shm_ctlmax)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SHM_CTLALL, ipc_namespace, shm_ctlall)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SHM_CTLMNI, ipc_namespace, shm_ctlmni)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SHM_RMID_FORCED, ipc_namespace, shm_rmid_forced)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_SET, ipc_namespace, mq_set)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_IPC_SET, ipc_namespace, ipc_set)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_SYSCTLS, ipc_namespace, mq_sysctls)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_IPC_SYSCTLS, ipc_namespace, ipc_sysctls)
#endif
};

#define DROID_LKM_TYPE(_id, _type)                                            	[_id] = { .name = #_type, .build = sizeof(struct _type) },

static struct ds_compat_type droid_lkm_types[DROID_LKM_TYPE_COUNT] = {
	DROID_LKM_TYPE(DROID_LKM_T_UPID, upid)
};

static struct ds_compat_layout droid_lkm_layout = {
	.module = "droid_lkm",
	.fields = droid_lkm_fields,
	.nfields = ARRAY_SIZE(droid_lkm_fields),
	.types = droid_lkm_types,
	.ntypes = ARRAY_SIZE(droid_lkm_types),
};

void droid_lkm_layout_init(void)
{
	ds_compat_layout_init(&droid_lkm_layout);
}

u32 droid_lkm_layout_off(enum droid_lkm_field_id id)
{
	if (id >= DROID_LKM_FIELD_COUNT)
		return 0;
	return droid_lkm_fields[id].live;
}

bool droid_lkm_layout_ok(enum droid_lkm_field_id id)
{
	if (id >= DROID_LKM_FIELD_COUNT)
		return false;
	return droid_lkm_fields[id].state == DS_COMPAT_OK;
}

void *droid_lkm_layout_ptr(void *base, enum droid_lkm_field_id id)
{
	if (!base || !droid_lkm_layout_ok(id))
		return NULL;
	return (char *)base + droid_lkm_layout_off(id);
}

u32 droid_lkm_layout_type_size(enum droid_lkm_type_id id)
{
	if (id >= DROID_LKM_TYPE_COUNT)
		return 0;
	return droid_lkm_types[id].live;
}

bool droid_lkm_layout_type_ok(enum droid_lkm_type_id id)
{
	if (id >= DROID_LKM_TYPE_COUNT)
		return false;
	return droid_lkm_types[id].state == DS_COMPAT_OK;
}

void droid_lkm_layout_report(void)
{
	ds_compat_layout_report(&droid_lkm_layout);
}

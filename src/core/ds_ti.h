/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_DS_TI_H
#define DROID_LKM_DS_TI_H

#include <linux/types.h>
#include <linux/version.h>

// offsets come from the running kernel BTF
// a field the running kernel does not place where the headers do is refused
enum droid_lkm_field_id {
	DROID_LKM_F_IPC_NS_IDS,
	DROID_LKM_F_IPC_NS_MQ_MNT,
	DROID_LKM_F_IPC_NS_USER_NS,
	DROID_LKM_F_IPC_NS_UCOUNTS,
	DROID_LKM_F_IPC_NS_NS,
	DROID_LKM_F_PID_NS_IDR,
	DROID_LKM_F_PID_NS_LEVEL,
	DROID_LKM_F_PID_NS_PARENT,
	DROID_LKM_F_PID_NS_PID_CACHEP,
	DROID_LKM_F_PID_NS_CHILD_REAPER,
	DROID_LKM_F_PID_NS_USER_NS,
	DROID_LKM_F_PID_NS_UCOUNTS,
	DROID_LKM_F_PID_NS_PID_ALLOCATED,
	DROID_LKM_F_PID_NS_NS,
	DROID_LKM_F_NSPROXY_IPC_NS,
	DROID_LKM_F_NSPROXY_PID_NS_FOR_CHILDREN,
	DROID_LKM_F_NSPROXY_UTS_NS,
	DROID_LKM_F_NSPROXY_MNT_NS,
	DROID_LKM_F_NSPROXY_NET_NS,
	DROID_LKM_F_TASK_PID,
	DROID_LKM_F_TASK_TGID,
	DROID_LKM_F_TASK_COMM,
	DROID_LKM_F_TASK_NSPROXY,
	DROID_LKM_F_TASK_THREAD_PID,
	DROID_LKM_F_TASK_GROUP_LEADER,
	DROID_LKM_F_TASK_REAL_PARENT,
	DROID_LKM_F_TASK_SIGNAL,
	DROID_LKM_F_TASK_SIGHAND,
	DROID_LKM_F_TASK_MM,
	DROID_LKM_F_TASK_FLAGS,
	DROID_LKM_F_TASK_SE,
	DROID_LKM_F_SEQ_FILE_BUF,
	DROID_LKM_F_SEQ_FILE_COUNT,
	DROID_LKM_F_SEQ_FILE_SIZE,
	DROID_LKM_F_PID_LEVEL,
	DROID_LKM_F_PID_NUMBERS,
	DROID_LKM_F_UPID_NR,
	DROID_LKM_F_UPID_NS,
	DROID_LKM_F_IPC_NS_MQ_QUEUES_MAX,
	DROID_LKM_F_IPC_NS_MQ_MSG_MAX,
	DROID_LKM_F_IPC_NS_MQ_MSGSIZE_MAX,
	DROID_LKM_F_IPC_NS_MQ_MSG_DEFAULT,
	DROID_LKM_F_IPC_NS_MQ_MSGSIZE_DEFAULT,
	DROID_LKM_F_IPC_NS_SEM_CTLS,
	DROID_LKM_F_IPC_NS_MSG_CTLMAX,
	DROID_LKM_F_IPC_NS_MSG_CTLMNB,
	DROID_LKM_F_IPC_NS_MSG_CTLMNI,
	DROID_LKM_F_IPC_NS_SHM_CTLMAX,
	DROID_LKM_F_IPC_NS_SHM_CTLALL,
	DROID_LKM_F_IPC_NS_SHM_CTLMNI,
	DROID_LKM_F_IPC_NS_SHM_RMID_FORCED,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	// per ns sysctl sets exist from 6.1 on
	DROID_LKM_F_IPC_NS_MQ_SET,
	DROID_LKM_F_IPC_NS_IPC_SET,
	DROID_LKM_F_IPC_NS_MQ_SYSCTLS,
	DROID_LKM_F_IPC_NS_IPC_SYSCTLS,
#endif
	DROID_LKM_FIELD_COUNT
};

// sizes needed to walk a flexible array
enum droid_lkm_type_id {
	DROID_LKM_T_UPID,
	DROID_LKM_TYPE_COUNT
};

void droid_lkm_layout_init(void);
void droid_lkm_layout_report(void);

// running kernel sizeof or 0 when refused
u32 droid_lkm_layout_type_size(enum droid_lkm_type_id id);
bool droid_lkm_layout_type_ok(enum droid_lkm_type_id id);

// running kernel offset or 0 when refused
// callers check droid_lkm_layout_ok() first
u32 droid_lkm_layout_off(enum droid_lkm_field_id id);
bool droid_lkm_layout_ok(enum droid_lkm_field_id id);

// NULL when the field is refused
void *droid_lkm_layout_ptr(void *base, enum droid_lkm_field_id id);

#endif /* DROID_LKM_DS_TI_H */

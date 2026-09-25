// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/sched/signal.h>
#include <linux/nsproxy.h>

#include "ds.h"
#include "ds_ksym.h"

struct droid_lkm_ksym droid_lkm_ks = { };
int *droid_lkm_ks_sysctl_overcommit_memory;

static unsigned long __nocfi droid_lkm_resolve(const char *name)
{
	return droid_lkm_sym(name);
}

#define DROID_LKM_THUNK(_field, _name, _type)                                         \
	do {                                                                   \
		droid_lkm_ks._field = (_type)droid_lkm_resolve(_name);                       \
		if (!droid_lkm_ks._field)                                             \
			droid_lkm_warn("thunk missing: %s\n", _name);                 \
	} while (0)

int droid_lkm_ksym_init(void)
{
	DROID_LKM_THUNK(proc_alloc_inum, "proc_alloc_inum", typeof(droid_lkm_ks.proc_alloc_inum));
	DROID_LKM_THUNK(proc_free_inum, "proc_free_inum", typeof(droid_lkm_ks.proc_free_inum));
	DROID_LKM_THUNK(disable_pid_allocation, "disable_pid_allocation",
		 typeof(droid_lkm_ks.disable_pid_allocation));
	DROID_LKM_THUNK(group_send_sig_info, "group_send_sig_info",
		 typeof(droid_lkm_ks.group_send_sig_info));
	DROID_LKM_THUNK(kernel_wait4, "kernel_wait4", typeof(droid_lkm_ks.kernel_wait4));
	DROID_LKM_THUNK(do_mmap, "do_mmap", typeof(droid_lkm_ks.do_mmap));
	DROID_LKM_THUNK(wake_q_add, "wake_q_add", typeof(droid_lkm_ks.wake_q_add));
	DROID_LKM_THUNK(wake_q_add_safe, "wake_q_add_safe", typeof(droid_lkm_ks.wake_q_add_safe));
	DROID_LKM_THUNK(wake_up_q, "wake_up_q", typeof(droid_lkm_ks.wake_up_q));
	DROID_LKM_THUNK(schedule_hrtimeout_range, "schedule_hrtimeout_range",
		 typeof(droid_lkm_ks.schedule_hrtimeout_range));
	DROID_LKM_THUNK(get_timespec64, "get_timespec64", typeof(droid_lkm_ks.get_timespec64));
	DROID_LKM_THUNK(get_old_timespec32, "get_old_timespec32",
		 typeof(droid_lkm_ks.get_old_timespec32));
	DROID_LKM_THUNK(__percpu_counter_sum, "__percpu_counter_sum",
		 typeof(droid_lkm_ks.__percpu_counter_sum));
	DROID_LKM_THUNK(shmem_lock, "shmem_lock", typeof(droid_lkm_ks.shmem_lock));
	DROID_LKM_THUNK(shmem_kernel_file_setup, "shmem_kernel_file_setup",
		 typeof(droid_lkm_ks.shmem_kernel_file_setup));
	DROID_LKM_THUNK(shmem_unlock_mapping, "shmem_unlock_mapping",
		 typeof(droid_lkm_ks.shmem_unlock_mapping));
	DROID_LKM_THUNK(alloc_file_clone, "alloc_file_clone",
		 typeof(droid_lkm_ks.alloc_file_clone));
	DROID_LKM_THUNK(__mm_populate, "__mm_populate", typeof(droid_lkm_ks.__mm_populate));
	DROID_LKM_THUNK(do_vmi_align_munmap, "do_vmi_align_munmap",
		 typeof(droid_lkm_ks.do_vmi_align_munmap));
	DROID_LKM_THUNK(switch_task_namespaces, "switch_task_namespaces",
		 typeof(droid_lkm_ks.switch_task_namespaces));

	DROID_LKM_THUNK(security_ipc_permission, "security_ipc_permission",
		 typeof(droid_lkm_ks.security_ipc_permission));
	DROID_LKM_THUNK(security_mmap_file, "security_mmap_file",
		 typeof(droid_lkm_ks.security_mmap_file));
	DROID_LKM_THUNK(security_msg_msg_alloc, "security_msg_msg_alloc",
		 typeof(droid_lkm_ks.security_msg_msg_alloc));
	DROID_LKM_THUNK(security_msg_msg_free, "security_msg_msg_free",
		 typeof(droid_lkm_ks.security_msg_msg_free));
	DROID_LKM_THUNK(security_msg_queue_alloc, "security_msg_queue_alloc",
		 typeof(droid_lkm_ks.security_msg_queue_alloc));
	DROID_LKM_THUNK(security_msg_queue_free, "security_msg_queue_free",
		 typeof(droid_lkm_ks.security_msg_queue_free));
	DROID_LKM_THUNK(security_msg_queue_associate, "security_msg_queue_associate",
		 typeof(droid_lkm_ks.security_msg_queue_associate));
	DROID_LKM_THUNK(security_msg_queue_msgctl, "security_msg_queue_msgctl",
		 typeof(droid_lkm_ks.security_msg_queue_msgctl));
	DROID_LKM_THUNK(security_msg_queue_msgsnd, "security_msg_queue_msgsnd",
		 typeof(droid_lkm_ks.security_msg_queue_msgsnd));
	DROID_LKM_THUNK(security_msg_queue_msgrcv, "security_msg_queue_msgrcv",
		 typeof(droid_lkm_ks.security_msg_queue_msgrcv));
	DROID_LKM_THUNK(security_shm_alloc, "security_shm_alloc",
		 typeof(droid_lkm_ks.security_shm_alloc));
	DROID_LKM_THUNK(security_shm_free, "security_shm_free",
		 typeof(droid_lkm_ks.security_shm_free));
	DROID_LKM_THUNK(security_shm_associate, "security_shm_associate",
		 typeof(droid_lkm_ks.security_shm_associate));
	DROID_LKM_THUNK(security_shm_shmctl, "security_shm_shmctl",
		 typeof(droid_lkm_ks.security_shm_shmctl));
	DROID_LKM_THUNK(security_shm_shmat, "security_shm_shmat",
		 typeof(droid_lkm_ks.security_shm_shmat));
	DROID_LKM_THUNK(security_sem_alloc, "security_sem_alloc",
		 typeof(droid_lkm_ks.security_sem_alloc));
	DROID_LKM_THUNK(security_sem_free, "security_sem_free",
		 typeof(droid_lkm_ks.security_sem_free));
	DROID_LKM_THUNK(security_sem_associate, "security_sem_associate",
		 typeof(droid_lkm_ks.security_sem_associate));
	DROID_LKM_THUNK(security_sem_semctl, "security_sem_semctl",
		 typeof(droid_lkm_ks.security_sem_semctl));
	DROID_LKM_THUNK(security_sem_semop, "security_sem_semop",
		 typeof(droid_lkm_ks.security_sem_semop));
	DROID_LKM_THUNK(__audit_ipc_obj, "__audit_ipc_obj",
		 typeof(droid_lkm_ks.__audit_ipc_obj));
	DROID_LKM_THUNK(__audit_ipc_set_perm, "__audit_ipc_set_perm",
		 typeof(droid_lkm_ks.__audit_ipc_set_perm));
	DROID_LKM_THUNK(__audit_inode, "__audit_inode",
		 typeof(droid_lkm_ks.__audit_inode));
	DROID_LKM_THUNK(__audit_file, "__audit_file",
		 typeof(droid_lkm_ks.__audit_file));
	DROID_LKM_THUNK(__audit_mq_open, "__audit_mq_open",
		 typeof(droid_lkm_ks.__audit_mq_open));
	DROID_LKM_THUNK(__audit_mq_sendrecv, "__audit_mq_sendrecv",
		 typeof(droid_lkm_ks.__audit_mq_sendrecv));
	DROID_LKM_THUNK(__audit_mq_notify, "__audit_mq_notify",
		 typeof(droid_lkm_ks.__audit_mq_notify));
	DROID_LKM_THUNK(__audit_mq_getsetattr, "__audit_mq_getsetattr",
		 typeof(droid_lkm_ks.__audit_mq_getsetattr));

	droid_lkm_ks_sysctl_overcommit_memory =
		(int *)droid_lkm_resolve("sysctl_overcommit_memory");
	if (!droid_lkm_ks_sysctl_overcommit_memory)
		droid_lkm_warn("thunk missing: sysctl_overcommit_memory\n");

	if (!droid_lkm_ks.proc_alloc_inum || !droid_lkm_ks.proc_free_inum ||
	    !droid_lkm_ks.disable_pid_allocation || !droid_lkm_ks.group_send_sig_info ||
	    !droid_lkm_ks.kernel_wait4) {
		droid_lkm_err("required thunks unresolved\n");
		return -ENODATA;
	}

	return 0;
}

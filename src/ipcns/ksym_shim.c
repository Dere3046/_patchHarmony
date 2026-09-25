// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/wake_q.h>
#include <linux/hrtimer.h>
#include <linux/time64.h>
#include <linux/time32.h>
#include <linux/percpu_counter.h>
#include <linux/shmem_fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/sem.h>
#include <uapi/linux/mman.h>

#include "ds.h"
#include "ds_ksym.h"

void wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	if (droid_lkm_ks.wake_q_add)
		droid_lkm_ks.wake_q_add(head, task);
}

void wake_q_add_safe(struct wake_q_head *head, struct task_struct *task)
{
	if (droid_lkm_ks.wake_q_add_safe)
		droid_lkm_ks.wake_q_add_safe(head, task);
}

void wake_up_q(struct wake_q_head *head)
{
	if (droid_lkm_ks.wake_up_q)
		droid_lkm_ks.wake_up_q(head);
}

int schedule_hrtimeout_range(ktime_t *expires, u64 delta,
			     const enum hrtimer_mode mode)
{
	if (!droid_lkm_ks.schedule_hrtimeout_range)
		return -ENOSYS;
	return droid_lkm_ks.schedule_hrtimeout_range(expires, delta, mode);
}

int get_timespec64(struct timespec64 *ts, const struct __kernel_timespec __user *uts)
{
	if (!droid_lkm_ks.get_timespec64)
		return -ENOSYS;
	return droid_lkm_ks.get_timespec64(ts, uts);
}

int get_old_timespec32(struct timespec64 *ts, const void __user *uts)
{
	if (!droid_lkm_ks.get_old_timespec32)
		return -ENOSYS;
	return droid_lkm_ks.get_old_timespec32(ts, uts);
}

s64 __percpu_counter_sum(struct percpu_counter *fbc)
{
	if (!droid_lkm_ks.__percpu_counter_sum)
		return 0;
	return droid_lkm_ks.__percpu_counter_sum(fbc);
}

int shmem_lock(struct file *file, int lock, struct ucounts *ucounts)
{
	if (!droid_lkm_ks.shmem_lock)
		return -ENOSYS;
	return droid_lkm_ks.shmem_lock(file, lock, ucounts);
}

struct file *shmem_kernel_file_setup(const char *name, loff_t size,
				     unsigned long flags)
{
	if (!droid_lkm_ks.shmem_kernel_file_setup)
		return ERR_PTR(-ENOSYS);
	return droid_lkm_ks.shmem_kernel_file_setup(name, size, flags);
}

void shmem_unlock_mapping(struct address_space *mapping)
{
	if (droid_lkm_ks.shmem_unlock_mapping)
		droid_lkm_ks.shmem_unlock_mapping(mapping);
}

struct file *alloc_file_clone(struct file *base, int flags,
			      const struct file_operations *fops)
{
	if (!droid_lkm_ks.alloc_file_clone)
		return ERR_PTR(-ENOSYS);
	return droid_lkm_ks.alloc_file_clone(base, flags, fops);
}

int __mm_populate(unsigned long addr, unsigned long len, int ignore_errors)
{
	if (!droid_lkm_ks.__mm_populate)
		return 0;
	return droid_lkm_ks.__mm_populate(addr, len, ignore_errors);
}

int do_vmi_align_munmap(struct vma_iterator *vmi, struct vm_area_struct *vma,
			struct mm_struct *mm, unsigned long start,
			unsigned long end, struct list_head *uf, bool unlock)
{
	if (!droid_lkm_ks.do_vmi_align_munmap)
		return -ENOSYS;
	return droid_lkm_ks.do_vmi_align_munmap(vmi, vma, mm, start, end, uf, unlock);
}


kmem_buckets *kmem_buckets_create(const char *name, slab_flags_t flags,
				  unsigned int useroffset,
				  unsigned int usersize,
				  void (*ctor)(void *))
{
	return NULL;
}


// missing int hooks fall back to 0 as the kernel does without CONFIG_SECURITY
int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	if (!droid_lkm_ks.security_ipc_permission)
		return 0;
	return droid_lkm_ks.security_ipc_permission(ipcp, flag);
}

int security_mmap_file(struct file *file, unsigned long prot, unsigned long flags)
{
	if (!droid_lkm_ks.security_mmap_file)
		return 0;
	return droid_lkm_ks.security_mmap_file(file, prot, flags);
}

int security_msg_msg_alloc(struct msg_msg *msg)
{
	if (!droid_lkm_ks.security_msg_msg_alloc)
		return 0;
	return droid_lkm_ks.security_msg_msg_alloc(msg);
}

void security_msg_msg_free(struct msg_msg *msg)
{
	if (droid_lkm_ks.security_msg_msg_free)
		droid_lkm_ks.security_msg_msg_free(msg);
}

int security_msg_queue_alloc(struct kern_ipc_perm *msq)
{
	if (!droid_lkm_ks.security_msg_queue_alloc)
		return 0;
	return droid_lkm_ks.security_msg_queue_alloc(msq);
}

void security_msg_queue_free(struct kern_ipc_perm *msq)
{
	if (droid_lkm_ks.security_msg_queue_free)
		droid_lkm_ks.security_msg_queue_free(msq);
}

int security_msg_queue_associate(struct kern_ipc_perm *msq, int msqflg)
{
	if (!droid_lkm_ks.security_msg_queue_associate)
		return 0;
	return droid_lkm_ks.security_msg_queue_associate(msq, msqflg);
}

int security_msg_queue_msgctl(struct kern_ipc_perm *msq, int cmd)
{
	if (!droid_lkm_ks.security_msg_queue_msgctl)
		return 0;
	return droid_lkm_ks.security_msg_queue_msgctl(msq, cmd);
}

int security_msg_queue_msgsnd(struct kern_ipc_perm *msq, struct msg_msg *msg,
			      int msqflg)
{
	if (!droid_lkm_ks.security_msg_queue_msgsnd)
		return 0;
	return droid_lkm_ks.security_msg_queue_msgsnd(msq, msg, msqflg);
}

int security_msg_queue_msgrcv(struct kern_ipc_perm *msq, struct msg_msg *msg,
			      struct task_struct *target, long type, int mode)
{
	if (!droid_lkm_ks.security_msg_queue_msgrcv)
		return 0;
	return droid_lkm_ks.security_msg_queue_msgrcv(msq, msg, target, type, mode);
}

int security_shm_alloc(struct kern_ipc_perm *shp)
{
	if (!droid_lkm_ks.security_shm_alloc)
		return 0;
	return droid_lkm_ks.security_shm_alloc(shp);
}

void security_shm_free(struct kern_ipc_perm *shp)
{
	if (droid_lkm_ks.security_shm_free)
		droid_lkm_ks.security_shm_free(shp);
}

int security_shm_associate(struct kern_ipc_perm *shp, int shmflg)
{
	if (!droid_lkm_ks.security_shm_associate)
		return 0;
	return droid_lkm_ks.security_shm_associate(shp, shmflg);
}

int security_shm_shmctl(struct kern_ipc_perm *shp, int cmd)
{
	if (!droid_lkm_ks.security_shm_shmctl)
		return 0;
	return droid_lkm_ks.security_shm_shmctl(shp, cmd);
}

int security_shm_shmat(struct kern_ipc_perm *shp, char __user *shmaddr, int shmflg)
{
	if (!droid_lkm_ks.security_shm_shmat)
		return 0;
	return droid_lkm_ks.security_shm_shmat(shp, shmaddr, shmflg);
}

int security_sem_alloc(struct kern_ipc_perm *sma)
{
	if (!droid_lkm_ks.security_sem_alloc)
		return 0;
	return droid_lkm_ks.security_sem_alloc(sma);
}

void security_sem_free(struct kern_ipc_perm *sma)
{
	if (droid_lkm_ks.security_sem_free)
		droid_lkm_ks.security_sem_free(sma);
}

int security_sem_associate(struct kern_ipc_perm *sma, int semflg)
{
	if (!droid_lkm_ks.security_sem_associate)
		return 0;
	return droid_lkm_ks.security_sem_associate(sma, semflg);
}

int security_sem_semctl(struct kern_ipc_perm *sma, int cmd)
{
	if (!droid_lkm_ks.security_sem_semctl)
		return 0;
	return droid_lkm_ks.security_sem_semctl(sma, cmd);
}

int security_sem_semop(struct kern_ipc_perm *sma, struct sembuf *sops, unsigned nsops,
		       int alter)
{
	if (!droid_lkm_ks.security_sem_semop)
		return 0;
	return droid_lkm_ks.security_sem_semop(sma, sops, nsops, alter);
}

void __audit_ipc_obj(struct kern_ipc_perm *ipcp)
{
	if (droid_lkm_ks.__audit_ipc_obj)
		droid_lkm_ks.__audit_ipc_obj(ipcp);
}

void __audit_ipc_set_perm(unsigned long qbytes, uid_t uid, gid_t gid,
			  umode_t mode)
{
	if (droid_lkm_ks.__audit_ipc_set_perm)
		droid_lkm_ks.__audit_ipc_set_perm(qbytes, uid, gid, mode);
}

void __audit_inode(struct filename *name, const struct dentry *dentry,
		   unsigned int aflags)
{
	if (droid_lkm_ks.__audit_inode)
		droid_lkm_ks.__audit_inode(name, dentry, aflags);
}

void __audit_file(const struct file *file)
{
	if (droid_lkm_ks.__audit_file)
		droid_lkm_ks.__audit_file(file);
}

void __audit_mq_open(int oflag, umode_t mode, struct mq_attr *attr)
{
	if (droid_lkm_ks.__audit_mq_open)
		droid_lkm_ks.__audit_mq_open(oflag, mode, attr);
}

void __audit_mq_sendrecv(mqd_t mqdes, size_t msg_len, unsigned int msg_prio,
			 const struct timespec64 *abs_timeout)
{
	if (droid_lkm_ks.__audit_mq_sendrecv)
		droid_lkm_ks.__audit_mq_sendrecv(mqdes, msg_len, msg_prio,
						 abs_timeout);
}

void __audit_mq_notify(mqd_t mqdes, const struct sigevent *notification)
{
	if (droid_lkm_ks.__audit_mq_notify)
		droid_lkm_ks.__audit_mq_notify(mqdes, notification);
}

void __audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat)
{
	if (droid_lkm_ks.__audit_mq_getsetattr)
		droid_lkm_ks.__audit_mq_getsetattr(mqdes, mqstat);
}


int droid_lkm_overcommit_memory(void)
{
	return droid_lkm_ks_sysctl_overcommit_memory ?
		       READ_ONCE(*droid_lkm_ks_sysctl_overcommit_memory) :
		       OVERCOMMIT_GUESS;
}

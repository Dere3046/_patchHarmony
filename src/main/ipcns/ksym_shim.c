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
#include <linux/mount.h>
#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/sem.h>
#include <uapi/linux/mman.h>

#include "ds.h"
#include "ds_shapes.h"
#include "ds_ksym.h"

/* 6.18 builds with -Wmissing-prototypes */
__nocfi noinline void __audit_file(const struct file *file);
__nocfi noinline void __audit_inode(struct filename *name, const struct dentry *dentry, unsigned int aflags);
__nocfi noinline void __audit_ipc_obj(struct kern_ipc_perm *ipcp);
__nocfi noinline void __audit_ipc_set_perm(unsigned long qbytes, uid_t uid, gid_t gid, umode_t mode);
__nocfi noinline void __audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat);
__nocfi noinline void __audit_mq_notify(mqd_t mqdes, const struct sigevent *notification);
__nocfi noinline void __audit_mq_open(int oflag, umode_t mode, struct mq_attr *attr);
__nocfi noinline void __audit_mq_sendrecv(mqd_t mqdes, size_t msg_len, unsigned int msg_prio, const struct timespec64 *abs_timeout);

__nocfi noinline void wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	if (droid_lkm_ks.wake_q_add)
		droid_lkm_ks.wake_q_add(head, task);
}

__nocfi noinline void wake_q_add_safe(struct wake_q_head *head, struct task_struct *task)
{
	if (droid_lkm_ks.wake_q_add_safe)
		droid_lkm_ks.wake_q_add_safe(head, task);
}

__nocfi noinline void wake_up_q(struct wake_q_head *head)
{
	if (droid_lkm_ks.wake_up_q)
		droid_lkm_ks.wake_up_q(head);
}

__nocfi noinline int schedule_hrtimeout_range(ktime_t *expires, u64 delta,
			     const enum hrtimer_mode mode)
{
	if (!droid_lkm_ks.schedule_hrtimeout_range)
		return -ENOSYS;
	return droid_lkm_ks.schedule_hrtimeout_range(expires, delta, mode);
}

__nocfi noinline int get_timespec64(struct timespec64 *ts, const struct __kernel_timespec __user *uts)
{
	if (!droid_lkm_ks.get_timespec64)
		return -ENOSYS;
	return droid_lkm_ks.get_timespec64(ts, uts);
}

__nocfi noinline int get_old_timespec32(struct timespec64 *ts, const void __user *uts)
{
	if (!droid_lkm_ks.get_old_timespec32)
		return -ENOSYS;
	return droid_lkm_ks.get_old_timespec32(ts, uts);
}

__nocfi noinline s64 __percpu_counter_sum(struct percpu_counter *fbc)
{
	if (!droid_lkm_ks.__percpu_counter_sum)
		return 0;
	return droid_lkm_ks.__percpu_counter_sum(fbc);
}

__nocfi noinline int shmem_lock(struct file *file, int lock, DROID_LKM_SHMEM_LOCK_PARAM)
{
	if (!droid_lkm_ks.shmem_lock)
		return -ENOSYS;
	return droid_lkm_ks.shmem_lock(file, lock, DROID_LKM_SHMEM_LOCK_PASS);
}

__nocfi noinline struct file *shmem_kernel_file_setup(const char *name, loff_t size,
				     unsigned long flags)
{
	if (!droid_lkm_ks.shmem_kernel_file_setup)
		return ERR_PTR(-ENOSYS);
	return droid_lkm_ks.shmem_kernel_file_setup(name, size, flags);
}

__nocfi noinline void shmem_unlock_mapping(struct address_space *mapping)
{
	if (droid_lkm_ks.shmem_unlock_mapping)
		droid_lkm_ks.shmem_unlock_mapping(mapping);
}

__nocfi noinline struct file *alloc_file_clone(struct file *base, int flags,
			      const struct file_operations *fops)
{
	if (!droid_lkm_ks.alloc_file_clone)
		return ERR_PTR(-ENOSYS);
	return droid_lkm_ks.alloc_file_clone(base, flags, fops);
}

__nocfi noinline int __mm_populate(unsigned long addr, unsigned long len, int ignore_errors)
{
	if (!droid_lkm_ks.__mm_populate)
		return 0;
	return droid_lkm_ks.__mm_populate(addr, len, ignore_errors);
}

__nocfi noinline int inode_permission(DROID_LKM_IDMAP_PARAM struct inode *inode, int mask)
{
	if (!droid_lkm_ks.inode_permission)
		return -ENOSYS;
	return droid_lkm_ks.inode_permission(DROID_LKM_IDMAP_PASS inode, mask);
}

__nocfi noinline int mnt_want_write(struct vfsmount *mnt)
{
	if (!droid_lkm_ks.mnt_want_write)
		return -ENOSYS;
	return droid_lkm_ks.mnt_want_write(mnt);
}

__nocfi noinline void mnt_drop_write(struct vfsmount *mnt)
{
	if (droid_lkm_ks.mnt_drop_write)
		droid_lkm_ks.mnt_drop_write(mnt);
}

__nocfi noinline struct dentry *lookup_one_len(const char *name, struct dentry *base, int len)
{
	if (droid_lkm_ks.lookup_one_len)
		return droid_lkm_ks.lookup_one_len(name, base, len);
	if (droid_lkm_ks.lookup_one_len)
		return droid_lkm_ks.lookup_one_len(name, base, len);

	{
		static struct dentry *(*fn)(const struct qstr *, struct dentry *,
					     unsigned int);

		if (!fn)
			fn = (void *)droid_lkm_sym("lookup_one_qstr_excl");
		if (fn && base && base->d_inode) {
			struct qstr q = {
				.name = name,
				.hash_len = hashlen_create(
					full_name_hash(NULL, name, len), len),
			};

			return fn(&q, base, 0);
		}
	}
	return ERR_PTR(-ENOSYS);
}

__nocfi noinline int get_tree_nodev(struct fs_context *fc,
		   int (*fill_super)(struct super_block *sb,
				     struct fs_context *fc))
{
	if (!droid_lkm_ks.get_tree_nodev)
		return -ENOSYS;
	return droid_lkm_ks.get_tree_nodev(fc, fill_super);
}

#ifdef CONFIG_SLAB_BUCKETS
kmem_buckets *kmem_buckets_create(const char *name, slab_flags_t flags,
				  unsigned int useroffset,
				  unsigned int usersize,
				  void (*ctor)(void *))
{
	return NULL;
}
#endif

// missing int hooks fall back to 0 as the kernel does without CONFIG_SECURITY
__nocfi noinline int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	if (!droid_lkm_ks.security_ipc_permission)
		return 0;
	return droid_lkm_ks.security_ipc_permission(ipcp, flag);
}

__nocfi noinline int security_mmap_file(struct file *file, unsigned long prot, unsigned long flags)
{
	if (!droid_lkm_ks.security_mmap_file)
		return 0;
	return droid_lkm_ks.security_mmap_file(file, prot, flags);
}

__nocfi noinline int security_msg_msg_alloc(struct msg_msg *msg)
{
	if (!droid_lkm_ks.security_msg_msg_alloc)
		return 0;
	return droid_lkm_ks.security_msg_msg_alloc(msg);
}

__nocfi noinline void security_msg_msg_free(struct msg_msg *msg)
{
	if (droid_lkm_ks.security_msg_msg_free)
		droid_lkm_ks.security_msg_msg_free(msg);
}

__nocfi noinline int security_msg_queue_alloc(struct kern_ipc_perm *msq)
{
	if (!droid_lkm_ks.security_msg_queue_alloc)
		return 0;
	return droid_lkm_ks.security_msg_queue_alloc(msq);
}

__nocfi noinline void security_msg_queue_free(struct kern_ipc_perm *msq)
{
	if (droid_lkm_ks.security_msg_queue_free)
		droid_lkm_ks.security_msg_queue_free(msq);
}

__nocfi noinline int security_msg_queue_associate(struct kern_ipc_perm *msq, int msqflg)
{
	if (!droid_lkm_ks.security_msg_queue_associate)
		return 0;
	return droid_lkm_ks.security_msg_queue_associate(msq, msqflg);
}

__nocfi noinline int security_msg_queue_msgctl(struct kern_ipc_perm *msq, int cmd)
{
	if (!droid_lkm_ks.security_msg_queue_msgctl)
		return 0;
	return droid_lkm_ks.security_msg_queue_msgctl(msq, cmd);
}

__nocfi noinline int security_msg_queue_msgsnd(struct kern_ipc_perm *msq, struct msg_msg *msg,
			      int msqflg)
{
	if (!droid_lkm_ks.security_msg_queue_msgsnd)
		return 0;
	return droid_lkm_ks.security_msg_queue_msgsnd(msq, msg, msqflg);
}

__nocfi noinline int security_msg_queue_msgrcv(struct kern_ipc_perm *msq, struct msg_msg *msg,
			      struct task_struct *target, long type, int mode)
{
	if (!droid_lkm_ks.security_msg_queue_msgrcv)
		return 0;
	return droid_lkm_ks.security_msg_queue_msgrcv(msq, msg, target, type, mode);
}

__nocfi noinline int security_shm_alloc(struct kern_ipc_perm *shp)
{
	if (!droid_lkm_ks.security_shm_alloc)
		return 0;
	return droid_lkm_ks.security_shm_alloc(shp);
}

__nocfi noinline void security_shm_free(struct kern_ipc_perm *shp)
{
	if (droid_lkm_ks.security_shm_free)
		droid_lkm_ks.security_shm_free(shp);
}

__nocfi noinline int security_shm_associate(struct kern_ipc_perm *shp, int shmflg)
{
	if (!droid_lkm_ks.security_shm_associate)
		return 0;
	return droid_lkm_ks.security_shm_associate(shp, shmflg);
}

__nocfi noinline int security_shm_shmctl(struct kern_ipc_perm *shp, int cmd)
{
	if (!droid_lkm_ks.security_shm_shmctl)
		return 0;
	return droid_lkm_ks.security_shm_shmctl(shp, cmd);
}

__nocfi noinline int security_shm_shmat(struct kern_ipc_perm *shp, char __user *shmaddr, int shmflg)
{
	if (!droid_lkm_ks.security_shm_shmat)
		return 0;
	return droid_lkm_ks.security_shm_shmat(shp, shmaddr, shmflg);
}

__nocfi noinline int security_sem_alloc(struct kern_ipc_perm *sma)
{
	if (!droid_lkm_ks.security_sem_alloc)
		return 0;
	return droid_lkm_ks.security_sem_alloc(sma);
}

__nocfi noinline void security_sem_free(struct kern_ipc_perm *sma)
{
	if (droid_lkm_ks.security_sem_free)
		droid_lkm_ks.security_sem_free(sma);
}

__nocfi noinline int security_sem_associate(struct kern_ipc_perm *sma, int semflg)
{
	if (!droid_lkm_ks.security_sem_associate)
		return 0;
	return droid_lkm_ks.security_sem_associate(sma, semflg);
}

__nocfi noinline int security_sem_semctl(struct kern_ipc_perm *sma, int cmd)
{
	if (!droid_lkm_ks.security_sem_semctl)
		return 0;
	return droid_lkm_ks.security_sem_semctl(sma, cmd);
}

__nocfi noinline int security_sem_semop(struct kern_ipc_perm *sma, struct sembuf *sops, unsigned nsops,
		       int alter)
{
	if (!droid_lkm_ks.security_sem_semop)
		return 0;
	return droid_lkm_ks.security_sem_semop(sma, sops, nsops, alter);
}

__nocfi noinline void __audit_ipc_obj(struct kern_ipc_perm *ipcp)
{
	if (droid_lkm_ks.__audit_ipc_obj)
		droid_lkm_ks.__audit_ipc_obj(ipcp);
}

__nocfi noinline void __audit_ipc_set_perm(unsigned long qbytes, uid_t uid, gid_t gid,
			  umode_t mode)
{
	if (droid_lkm_ks.__audit_ipc_set_perm)
		droid_lkm_ks.__audit_ipc_set_perm(qbytes, uid, gid, mode);
}

__nocfi noinline void __audit_inode(struct filename *name, const struct dentry *dentry,
		   unsigned int aflags)
{
	if (droid_lkm_ks.__audit_inode)
		droid_lkm_ks.__audit_inode(name, dentry, aflags);
}

__nocfi noinline void __audit_file(const struct file *file)
{
	if (droid_lkm_ks.__audit_file)
		droid_lkm_ks.__audit_file(file);
}

__nocfi noinline void __audit_mq_open(int oflag, umode_t mode, struct mq_attr *attr)
{
	if (droid_lkm_ks.__audit_mq_open)
		droid_lkm_ks.__audit_mq_open(oflag, mode, attr);
}

__nocfi noinline void __audit_mq_sendrecv(mqd_t mqdes, size_t msg_len, unsigned int msg_prio,
			 const struct timespec64 *abs_timeout)
{
	if (droid_lkm_ks.__audit_mq_sendrecv)
		droid_lkm_ks.__audit_mq_sendrecv(mqdes, msg_len, msg_prio,
						 abs_timeout);
}

__nocfi noinline void __audit_mq_notify(mqd_t mqdes, const struct sigevent *notification)
{
	if (droid_lkm_ks.__audit_mq_notify)
		droid_lkm_ks.__audit_mq_notify(mqdes, notification);
}

__nocfi noinline void __audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat)
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

/*
 * helpers the kernel carries but 6.18 keeps in a namespace, trims out of the
 * export table or marks protected. defining the kernel name in this module
 * keeps the link away from the loader, so no namespace import decides whether
 * the module loads, and the call goes through the resolved table. a NULL
 * entry leaves only the feature that needs it degraded
 */
__nocfi noinline struct dentry *d_splice_alias(struct inode *inode,
					       struct dentry *dentry)
{
	if (droid_lkm_ks.d_splice_alias)
		return droid_lkm_ks.d_splice_alias(inode, dentry);
	/* a lookup dentry is unhashed, d_add is the no-alias instantiation */
	d_add(dentry, inode);
	return NULL;
}

#ifdef CONFIG_MULTIUSER
__nocfi noinline int in_group_p(kgid_t grp)
{
	if (droid_lkm_ks.in_group_p)
		return droid_lkm_ks.in_group_p(grp);
	return 0;
}
#endif

__nocfi noinline void ihold(struct inode *inode)
{
	if (droid_lkm_ks.ihold)
		droid_lkm_ks.ihold(inode);
}

__nocfi noinline void inc_nlink(struct inode *inode)
{
	if (droid_lkm_ks.inc_nlink)
		droid_lkm_ks.inc_nlink(inode);
}

__nocfi noinline void drop_nlink(struct inode *inode)
{
	if (droid_lkm_ks.drop_nlink)
		droid_lkm_ks.drop_nlink(inode);
}

__nocfi noinline void clear_inode(struct inode *inode)
{
	if (droid_lkm_ks.clear_inode)
		droid_lkm_ks.clear_inode(inode);
}

__nocfi noinline void d_instantiate(struct dentry *dentry, struct inode *inode)
{
	if (droid_lkm_ks.d_instantiate)
		droid_lkm_ks.d_instantiate(dentry, inode);
}

__nocfi noinline void inode_init_once(struct inode *inode)
{
	if (droid_lkm_ks.inode_init_once)
		droid_lkm_ks.inode_init_once(inode);
}

/*
 * data, not a function: from_kgid_munged() reads it through an inline the
 * module cannot intercept, so the local copy is filled from the kernel address
 * in droid_lkm_ksym_init() before any module code runs
 */
typeof(overflowgid) overflowgid;

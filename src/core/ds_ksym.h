// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DROID_LKM_KSYM_H
#define DROID_LKM_KSYM_H

#include <linux/types.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/sched/signal.h>
#include <linux/sched/wake_q.h>
#include <linux/hrtimer.h>
#include <linux/time64.h>
#include <linux/percpu_counter.h>
#include <linux/mm.h>
#include <linux/fs.h>

struct nsproxy;
struct task_struct;
struct mnt_namespace;
struct kern_ipc_perm;
struct msg_msg;
struct sembuf;
struct filename;
struct mq_attr;
struct sigevent;
struct vfsmount;
struct fs_context;
struct mnt_idmap;

struct droid_lkm_ksym {

	int (*proc_alloc_inum)(unsigned int *inum);
	void (*proc_free_inum)(unsigned int inum);


	void (*disable_pid_allocation)(struct pid_namespace *ns);


	int (*group_send_sig_info)(int sig, struct kernel_siginfo *info,
				   struct task_struct *p, enum pid_type type);


	long (*kernel_wait4)(pid_t upid, int __user *stat_addr, int options,
			     struct rusage __user *ru);


	unsigned long (*do_mmap)(struct file *file, unsigned long addr,
				 unsigned long len, unsigned long prot,
				 unsigned long flags, unsigned long vm_flags,
				 unsigned long pgoff, unsigned long *populate,
				 struct list_head *uf);


	void (*wake_q_add)(struct wake_q_head *head, struct task_struct *task);
	void (*wake_q_add_safe)(struct wake_q_head *head, struct task_struct *task);
	void (*wake_up_q)(struct wake_q_head *head);


	int (*schedule_hrtimeout_range)(ktime_t *expires, u64 delta,
					const enum hrtimer_mode mode);
	int (*get_timespec64)(struct timespec64 *ts,
			      const struct __kernel_timespec __user *uts);
	int (*get_old_timespec32)(struct timespec64 *ts, const void __user *uts);


	s64 (*__percpu_counter_sum)(struct percpu_counter *fbc);


	int (*shmem_lock)(struct file *file, int lock, struct ucounts *ucounts);
	struct file *(*shmem_kernel_file_setup)(const char *name, loff_t size,
						unsigned long flags);
	void (*shmem_unlock_mapping)(struct address_space *mapping);
	struct file *(*alloc_file_clone)(struct file *base, int flags,
					 const struct file_operations *fops);
	int (*__mm_populate)(unsigned long addr, unsigned long len,
			     int ignore_errors);
	int (*do_vmi_align_munmap)(struct vma_iterator *vmi,
				   struct vm_area_struct *vma,
				   struct mm_struct *mm, unsigned long start,
				   unsigned long end, struct list_head *uf,
				   bool unlock);


	void (*switch_task_namespaces)(struct task_struct *p, struct nsproxy *new);

	/*
	 * mqueue vfs helpers. exported by the kernel the module links against,
	 * trimmed from some GKI kernels by TRIM_UNUSED_KSYMS, so they resolve at
	 * load time like the rest of the table.
	 */
	int (*inode_permission)(struct mnt_idmap *idmap, struct inode *inode,
				int mask);
	int (*mnt_want_write)(struct vfsmount *mnt);
	void (*mnt_drop_write)(struct vfsmount *mnt);
	struct dentry *(*lookup_one_len)(const char *name, struct dentry *base,
					 int len);
	int (*get_tree_nodev)(struct fs_context *fc,
			      int (*fill_super)(struct super_block *sb,
						struct fs_context *fc));


	int (*security_ipc_permission)(struct kern_ipc_perm *ipcp, short flag);
	int (*security_mmap_file)(struct file *file, unsigned long prot,
				 unsigned long flags);

	int (*security_msg_msg_alloc)(struct msg_msg *msg);
	void (*security_msg_msg_free)(struct msg_msg *msg);

	int (*security_msg_queue_alloc)(struct kern_ipc_perm *msq);
	void (*security_msg_queue_free)(struct kern_ipc_perm *msq);
	int (*security_msg_queue_associate)(struct kern_ipc_perm *msq, int msqflg);
	int (*security_msg_queue_msgctl)(struct kern_ipc_perm *msq, int cmd);
	int (*security_msg_queue_msgsnd)(struct kern_ipc_perm *msq,
					 struct msg_msg *msg, int msqflg);
	int (*security_msg_queue_msgrcv)(struct kern_ipc_perm *msq,
					 struct msg_msg *msg,
					 struct task_struct *target, long type,
					 int mode);

	int (*security_shm_alloc)(struct kern_ipc_perm *shp);
	void (*security_shm_free)(struct kern_ipc_perm *shp);
	int (*security_shm_associate)(struct kern_ipc_perm *shp, int shmflg);
	int (*security_shm_shmctl)(struct kern_ipc_perm *shp, int cmd);
	int (*security_shm_shmat)(struct kern_ipc_perm *shp,
				  char __user *shmaddr, int shmflg);

	int (*security_sem_alloc)(struct kern_ipc_perm *sma);
	void (*security_sem_free)(struct kern_ipc_perm *sma);
	int (*security_sem_associate)(struct kern_ipc_perm *sma, int semflg);
	int (*security_sem_semctl)(struct kern_ipc_perm *sma, int cmd);
	int (*security_sem_semop)(struct kern_ipc_perm *sma, struct sembuf *sops,
				  unsigned nsops, int alter);

	void (*__audit_ipc_obj)(struct kern_ipc_perm *ipcp);
	void (*__audit_ipc_set_perm)(unsigned long qbytes, uid_t uid, gid_t gid,
				     umode_t mode);
	void (*__audit_inode)(struct filename *name,
			      const struct dentry *dentry, unsigned int aflags);
	void (*__audit_file)(const struct file *file);
	void (*__audit_mq_open)(int oflag, umode_t mode, struct mq_attr *attr);
	void (*__audit_mq_sendrecv)(mqd_t mqdes, size_t msg_len,
				    unsigned int msg_prio,
				    const struct timespec64 *abs_timeout);
	void (*__audit_mq_notify)(mqd_t mqdes,
				  const struct sigevent *notification);
	void (*__audit_mq_getsetattr)(mqd_t mqdes, struct mq_attr *mqstat);
};

extern struct droid_lkm_ksym droid_lkm_ks;

extern int *droid_lkm_ks_sysctl_overcommit_memory;

int droid_lkm_ksym_init(void);

#endif

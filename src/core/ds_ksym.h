// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_KSYM_H
#define DROID_LKM_KSYM_H

#include <linux/types.h>

/* the thunk shapes that differ per branch are spelled in ds_compat.h */
#include "ds_compat.h"

struct ma_state;
struct vma_iterator;
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/sched/signal.h>
#include <linux/sched/wake_q.h>
#include <linux/hrtimer.h>
#include <linux/time64.h>
#include <linux/percpu_counter.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/uidgid.h>

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
struct user_namespace;
struct user_struct;
struct path;
struct cred;
struct ctl_table;
struct ctl_table_header;
struct ipc_namespace;
struct ipc_ids;
struct inode;
struct dentry;
struct dentry_operations;
struct list_lru;
struct kmem_cache;
struct rw_semaphore;
struct kprobe;
struct proc_ns_operations;
struct ucounts;

struct droid_lkm_ksym {

	int (*proc_alloc_inum)(unsigned int *inum);
	void (*proc_free_inum)(unsigned int inum);

	void (*disable_pid_allocation)(struct pid_namespace *ns);

	int (*group_send_sig_info)(int sig, struct kernel_siginfo *info,
				   struct task_struct *p, enum pid_type type);

	long (*kernel_wait4)(pid_t upid, int __user *stat_addr, int options,
			     struct rusage __user *ru);

	unsigned long (*do_mmap_legacy)(struct file *file, unsigned long addr,
					unsigned long len, unsigned long prot,
					unsigned long flags, unsigned long pgoff,
					unsigned long *populate, struct list_head *uf);
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

	int (*shmem_lock)(struct file *file, int lock,
			  DROID_LKM_SHMEM_LOCK_PARAM);
	struct file *(*shmem_kernel_file_setup)(const char *name, loff_t size,
						unsigned long flags);
	void (*shmem_unlock_mapping)(struct address_space *mapping);
	struct file *(*alloc_file_clone)(struct file *base, int flags,
					 const struct file_operations *fops);
	int (*__mm_populate)(unsigned long addr, unsigned long len,
			     int ignore_errors);
	int (*__do_munmap)(struct mm_struct *mm, unsigned long start,
			   size_t len, struct list_head *uf, bool downgrade);
	int (*do_mas_munmap)(struct ma_state *mas, struct mm_struct *mm,
			     unsigned long start, size_t len,
			     struct list_head *uf, bool unlock);
	int (*do_vmi_munmap)(struct vma_iterator *vmi, struct mm_struct *mm,
			     unsigned long start, size_t len,
			     struct list_head *uf, bool unlock);

	void (*switch_task_namespaces)(struct task_struct *p, struct nsproxy *new);

	/*
	 * mqueue vfs helpers. exported by the kernel the module links against,
	 * trimmed from some GKI kernels by TRIM_UNUSED_KSYMS, so they resolve at
	 * load time like the rest of the table.
	 */
	int (*inode_permission)(DROID_LKM_IDMAP_PARAM struct inode *inode,
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

	/*
	 * mqueue rlimit accounting. 5.10 has no rlimit ucounts at all, so a
	 * missing symbol turns the accounting off and leaves the queues fully
	 * usable. the type argument is an int because the enum behind it is
	 * named differently on each branch, and every one of them is int sized.
	 * the fields carry a ucounts_ prefix because the kernel names collide
	 * with the call site macros in ipc_mqueue_compat.h.
	 */
	struct ucounts *(*ucounts_get)(struct ucounts *ucounts);
	void (*ucounts_put)(struct ucounts *ucounts);
	long (*ucounts_inc_rlimit)(struct ucounts *ucounts, droid_lkm_rlimit_type type, long v);
	bool (*ucounts_dec_rlimit)(struct ucounts *ucounts, droid_lkm_rlimit_type type, long v);

	/*
	 * kernel helpers that a stock GKI image may not export. linking them
	 * makes the module fail to load on such an image with -ENOENT, so each
	 * one is resolved here and ipc_mqueue_shim.c defines the kernel name and
	 * refuses cleanly when it is missing. the shapes are identical on every
	 * branch we build for, except the two that already have a compat macro.
	 */
	struct file *(*dentry_open)(const struct path *path, int flags,
				    const struct cred *cred);
	int (*do_send_sig_info)(int sig, struct kernel_siginfo *info,
				struct task_struct *p, enum pid_type type);
	struct vfsmount *(*fc_mount)(struct fs_context *fc);
	struct vfsmount *(*mntget)(struct vfsmount *mnt);
	void (*put_fs_context)(struct fs_context *fc);
	void (*free_ipcs)(struct ipc_namespace *ns, struct ipc_ids *ids,
			  void (*free)(struct ipc_namespace *ns,
				       struct kern_ipc_perm *ipcp));
	void (*put_ipc_ns)(struct ipc_namespace *ns);
	pid_t (*pid_nr_ns)(struct pid *pid, struct pid_namespace *ns);
	pid_t (*pid_vnr)(struct pid *pid);
	int (*proc_dointvec_minmax)(DROID_LKM_CTL_TABLE *table, int write,
				    void *buffer, size_t *lenp, loff_t *ppos);
	int (*proc_doulongvec_minmax)(DROID_LKM_CTL_TABLE *table, int write,
				      void *buffer, size_t *lenp,
				      loff_t *ppos);
	int (*register_kprobe)(struct kprobe *p);
	void (*unregister_kprobe)(struct kprobe *p);
	int (*kern_path)(const char *name, unsigned int flags,
			 struct path *path);
	void (*path_put)(const struct path *path);
	void (*d_set_d_op)(struct dentry *dentry,
			   const struct dentry_operations *op);
	int (*down_write_killable)(struct rw_semaphore *sem);
	int (*radix_tree_tagged)(const struct radix_tree_root *root,
				 unsigned int tag);
	int (*vfs_unlink)(DROID_LKM_IDMAP_PARAM struct inode *dir,
			  struct dentry *dentry, struct inode **deleted);
	struct ctl_table_header *(*register_sysctl)(const char *path,
						    struct ctl_table *table);
	void (*unregister_sysctl_table)(struct ctl_table_header *header);
	struct ipc_namespace *(*copy_ipcs)(unsigned long flags,
					   struct user_namespace *user_ns,
					   struct ipc_namespace *ns);

	/*
	 * VFS and ipc helpers 6.18 keeps in a namespace and trims out of the
	 * plain export table. the module defines each kernel name itself, so the
	 * link never asks the loader for one, and the call goes through this
	 * table: a NULL entry degrades the one feature that needs it
	 */
	/* helpers 6.18 keeps in a namespace and trims or protects */
	struct dentry *(*d_splice_alias)(struct inode *inode,
					 struct dentry *dentry);
	int (*in_group_p)(kgid_t grp);
	void (*ihold)(struct inode *inode);
	void (*inc_nlink)(struct inode *inode);
	void (*drop_nlink)(struct inode *inode);
	void (*clear_inode)(struct inode *inode);
	void (*d_instantiate)(struct dentry *dentry, struct inode *inode);
	void (*inode_init_once)(struct inode *inode);
	/* protected on 6.18 instead of trimmed, and it answers the stamp it set */
	struct timespec64 (*simple_inode_init_ts)(struct inode *inode);
	int *overflowgid_addr;

	/*
	 * the hook engine patches text through the fixmap and resolves these
	 * itself, silently falling back when one is missing. resolving them here
	 * too puts them in the load time report and lets the inline hooks refuse
	 * instead of writing through a physical address that was never
	 * translated. plain addresses on purpose: the engine owns the calls.
	 */
	unsigned long text_start;	/* _text */
	unsigned long text_end;		/* _end */
	unsigned long kimage_voffset;	/* kimage_voffset */
	unsigned long vmalloc_to_pfn;	/* vmalloc_to_pfn */
	unsigned long set_fixmap;	/* __set_fixmap */
};

/* false when the text patch path cannot run on this kernel, one warning names what is missing */
bool droid_lkm_text_patch_ready(void);

extern struct droid_lkm_ksym droid_lkm_ks;

extern int *droid_lkm_ks_sysctl_overcommit_memory;

/* data symbols of the same class, dereferenced through these pointers */
extern rwlock_t *droid_lkm_ks_tasklist_lock;
extern const struct proc_ns_operations *droid_lkm_ks_ipcns_operations;

int droid_lkm_ksym_init(void);

#endif

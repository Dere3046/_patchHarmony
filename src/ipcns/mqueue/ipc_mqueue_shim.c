// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#include <linux/module.h>
#include <linux/statfs.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/dcache.h>
#include <linux/sched/signal.h>
#include <linux/kprobes.h>
#include <linux/sysctl.h>
#include <linux/rwsem.h>
#include <linux/radix-tree.h>
#include <linux/pid.h>
#include <linux/ipc_namespace.h>

#include "ds.h"
#include "ds_ksym.h"
#include "ipc_util.h"
#include "ipc_mqueue_compat.h"

/*
 * kernel helpers that a stock GKI image may not export. defining the kernel
 * name here satisfies every reference in the module, including an inline in a
 * kernel header and an address taken for a sysctl handler, and the thunk inside
 * refuses the feature instead of leaving the load with an unresolved symbol.
 */
__nocfi noinline struct file *dentry_open(const struct path *path, int flags,
			 const struct cred *cred)
{
	if (!droid_lkm_ks.dentry_open)
		return ERR_PTR(-ENOSYS);
	return droid_lkm_ks.dentry_open(path, flags, cred);
}

__nocfi noinline int do_send_sig_info(int sig, struct kernel_siginfo *info, struct task_struct *p,
		     enum pid_type type)
{
	if (!droid_lkm_ks.do_send_sig_info)
		return -ENOSYS;
	return droid_lkm_ks.do_send_sig_info(sig, info, p, type);
}

__nocfi noinline struct vfsmount *fc_mount(struct fs_context *fc)
{
	if (!droid_lkm_ks.fc_mount)
		return ERR_PTR(-ENOSYS);
	return droid_lkm_ks.fc_mount(fc);
}

__nocfi noinline struct vfsmount *mntget(struct vfsmount *mnt)
{
	/*
	 * without the symbol no reference can be taken. the mqueue shim refuses
	 * to enable mqueue in that case, so this only keeps the load working
	 */
	if (!droid_lkm_ks.mntget)
		return mnt;
	return droid_lkm_ks.mntget(mnt);
}

__nocfi noinline void put_fs_context(struct fs_context *fc)
{
	if (droid_lkm_ks.put_fs_context)
		droid_lkm_ks.put_fs_context(fc);
}

__nocfi noinline void free_ipcs(struct ipc_namespace *ns, struct ipc_ids *ids,
	       void (*free)(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp))
{
	if (droid_lkm_ks.free_ipcs)
		droid_lkm_ks.free_ipcs(ns, ids, free);
}

#ifdef CONFIG_IPC_NS
/* only a kernel with ipc namespaces declares this out of line */
__nocfi noinline void put_ipc_ns(struct ipc_namespace *ns)
{
	if (droid_lkm_ks.put_ipc_ns)
		droid_lkm_ks.put_ipc_ns(ns);
}
#endif

__nocfi noinline pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns)
{
	if (!droid_lkm_ks.pid_nr_ns)
		return 0;
	return droid_lkm_ks.pid_nr_ns(pid, ns);
}

__nocfi noinline pid_t pid_vnr(struct pid *pid)
{
	if (!droid_lkm_ks.pid_vnr)
		return 0;
	return droid_lkm_ks.pid_vnr(pid);
}

__nocfi noinline int proc_dointvec_minmax(DROID_LKM_CTL_TABLE *table, int write, void *buffer,
			 size_t *lenp, loff_t *ppos)
{
	if (!droid_lkm_ks.proc_dointvec_minmax)
		return -ENOSYS;
	return droid_lkm_ks.proc_dointvec_minmax(table, write, buffer, lenp,
						 ppos);
}

__nocfi noinline int register_kprobe(struct kprobe *p)
{
	if (!droid_lkm_ks.register_kprobe)
		return -ENOSYS;
	return droid_lkm_ks.register_kprobe(p);
}

__nocfi noinline void unregister_kprobe(struct kprobe *p)
{
	if (droid_lkm_ks.unregister_kprobe)
		droid_lkm_ks.unregister_kprobe(p);
}

__nocfi noinline int kern_path(const char *name, unsigned int flags, struct path *path)
{
	if (!droid_lkm_ks.kern_path)
		return -ENOSYS;
	return droid_lkm_ks.kern_path(name, flags, path);
}

__nocfi noinline void path_put(const struct path *path)
{
	if (droid_lkm_ks.path_put)
		droid_lkm_ks.path_put(path);
}

__nocfi noinline void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op)
{
	/*
	 * a kernel without the helper keeps the default dentry operations: the
	 * lookups still work, the dentry only stays cached longer
	 */
	if (droid_lkm_ks.d_set_d_op)
		droid_lkm_ks.d_set_d_op(dentry, op);
}

__nocfi noinline int down_write_killable(struct rw_semaphore *sem)
{
	/*
	 * the callers of the mmap_write_lock_killable() inline accept an
	 * uninterruptible acquire, which is what every kernel did before the
	 * killable variant existed
	 */
	if (!droid_lkm_ks.down_write_killable) {
		down_write(sem);
		return 0;
	}
	return droid_lkm_ks.down_write_killable(sem);
}

__nocfi noinline int radix_tree_tagged(const struct radix_tree_root *root, unsigned int tag)
{
	/* an idr that cannot be probed reads as not empty, which only keeps state */
	if (!droid_lkm_ks.radix_tree_tagged)
		return 0;
	return droid_lkm_ks.radix_tree_tagged(root, tag);
}

__nocfi noinline int vfs_unlink(DROID_LKM_IDMAP_PARAM struct inode *dir, struct dentry *dentry,
	       struct inode **deleted)
{
	if (!droid_lkm_ks.vfs_unlink)
		return -ENOSYS;
	return droid_lkm_ks.vfs_unlink(DROID_LKM_IDMAP_PASS dir, dentry,
				       deleted);
}

typeof(&getname) droid_lkm_p_getname;
typeof(&putname) droid_lkm_p_putname;
typeof(&get_next_ino) droid_lkm_p_get_next_ino;
typeof(&vfs_mkobj) droid_lkm_p_vfs_mkobj;
typeof(&fs_context_for_mount) droid_lkm_p_fs_context_for_mount;
typeof(&get_tree_keyed) droid_lkm_p_get_tree_keyed;
typeof(&netlink_getsockbyfilp) droid_lkm_p_netlink_getsockbyfilp;
typeof(&netlink_attachskb) droid_lkm_p_netlink_attachskb;
typeof(&netlink_sendskb) droid_lkm_p_netlink_sendskb;
typeof(&netlink_detachskb) droid_lkm_p_netlink_detachskb;
typeof(&schedule_hrtimeout_range_clock) droid_lkm_p_schedule_hrtimeout_range_clock;
#ifdef CONFIG_COMPAT
typeof(&get_compat_sigevent) droid_lkm_p_get_compat_sigevent;
#endif

/*
 * the queues are charged against RLIMIT_MSGQUEUE through the ucounts of the
 * caller. 5.10 has neither UCOUNT_RLIMIT_MSGQUEUE nor current_ucounts(), so the
 * accounting cannot be written for it at all and is left out, which is what
 * that kernel did: the queue keeps working, it is just not counted against the
 * user. on the branches that have it, a symbol trimmed from the image by
 * TRIM_UNUSED_KSYMS degrades the same way instead of failing the load.
 */
__nocfi noinline struct ucounts *droid_lkm_mq_ucounts_get(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	if (!droid_lkm_ks.ucounts_get)
		return NULL;
	return droid_lkm_ks.ucounts_get(current_ucounts());
#else
	return NULL;
#endif
}

__nocfi noinline long droid_lkm_mq_ucounts_charge(struct ucounts *ucounts, unsigned long bytes)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	if (droid_lkm_ks.ucounts_inc_rlimit)
		return droid_lkm_ks.ucounts_inc_rlimit(ucounts,
						       UCOUNT_RLIMIT_MSGQUEUE,
						       bytes);
#endif
	return 0;
}

__nocfi noinline void droid_lkm_mq_ucounts_uncharge(struct ucounts *ucounts, unsigned long bytes)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	if (droid_lkm_ks.ucounts_dec_rlimit)
		droid_lkm_ks.ucounts_dec_rlimit(ucounts,
						UCOUNT_RLIMIT_MSGQUEUE,
						bytes);
#endif
}

__nocfi noinline void droid_lkm_mq_ucounts_put(struct ucounts *ucounts)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	if (droid_lkm_ks.ucounts_put)
		droid_lkm_ks.ucounts_put(ucounts);
#endif
}

static bool droid_lkm_mqueue_shim_ok;


static bool droid_lkm_mqueue_enabled = true;
module_param_named(mqueue, droid_lkm_mqueue_enabled, bool, 0444);
MODULE_PARM_DESC(mqueue,
	"ported POSIX mqueue (mqueuefs plus the 6 syscall slots), default 1");

#define DROID_LKM_MQ_RESOLVE(_p, _name)                                        \
	do {                                                                   \
		(_p) = (typeof(_p))droid_lkm_sym(_name);                       \
		if (!(_p)) {                                                   \
			droid_lkm_warn("mqueue shim: %s not found\n", _name);  \
			missing++;                                             \
		}                                                              \
	} while (0)

__nocfi noinline int droid_lkm_mqueue_shim_init(void)
{
	int missing = 0;

	if (!droid_lkm_mqueue_enabled) {
		droid_lkm_info("POSIX mqueue disabled by param\n");
		return -ENODATA;
	}

	/*
	 * whether the kernel serves POSIX mqueue is asked of the running kernel,
	 * never of the config this module was built with: the DDK
	 * android14-6.1 kdir carries CONFIG_POSIX_MQUEUE=y while the 6.1 device
	 * kernel has it off, so a compile time branch leaves that device with
	 * neither the kernel's mqueue nor ours. a kernel that serves mq_open owns
	 * mqueuefs, the fs/mqueue sysctls and the six syscall slots, and
	 * registering our table next to its own fails on a duplicate entry
	 */
	if (droid_lkm_caps.posix_mqueue.owner == DROID_LKM_KERNEL) {
		droid_lkm_info("POSIX mqueue is the running kernel's (%s), module mqueue stays off\n",
			       droid_lkm_caps.posix_mqueue.reason);
		return -ENODATA;
	}
	if (droid_lkm_caps.posix_mqueue.owner != DROID_LKM_MODULE) {
		droid_lkm_info("mqueue stays off: %s\n",
			       droid_lkm_caps.posix_mqueue.reason);
		return -ENODATA;
	}
	droid_lkm_info("POSIX mqueue: %s, installing the module port\n",
		       droid_lkm_caps.posix_mqueue.reason);

	DROID_LKM_MQ_RESOLVE(droid_lkm_p_getname, "getname");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_putname, "putname");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_get_next_ino, "get_next_ino");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_vfs_mkobj, "vfs_mkobj");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_fs_context_for_mount,
			     "fs_context_for_mount");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_get_tree_keyed, "get_tree_keyed");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_netlink_getsockbyfilp,
			     "netlink_getsockbyfilp");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_netlink_attachskb,
			     "netlink_attachskb");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_netlink_sendskb, "netlink_sendskb");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_netlink_detachskb,
			     "netlink_detachskb");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_schedule_hrtimeout_range_clock,
			     "schedule_hrtimeout_range_clock");
#ifdef CONFIG_COMPAT
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_get_compat_sigevent,
			     "get_compat_sigevent");
#endif

	if (missing) {
		droid_lkm_warn("mqueue shim: %d symbol(s) missing, POSIX mqueue disabled\n",
			       missing);
		return -ENODATA;
	}

	/*
	 * mqueue cannot mount or open a queue without these, so they join the
	 * readiness gate instead of degrading one operation at a time
	 */
	if (!droid_lkm_ks.fc_mount || !droid_lkm_ks.put_fs_context ||
	    !droid_lkm_ks.dentry_open || !droid_lkm_ks.mntget) {
		droid_lkm_warn("mqueue shim: mount/open helpers missing (fc_mount=%p put_fs_context=%p dentry_open=%p mntget=%p), POSIX mqueue disabled\n",
			       droid_lkm_ks.fc_mount, droid_lkm_ks.put_fs_context,
			       droid_lkm_ks.dentry_open, droid_lkm_ks.mntget);
		return -ENODATA;
	}

	if (!droid_lkm_ks.ucounts_get || !droid_lkm_ks.ucounts_put ||
	    !droid_lkm_ks.ucounts_inc_rlimit || !droid_lkm_ks.ucounts_dec_rlimit)
		droid_lkm_warn("mqueue: RLIMIT_MSGQUEUE accounting unavailable, queues not charged\n");

	droid_lkm_mqueue_shim_ok = true;
	droid_lkm_dbg("mqueue shim: 12 unexported VFS/netlink/timer helpers resolved\n");
	return 0;
}

bool droid_lkm_mqueue_ready(void)
{
	return droid_lkm_mqueue_shim_ok;
}

/*
 * the filesystem registration is the ownership test, and it runs after this
 * shim has already claimed readiness: a kernel whose own mqueuefs holds the
 * name serves POSIX mqueue itself, so the claim is retracted before the syscall
 * slots are wired
 */
void droid_lkm_mqueue_shim_retract(void)
{
	droid_lkm_mqueue_shim_ok = false;
}

/*
 * mq_put_mnt lives in ipc/namespace.c upstream, not mqueue.c
 */
void droid_lkm_mq_put_mnt(struct ipc_namespace *ns)
{
	if (ns->mq_mnt)
		kern_unmount(ns->mq_mnt);
}

static int droid_lkm_mq_d_delete(const struct dentry *dentry)
{
	return 1;	
}

static const struct dentry_operations droid_lkm_mq_dops = {
	.d_delete = droid_lkm_mq_d_delete,
};

int droid_lkm_mq_simple_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	buf->f_type = dentry->d_sb->s_magic;
	buf->f_bsize = PAGE_SIZE;
	buf->f_namelen = NAME_MAX;
	return 0;
}

struct dentry *droid_lkm_mq_simple_lookup(struct inode *dir,
					  struct dentry *dentry,
					  unsigned int flags)
{
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	if (!dentry->d_sb->s_d_op)
		d_set_d_op(dentry, &droid_lkm_mq_dops);
	d_add(dentry, NULL);
	return NULL;
}

void droid_lkm_mq_kill_sb(struct super_block *sb)
{
	kill_anon_super(sb);
}

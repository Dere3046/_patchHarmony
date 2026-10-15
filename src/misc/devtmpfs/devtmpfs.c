// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * devtmpfs, ported from drivers/base/devtmpfs.c (android16-6.12) because this
 * device kernel is built with CONFIG_DEVTMPFS off.
 *
 * The shape is upstream's. An internal mount holds the node tree, a kernel
 * thread owns it, and the driver core feeds it one request per device. A mount
 * of "devtmpfs" by anyone returns that same superblock, which is what makes it
 * one /dev for the whole system.
 *
 * Four things differ from upstream, all deliberate.
 *
 *   - Upstream mounts the result over /dev during boot. This module is loaded
 *     long after the platform's device manager has built /dev, and mounting over
 *     it would hide the nodes already there, labels included. The filesystem is
 *     registered and fully usable, and nothing is mounted by itself.
 *   - Upstream resolves the names the driver core reports as relative paths
 *     inside a chroot its worker thread makes for itself. That needs
 *     ksys_unshare(), init_mount() and init_chroot(), and the first version of
 *     this port found out why it cannot be copied: path_init() returns early for
 *     the string "/" without following the mount just put there, so the chroot
 *     silently did not happen and every node landed in the host root. The names
 *     are walked as dentries inside the internal mount here, which cannot
 *     resolve anywhere else.
 *   - Upstream is built in and never tears down. This keeps a reference to the
 *     module in the superblock (.owner below), which is the kernel's own way of
 *     saying "my superblock points at my memory": rmmod is refused while any
 *     mount of devtmpfs is still alive, including one that was lazily unmounted
 *     and is waiting for its deferred cleanup. The internal mount is what keeps
 *     a superblock alive in the quiet case, so it is dropped by an explicit
 *     release step (/sys/module/droid_lkm_misc/parameters/release) before
 *     unloading, the same discipline as unmounting a filesystem before rmmod.
 *   - Upstream sees every device register from early boot. This walks the
 *     devices that already exist first, through devices_kset.
 *
 * devtmpfs_create_node() and devtmpfs_delete_node() are what device_add() and
 * device_del() call, and those two calls are compiled out with the same option,
 * so this hooks those two functions instead. device_get_devnode() lives in
 * drivers/base/core.c and is present either way, so it is called, not ported.
 */

#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/dcache.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/ramfs.h>
#include <linux/shmem_fs.h>
#include <linux/mount.h>
#include <uapi/linux/mount.h>
#include <linux/stat.h>
#include <linux/version.h>
#include <linux/user_namespace.h>
#include <linux/mnt_idmapping.h>

#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "ds.h"
#include "ds_caps.h"
#include "ds_ti.h"
#include "hk_inline.h"
#include "devtmpfs.h"

/*
 * every entry below is a kernel function reached by name and called through
 * this pointer, so each one has to be the kernel's own type. clang's cfi hashes
 * the whole function type at the call, so a prototype that differs from the
 * kernel's by one qualifier is a fatal cfi failure on the device rather than a
 * warning. this file took a device down over a const on device_get_devnode's
 * device argument, which is why the types are not written here at all.
 *
 * typeof(&symbol) is the declaration the build is already compiling against,
 * which is the point of building inside the ddk: the same headers and the same
 * clang as the kernel that will load this. the vfs family's first argument
 * changed shape twice on the way here, absent until 5.11, a user namespace from
 * 5.12, an id mapping from 6.3, and none of that has to be spelled out now. the
 * value handed in at the call sites is droid_lkm_caps.idmap_none, which is
 * nop_mnt_idmap or init_user_ns for the running kernel.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
/*
 * device_get_devnode left <linux/device.h> for the driver core's own header in
 * 6.3. the ddk tree is on the include path (the Makefile adds it), which is
 * what keeps the field below the kernel's type instead of a second guess at it
 */
#include <drivers/base/base.h>
#endif

struct droid_lkm_devtmpfs_ksym {
	typeof(&vfs_kern_mount)		vfs_kern_mount;
	typeof(&register_filesystem)	register_filesystem;
	typeof(&unregister_filesystem)	unregister_filesystem;
	typeof(&reconfigure_single)	reconfigure_single;
	typeof(&deactivate_locked_super) deactivate_locked_super;
	typeof(&kern_unmount)		kern_unmount;
	typeof(&shmem_init_fs_context)	shmem_init_fs_context;
	typeof(&kill_litter_super)	kill_litter_super;

	typeof(&lookup_one_len)		lookup_one_len;
	/* namespaced export (VFS_internal_...), so resolved rather than imported */
	typeof(&dget_parent)		dget_parent;
	typeof(&vfs_mknod)		vfs_mknod;
	typeof(&vfs_mkdir)		vfs_mkdir;
	typeof(&notify_change)		notify_change;
	typeof(&vfs_unlink)		vfs_unlink;
	typeof(&vfs_rmdir)		vfs_rmdir;
	typeof(&vfs_getattr)		vfs_getattr;

	typeof(&device_get_devnode)	device_get_devnode;
	const struct class		*block_class;
	/* every device the driver core knows, walked once to fill the tree */
	struct kset			*devices_kset;
};

static struct droid_lkm_devtmpfs_ksym droid_lkm_devtmpfs_ks;

static struct task_struct *droid_lkm_devtmpfs_thread;
static struct vfsmount *droid_lkm_devtmpfs_mnt;
static bool droid_lkm_devtmpfs_registered;
static unsigned long droid_lkm_devtmpfs_nodes;

/* how many node failures have been named, so a walk of hundreds stays readable */
static unsigned int droid_lkm_devtmpfs_fail_log;

/*
 * what a lock's state looks like from the outside, so the mount path can be read
 * instead of argued about. struct rw_semaphore's count is its first member on
 * every branch this module builds for, and s_active says which reference is left.
 * the question this answers is whether the write lock the mount takes is given
 * back, and by whom -- a count that stays off the unlocked value after up_write()
 * names the call that is holding it
 */
static void droid_lkm_devtmpfs_lock_state(const char *where, struct super_block *s)
{
	droid_lkm_misc_dbg("devtmpfs: %s: s_umount.count=0x%lx s_active=%d\n", where,
			   (unsigned long)atomic_long_read(&s->s_umount.count),
			   atomic_read(&s->s_active));
}

static struct hk_inline droid_lkm_devtmpfs_add_hook;
static struct hk_inline droid_lkm_devtmpfs_del_hook;
static int (*droid_lkm_devtmpfs_add_orig)(struct device *dev);
static void (*droid_lkm_devtmpfs_del_orig)(struct device *dev);

/* upstream's is_blockdev(), which is a static inline in drivers/base/base.h */
static bool droid_lkm_devtmpfs_is_blockdev(struct device *dev)
{
	return droid_lkm_devtmpfs_ks.block_class &&
	       dev->class == droid_lkm_devtmpfs_ks.block_class;
}

static DEFINE_SPINLOCK(droid_lkm_devtmpfs_req_lock);

struct droid_lkm_devtmpfs_req {
	struct droid_lkm_devtmpfs_req *next;
	struct completion done;
	int err;
	const char *name;
	umode_t mode;	/* 0 means delete */
	kuid_t uid;
	kgid_t gid;
	struct device *dev;
};

static struct droid_lkm_devtmpfs_req *droid_lkm_devtmpfs_requests;

/*
 * latched when a request went unanswered past the timeout. the thread handle is
 * deliberately left alone: only the unload path owns kthread_stop(), and it
 * decides on the pointer this field holds, so clearing it from a submit would
 * strand the thread with nobody left able to stop it. this flag is what stops
 * the driver core from queueing more work behind a stuck worker
 */
static bool droid_lkm_devtmpfs_worker_lost;

/* mount setup against teardown, so a release cannot kill the superblock mid mount */
static DEFINE_MUTEX(droid_lkm_devtmpfs_lock);

static struct dentry *droid_lkm_devtmpfs_public_mount(struct file_system_type *fs_type,
						      int flags, const char *dev_name,
						      void *data)
{
	struct super_block *s;
	struct dentry *root;
	int err;

	droid_lkm_misc_dbg("devtmpfs: mount enter: %s[%d] flags=0x%x data=%s\n",
			   current->comm, current->pid, flags,
			   data ? (const char *)data : "-");

	/*
	 * a tree with no nodes is not a devtmpfs, and serving one is worse than
	 * serving nothing. the mount itself is what goes wrong: this path hands the
	 * vfs a mount of the internal superblock, and on this kernel the mount
	 * machinery leaves that superblock's write lock held once it has run --
	 * a probe read s_umount.count as 0x0 as this function returned and the very
	 * next mount blocked on the same lock. everything that walks superblocks
	 * then blocks with it, sync included, and a container whose /dev is this
	 * filesystem cannot be stopped: its mount never returns and SIGKILL cannot
	 * reach the waiters.
	 *
	 * the walk refuses every node on this device (623 devices carried a devt and
	 * not one vfs_mknod succeeded -- EPERM and EACCES, from the kernel thread's
	 * context rather than from the superblock, which takes a userspace mknod
	 * without complaint), so the count stays zero and this refuses. when that is
	 * fixed the count goes up on its own and mounts are served again.
	 */
	if (!droid_lkm_devtmpfs_nodes) {
		droid_lkm_misc_warn("devtmpfs: refusing a mount, the tree has no nodes\n");
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&droid_lkm_devtmpfs_lock);

	if (!droid_lkm_devtmpfs_mnt) {
		mutex_unlock(&droid_lkm_devtmpfs_lock);
		return ERR_PTR(-ENODEV);
	}

	/* one instance for the whole system, the internal superblock */
	s = droid_lkm_devtmpfs_mnt->mnt_sb;
	atomic_inc(&s->s_active);
	/*
	 * killable on purpose. an uninterruptible down_write leaves the mounting
	 * task in D with SIGKILL unable to reach it, which is how a container whose
	 * /dev is this filesystem became impossible to stop: the mount never
	 * returned, systemd waited on it for good, and the only way out was a
	 * reboot. an interrupted wait fails the mount with EINTR and leaves the
	 * caller killable, which is the right answer for a lock that should not be
	 * held in the first place. the shim falls back to down_write where the
	 * kernel does not carry the killable variant
	 */
	if (down_write_killable(&s->s_umount)) {
		droid_lkm_misc_warn("devtmpfs: mount interrupted waiting for s_umount, a previous holder never released it\n");
		atomic_dec(&s->s_active);
		mutex_unlock(&droid_lkm_devtmpfs_lock);
		return ERR_PTR(-EINTR);
	}
	droid_lkm_devtmpfs_lock_state("locked", s);

	err = droid_lkm_devtmpfs_ks.reconfigure_single(s, flags, data);
	droid_lkm_misc_dbg("devtmpfs: reconfigure_single -> %d\n", err);
	if (err < 0) {
		/*
		 * the reference taken above is dropped here and the write lock is
		 * released here, neither of them through deactivate_locked_super().
		 * that call was taking the mount down twice over. the reference this
		 * function took is the only one at this point -- the mount has not
		 * reached the VFS far enough for it to hold one -- so s_active fell
		 * to zero and deactivate_locked_super() ran kill_sb(), which is
		 * kill_litter_super, over this module's own internal superblock: the
		 * tree every container's /dev is built from was being torn down by a
		 * mount that had failed. that is the device's
		 *
		 *     devtmpfs ready: 0 nodes
		 *
		 * with every vfs_mknod in the walk refused from then on.
		 *
		 * and its release of the write lock is conditional: put_super() only
		 * runs up_write(&s->s_umount) when s_count reaches zero, and the
		 * internal mount this module holds keeps s_count at one. so the lock
		 * this function took was never given back, and every sync
		 * (iterate_supers) and every later mount of devtmpfs blocked from then
		 * on for as long as the module stayed loaded -- which is why a
		 * container whose /dev is this filesystem could never be stopped.
		 */
		atomic_dec(&s->s_active);
		up_write(&s->s_umount);
		droid_lkm_devtmpfs_lock_state("released(err)", s);
		mutex_unlock(&droid_lkm_devtmpfs_lock);
		return ERR_PTR(err);
	}

	root = dget(s->s_root);
	/*
	 * reconfigure_single() is called with s_umount held and leaves it held, so
	 * this path releases it. the error path above releases it itself as well,
	 * which is the point of that change: the variant that hands the lock to
	 * deactivate_locked_super() is the one that never gave it back
	 */
	up_write(&s->s_umount);
	droid_lkm_devtmpfs_lock_state("released(ok)", s);
	mutex_unlock(&droid_lkm_devtmpfs_lock);

	return root;
}

static struct file_system_type droid_lkm_devtmpfs_internal_type = {
	.name		= "devtmpfs",
	.init_fs_context = NULL,	/* filled in from the kernel's shmem */
	.kill_sb	= NULL,
	/* alloc_super() refs the type owner and deactivate puts it back, so a live sb pins the module */
	.owner		= THIS_MODULE,
};

static struct file_system_type droid_lkm_devtmpfs_type = {
	.name		= "devtmpfs",
	.mount		= droid_lkm_devtmpfs_public_mount,
};

static void droid_lkm_devtmpfs_drain(void);

/*
 * one worker thread answers every request, and it is a kernel thread: a fault inside
 * a handler is answered by the panic guard with a task death, not a reboot, which
 * leaves nothing able to complete the request its caller is waiting on. the caller is
 * insmod, so the price of that used to be the whole shell in an uninterruptible wait
 * with nothing said about why. the wait is bounded here and the request lives on the
 * heap, so a worker that is only late still completes it without writing through a
 * stack frame that no longer exists, and a worker that is gone costs this component
 * instead of the caller.
 */
#define DROID_LKM_DEVTMPFS_REQ_TIMEOUT	(10 * HZ)

static int droid_lkm_devtmpfs_submit(const char *name, umode_t mode, kuid_t uid,
				     kgid_t gid, struct device *dev, const char *tmp)
{
	struct droid_lkm_devtmpfs_req *req;
	int err;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req) {
		kfree(tmp);
		return -ENOMEM;
	}

	req->name = name;
	req->mode = mode;
	req->uid = uid;
	req->gid = gid;
	req->dev = dev;
	init_completion(&req->done);

	spin_lock(&droid_lkm_devtmpfs_req_lock);
	if (!droid_lkm_devtmpfs_thread || droid_lkm_devtmpfs_worker_lost) {
		spin_unlock(&droid_lkm_devtmpfs_req_lock);
		kfree(tmp);
		kfree(req);
		return -ENODEV;
	}
	req->next = droid_lkm_devtmpfs_requests;
	droid_lkm_devtmpfs_requests = req;
	spin_unlock(&droid_lkm_devtmpfs_req_lock);

	wake_up_process(droid_lkm_devtmpfs_thread);

	if (!wait_for_completion_timeout(&req->done, DROID_LKM_DEVTMPFS_REQ_TIMEOUT)) {
		/* a worker that never answered will not answer the queued ones either */
		droid_lkm_misc_err("devtmpfs: %s went unanswered, the worker is stalled\n", name);
		droid_lkm_misc_report("devtmpfs", "unsupported", "worker stalled");
		spin_lock(&droid_lkm_devtmpfs_req_lock);
		droid_lkm_devtmpfs_worker_lost = true;
		spin_unlock(&droid_lkm_devtmpfs_req_lock);
		droid_lkm_devtmpfs_drain();
		/*
		 * both req and tmp are left behind on purpose: a worker that is only
		 * late reads req->name to finish, and that name may be tmp
		 */
		return -ETIMEDOUT;
	}

	err = req->err;
	kfree(tmp);
	kfree(req);

	return err;
}

static int droid_lkm_devtmpfs_create_node(struct device *dev)
{
	const char *tmp = NULL;
	const char *name;
	umode_t mode = 0;
	kuid_t uid = GLOBAL_ROOT_UID;
	kgid_t gid = GLOBAL_ROOT_GID;
	int ret;

	if (!droid_lkm_devtmpfs_thread)
		return 0;

	name = droid_lkm_devtmpfs_ks.device_get_devnode(dev, &mode, &uid, &gid, &tmp);
	if (!name)
		return -ENOMEM;

	if (mode == 0)
		mode = 0600;
	if (droid_lkm_devtmpfs_is_blockdev(dev))
		mode |= S_IFBLK;
	else
		mode |= S_IFCHR;

	ret = droid_lkm_devtmpfs_submit(name, mode, uid, gid, dev, tmp);
	/*
	 * the walk refuses silently otherwise, which is how a tree that ends up
	 * with no nodes at all says nothing about why
	 */
	if (ret && droid_lkm_devtmpfs_fail_log < 8) {
		droid_lkm_devtmpfs_fail_log++;
		droid_lkm_misc_warn("devtmpfs: %s: create failed: %d\n", name, ret);
	}

	return ret;
}

static int droid_lkm_devtmpfs_delete_node(struct device *dev)
{
	const char *tmp = NULL;
	const char *name;

	if (!droid_lkm_devtmpfs_thread)
		return 0;

	name = droid_lkm_devtmpfs_ks.device_get_devnode(dev, NULL, NULL, NULL, &tmp);
	if (!name)
		return -ENOMEM;

	/* mode 0 is what tells the worker this is a removal */
	return droid_lkm_devtmpfs_submit(name, 0, GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, dev, tmp);
}

/* a request can still be queued after the unhook, so fail what is left */
static void droid_lkm_devtmpfs_drain(void)
{
	struct droid_lkm_devtmpfs_req *req, *next;

	spin_lock(&droid_lkm_devtmpfs_req_lock);
	req = droid_lkm_devtmpfs_requests;
	droid_lkm_devtmpfs_requests = NULL;
	spin_unlock(&droid_lkm_devtmpfs_req_lock);

	while (req) {
		next = req->next;
		req->err = -ENODEV;
		complete(&req->done);
		req = next;
	}
}

/* walk the name inside the internal mount, create dirs, return the leaf length and its parent */
static int droid_lkm_devtmpfs_parent(const char *name, struct dentry **parent_out,
				     const char **leaf_out)
{
	struct dentry *parent = dget(droid_lkm_devtmpfs_mnt->mnt_root);
	const char *s = name;
	int err = 0;

	for (;;) {
		const char *slash;
		int len;
		struct dentry *next;

		/*
		 * this function and both of its callers lock a directory before walking
		 * into it, and locking an inode that is not there is a fault inside the
		 * rwsem whose log names the lock rather than the device it came from.
		 * a parent without an inode is refused here so no caller ever sees one
		 */
		if (!d_inode(parent)) {
			droid_lkm_misc_warn("devtmpfs: %s: %s has no inode\n", name,
					    parent == droid_lkm_devtmpfs_mnt->mnt_root ?
					    "the mount root" : "a walked directory");
			err = -ENOTDIR;
			break;
		}

		slash = strchr(s, '/');
		len = slash ? (int)(slash - s) : (int)strlen(s);

		if (!len) {
			err = -EINVAL;
			break;
		}

		if (!slash) {
			/* the last component is the caller's to create */
			*parent_out = parent;
			*leaf_out = s;
			return len;
		}

		inode_lock_nested(d_inode(parent), I_MUTEX_PARENT);
		next = droid_lkm_devtmpfs_ks.lookup_one_len(s, parent, len);
		if (IS_ERR(next)) {
			inode_unlock(d_inode(parent));
			err = PTR_ERR(next);
			break;
		}

		if (d_really_is_negative(next)) {
			err = droid_lkm_devtmpfs_ks.vfs_mkdir(droid_lkm_caps.idmap_none,
							      d_inode(parent), next,
							      0755);
			if (!err)
				/* mark as kernel-created inode */
				d_inode(next)->i_private = &droid_lkm_devtmpfs_thread;
		}
		inode_unlock(d_inode(parent));

		/*
		 * the old parent's reference is dropped only once this step
		 * succeeded and next is about to become the new parent. dropping it
		 * before the error test released it twice -- once here, once in the
		 * dput(parent) the loop leaves through -- so a vfs_mkdir that failed
		 * part way down a name put a dentry this module still held a
		 * reference to. dput() takes the count to zero, dentry_unlink_inode()
		 * clears d_inode, and every later step then reports the mount root
		 * itself as having no inode, which is what the device log showed for
		 * every device name before the report went down inside dput()
		 */
		if (err) {
			dput(next);
			break;
		}

		dput(parent);
		parent = next;
		s = slash + 1;
	}

	dput(parent);

	return err;
}

static int droid_lkm_devtmpfs_handle_create(const char *nodename, umode_t mode,
					    kuid_t uid, kgid_t gid, struct device *dev)
{
	struct dentry *parent, *dentry;
	const char *leaf;
	int len, err;

	len = droid_lkm_devtmpfs_parent(nodename, &parent, &leaf);
	if (len < 0)
		return len;

	inode_lock_nested(d_inode(parent), I_MUTEX_PARENT);
	dentry = droid_lkm_devtmpfs_ks.lookup_one_len(leaf, parent, len);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock;
	}

	err = droid_lkm_devtmpfs_ks.vfs_mknod(droid_lkm_caps.idmap_none, d_inode(parent), dentry,
					      mode, dev->devt);
	if (!err) {
		struct iattr newattrs;

		/*
		 * vfs_mknod() instantiates the dentry, so this is not expected to be
		 * negative; locking an inode that is not there is the fault this file
		 * already took once, and the check costs nothing next to a down_write
		 */
		if (!d_inode(dentry)) {
			droid_lkm_misc_warn("devtmpfs: %s: vfs_mknod left the dentry without an inode\n",
					    nodename);
			err = -EIO;
		} else {
			newattrs.ia_mode = mode;
			newattrs.ia_uid = uid;
			newattrs.ia_gid = gid;
			newattrs.ia_valid = ATTR_MODE | ATTR_UID | ATTR_GID;
			/* notify_change wants the affected inode locked by its caller */
			inode_lock(d_inode(dentry));
			droid_lkm_devtmpfs_ks.notify_change(droid_lkm_caps.idmap_none, dentry,
							    &newattrs, NULL);
			inode_unlock(d_inode(dentry));

			/* mark as kernel-created inode */
			d_inode(dentry)->i_private = &droid_lkm_devtmpfs_thread;
			droid_lkm_devtmpfs_nodes++;
		}
	}

	dput(dentry);
out_unlock:
	inode_unlock(d_inode(parent));
	dput(parent);

	return err;
}

/* remove the dirs a node left behind, consumes the reference handed in */
static void droid_lkm_devtmpfs_prune(struct dentry *dir)
{
	struct dentry *root = droid_lkm_devtmpfs_mnt->mnt_root;

	while (dir && dir != root) {
		struct dentry *up;
		int err;

		if (!d_really_is_positive(dir) ||
		    d_inode(dir)->i_private != &droid_lkm_devtmpfs_thread)
			break;

		up = droid_lkm_devtmpfs_ks.dget_parent(dir);
		if (!d_inode(up)) {
			/* a parent that is not a directory has nothing to remove from */
			dput(up);
			break;
		}
		inode_lock_nested(d_inode(up), I_MUTEX_PARENT);
		err = droid_lkm_devtmpfs_ks.vfs_rmdir(droid_lkm_caps.idmap_none, d_inode(up), dir);
		inode_unlock(d_inode(up));

		dput(dir);
		dir = up;
		if (err)
			break;
	}

	dput(dir);
}

static int droid_lkm_devtmpfs_mynode(struct device *dev, struct inode *inode,
				     struct kstat *stat)
{
	/* did we create it */
	if (inode->i_private != &droid_lkm_devtmpfs_thread)
		return 0;

	/* does the dev_t match */
	if (droid_lkm_devtmpfs_is_blockdev(dev)) {
		if (!S_ISBLK(stat->mode))
			return 0;
	} else {
		if (!S_ISCHR(stat->mode))
			return 0;
	}

	if (stat->rdev != dev->devt)
		return 0;

	/* ours */
	return 1;
}

static int droid_lkm_devtmpfs_handle_remove(const char *nodename, struct device *dev)
{
	struct dentry *parent, *dentry;
	const char *leaf;
	int len, err, deleted = 0;

	len = droid_lkm_devtmpfs_parent(nodename, &parent, &leaf);
	if (len < 0)
		return len;

	inode_lock_nested(d_inode(parent), I_MUTEX_PARENT);
	dentry = droid_lkm_devtmpfs_ks.lookup_one_len(leaf, parent, len);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock;
	}

	if (d_really_is_positive(dentry)) {
		struct kstat stat;
		struct path p = { .mnt = droid_lkm_devtmpfs_mnt, .dentry = dentry };

		err = droid_lkm_devtmpfs_ks.vfs_getattr(&p, &stat,
							STATX_TYPE | STATX_MODE,
							AT_STATX_SYNC_AS_STAT);
		if (!err && droid_lkm_devtmpfs_mynode(dev, d_inode(dentry), &stat)) {
			struct iattr newattrs;

			/*
			 * before unlinking this node, reset permissions
			 * of possible references like hardlinks
			 */
			newattrs.ia_uid = GLOBAL_ROOT_UID;
			newattrs.ia_gid = GLOBAL_ROOT_GID;
			newattrs.ia_mode = stat.mode & ~0777;
			newattrs.ia_valid = ATTR_UID | ATTR_GID | ATTR_MODE;
			inode_lock(d_inode(dentry));
			droid_lkm_devtmpfs_ks.notify_change(droid_lkm_caps.idmap_none, dentry,
							    &newattrs, NULL);
			inode_unlock(d_inode(dentry));

			err = droid_lkm_devtmpfs_ks.vfs_unlink(droid_lkm_caps.idmap_none,
							       d_inode(parent), dentry,
							       NULL);
			if (!err || err == -ENOENT) {
				deleted = 1;
				droid_lkm_devtmpfs_nodes--;
			}
		}
	} else {
		err = -ENOENT;
	}

	dput(dentry);
out_unlock:
	inode_unlock(d_inode(parent));

	if (deleted && strchr(nodename, '/'))
		droid_lkm_devtmpfs_prune(parent);
	else
		dput(parent);

	return err;
}

static int droid_lkm_devtmpfs_handle(const char *name, umode_t mode, kuid_t uid,
				     kgid_t gid, struct device *dev)
{
	if (mode)
		return droid_lkm_devtmpfs_handle_create(name, mode, uid, gid, dev);

	return droid_lkm_devtmpfs_handle_remove(name, dev);
}

static int droid_lkm_devtmpfs_thread_fn(void *p)
{
	while (!kthread_should_stop()) {
		spin_lock(&droid_lkm_devtmpfs_req_lock);
		while (droid_lkm_devtmpfs_requests) {
			struct droid_lkm_devtmpfs_req *req = droid_lkm_devtmpfs_requests;

			droid_lkm_devtmpfs_requests = NULL;
			spin_unlock(&droid_lkm_devtmpfs_req_lock);
			while (req) {
				struct droid_lkm_devtmpfs_req *next = req->next;

				req->err = droid_lkm_devtmpfs_handle(req->name, req->mode,
								     req->uid, req->gid,
								     req->dev);
				complete(&req->done);
				req = next;
			}
			spin_lock(&droid_lkm_devtmpfs_req_lock);
		}
		__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock(&droid_lkm_devtmpfs_req_lock);
		schedule();
	}

	return 0;
}

/* devices registered before this module loaded, refs taken under the kset lock */
static void droid_lkm_devtmpfs_populate(void)
{
	struct kobject *kobj;
	unsigned int count = 0, tried = 0, i = 0;
	struct device **devs;

	if (!droid_lkm_devtmpfs_ks.devices_kset)
		return;

	spin_lock(&droid_lkm_devtmpfs_ks.devices_kset->list_lock);
	list_for_each_entry(kobj, &droid_lkm_devtmpfs_ks.devices_kset->list, entry)
		count++;
	spin_unlock(&droid_lkm_devtmpfs_ks.devices_kset->list_lock);

	if (!count)
		return;

	devs = kcalloc(count, sizeof(*devs), GFP_KERNEL);
	if (!devs)
		return;

	spin_lock(&droid_lkm_devtmpfs_ks.devices_kset->list_lock);
	list_for_each_entry(kobj, &droid_lkm_devtmpfs_ks.devices_kset->list, entry) {
		if (i == count)
			break;
		devs[i] = get_device(kobj_to_dev(kobj));
		if (devs[i])
			i++;
	}
	spin_unlock(&droid_lkm_devtmpfs_ks.devices_kset->list_lock);

	while (i--) {
		if (MAJOR(devs[i]->devt)) {
			tried++;
			droid_lkm_devtmpfs_create_node(devs[i]);
		}
		put_device(devs[i]);
	}

	droid_lkm_misc_info("devtmpfs: walk: %u devices, %u with a devt, %lu nodes\n",
			    count, tried, droid_lkm_devtmpfs_nodes);

	kfree(devs);
}

__nocfi noinline int droid_lkm_devtmpfs_device_add(struct device *dev)
{
	int ret;

	if (!droid_lkm_devtmpfs_add_orig)
		return -ENODEV;

	ret = droid_lkm_devtmpfs_add_orig(dev);
	if (!ret && dev && MAJOR(dev->devt))
		droid_lkm_devtmpfs_create_node(dev);

	return ret;
}

__nocfi noinline void droid_lkm_devtmpfs_device_del(struct device *dev)
{
	if (!droid_lkm_devtmpfs_del_orig)
		return;

	droid_lkm_devtmpfs_del_orig(dev);
	if (dev && MAJOR(dev->devt))
		droid_lkm_devtmpfs_delete_node(dev);
}

static int droid_lkm_devtmpfs_resolve(void)
{
	struct {
		const char *name;
		void **slot;
	} syms[] = {
		{ "vfs_kern_mount",	(void **)&droid_lkm_devtmpfs_ks.vfs_kern_mount },
		{ "register_filesystem", (void **)&droid_lkm_devtmpfs_ks.register_filesystem },
		{ "unregister_filesystem", (void **)&droid_lkm_devtmpfs_ks.unregister_filesystem },
		{ "reconfigure_single",	(void **)&droid_lkm_devtmpfs_ks.reconfigure_single },
		{ "deactivate_locked_super", (void **)&droid_lkm_devtmpfs_ks.deactivate_locked_super },
		{ "kern_unmount",	(void **)&droid_lkm_devtmpfs_ks.kern_unmount },
		{ "shmem_init_fs_context", (void **)&droid_lkm_devtmpfs_ks.shmem_init_fs_context },
		{ "kill_litter_super",	(void **)&droid_lkm_devtmpfs_ks.kill_litter_super },
		{ "lookup_one_len",	(void **)&droid_lkm_devtmpfs_ks.lookup_one_len },
		{ "dget_parent",	(void **)&droid_lkm_devtmpfs_ks.dget_parent },
		{ "vfs_mknod",		(void **)&droid_lkm_devtmpfs_ks.vfs_mknod },
		{ "vfs_mkdir",		(void **)&droid_lkm_devtmpfs_ks.vfs_mkdir },
		{ "notify_change",	(void **)&droid_lkm_devtmpfs_ks.notify_change },
		{ "vfs_unlink",		(void **)&droid_lkm_devtmpfs_ks.vfs_unlink },
		{ "vfs_rmdir",		(void **)&droid_lkm_devtmpfs_ks.vfs_rmdir },
		{ "vfs_getattr",	(void **)&droid_lkm_devtmpfs_ks.vfs_getattr },
		{ "device_get_devnode",	(void **)&droid_lkm_devtmpfs_ks.device_get_devnode },
		{ "block_class",	(void **)&droid_lkm_devtmpfs_ks.block_class },
	};
	unsigned long devices_kset;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(syms); i++) {
		*syms[i].slot = (void *)droid_lkm_misc_sym(syms[i].name);
		if (!*syms[i].slot) {
			droid_lkm_misc_warn("devtmpfs: %s not found\n", syms[i].name);
			droid_lkm_misc_report("devtmpfs", "unsupported", syms[i].name);
			return -ENOENT;
		}
	}

	/*
	 * what the resolver handed back for the mount machinery, printed next to a
	 * symbol this file already depends on being right: device_get_devnode is the
	 * control, the names it returns are real device names. an address here that
	 * does not answer to /proc/kallsyms is a name the resolver took from a module
	 * symbol table instead of the kernel's, which matters because a shim answers
	 * a call without meaning to
	 */
	droid_lkm_misc_info("devtmpfs: vfs_kern_mount=0x%lx shmem_init_fs_context=0x%lx kill_litter_super=0x%lx lookup_one_len=0x%lx device_get_devnode=0x%lx\n",
			    (unsigned long)droid_lkm_devtmpfs_ks.vfs_kern_mount,
			    (unsigned long)droid_lkm_devtmpfs_ks.shmem_init_fs_context,
			    (unsigned long)droid_lkm_devtmpfs_ks.kill_litter_super,
			    (unsigned long)droid_lkm_devtmpfs_ks.lookup_one_len,
			    (unsigned long)droid_lkm_devtmpfs_ks.device_get_devnode);

	/* devices_kset is a pointer, the lookup returns its address, no kset means no walk */
	devices_kset = droid_lkm_misc_sym("devices_kset");
	if (!devices_kset ||
	    safe_read(&droid_lkm_devtmpfs_ks.devices_kset, (void *)devices_kset,
		      sizeof(droid_lkm_devtmpfs_ks.devices_kset)) ||
	    !droid_lkm_devtmpfs_ks.devices_kset) {
		droid_lkm_misc_warn("devtmpfs: devices_kset unusable\n");
		droid_lkm_misc_report("devtmpfs", "unsupported", "devices_kset");
		return -ENOENT;
	}

	return 0;
}

/*
 * the internal mount is the one object here that is read through fields this build
 * cannot be sure of: struct vfsmount, dentry and inode are walked directly and the
 * layout check only knows the namespace types. what the kernel handed back is
 * printed once and then tested rather than trusted. a root that is missing, has no
 * inode, or belongs to a superblock other than the mount's own means the tree the
 * worker would walk does not exist, and a worker that finds that out inside
 * down_write() costs the caller its shell instead of saying so.
 */
static int droid_lkm_devtmpfs_mount_check(void)
{
	struct vfsmount *mnt = droid_lkm_devtmpfs_mnt;
	struct dentry *root = NULL;
	struct super_block *sb = NULL;
	unsigned long words[4] = { 0, 0, 0, 0 };
	unsigned long root_inode = 0, root_sb = 0;

	if (safe_read(&root, &mnt->mnt_root, sizeof(root)) ||
	    safe_read(&sb, &mnt->mnt_sb, sizeof(sb)) ||
	    safe_read(words, mnt, sizeof(words))) {
		droid_lkm_misc_err("devtmpfs: the internal mount cannot be read\n");
		return -ENODEV;
	}

	if (root) {
		safe_read(&root_inode, &root->d_inode, sizeof(root_inode));
		safe_read(&root_sb, &root->d_sb, sizeof(root_sb));
	}

	droid_lkm_misc_info("devtmpfs: mount=0x%lx words %lx %lx %lx %lx root=0x%lx inode=0x%lx sb=0x%lx root sb=0x%lx\n",
			    (unsigned long)mnt, words[0], words[1], words[2], words[3],
			    (unsigned long)root, root_inode, (unsigned long)sb, root_sb);

	if (!root) {
		droid_lkm_misc_err("devtmpfs: the internal mount has no root\n");
		return -ENODEV;
	}
	if (!root_inode) {
		droid_lkm_misc_err("devtmpfs: the internal mount's root has no inode\n");
		return -ENODEV;
	}
	if (root_sb != (unsigned long)sb) {
		droid_lkm_misc_err("devtmpfs: the root is not a child of the mount's superblock\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * every field of a live kernel object this file dereferences. the rest of the
 * tree reads namespaces through the layout table; this half reads struct
 * dentry, inode, vfsmount, super_block, device and kset with the build's own
 * field offsets, because the kernel's inline helpers -- d_inode(),
 * d_really_is_negative(), inode_lock_nested() -- are written against the build
 * headers and cannot be redirected. a build offset is only right when the
 * running kernel places the field where the headers do, and one field off is
 * d_inode() answering with something that is not an inode, or down_write()
 * being handed an address that is not an rwsem: that is the fault that took the
 * device down in droid_lkm_devtmpfs_parent(), and the check it added there read
 * the same wrong offsets, so it could not catch it. the list is verified before
 * the mount is built and before the walk runs, and a field the running kernel
 * does not agree about turns the component off instead of leaving a walk that
 * dereferences the wrong thing
 */
static const enum droid_lkm_field_id droid_lkm_devtmpfs_fields[] = {
	DROID_LKM_F_VFSMOUNT_MNT_ROOT,
	DROID_LKM_F_VFSMOUNT_MNT_SB,
	DROID_LKM_F_SUPER_S_ROOT,
	DROID_LKM_F_SUPER_S_ACTIVE,
	DROID_LKM_F_SUPER_S_UMOUNT,
	DROID_LKM_F_DENTRY_D_INODE,
	DROID_LKM_F_DENTRY_D_SB,
	DROID_LKM_F_INODE_I_RWSEM,
	DROID_LKM_F_INODE_I_PRIVATE,
	DROID_LKM_F_DEVICE_KOBJ,
	DROID_LKM_F_DEVICE_CLASS,
	DROID_LKM_F_DEVICE_DEVT,
	DROID_LKM_F_KSET_LIST,
	DROID_LKM_F_KSET_LIST_LOCK,
};

int droid_lkm_misc_devtmpfs_init(void)
{
	const char *bad = NULL;
	char opts[] = "mode=0755";
	char detail[64];
	int err;

	if (!droid_lkm_layout_all_ok(droid_lkm_devtmpfs_fields,
				     ARRAY_SIZE(droid_lkm_devtmpfs_fields), &bad)) {
		droid_lkm_misc_err("devtmpfs: the running kernel does not place %s where this build does, refusing to walk live structs\n",
				   bad ? bad : "a struct field");
		droid_lkm_misc_report("devtmpfs", "unsupported", "struct layout");
		return -EOPNOTSUPP;
	}

	err = droid_lkm_devtmpfs_resolve();
	if (err)
		return err;

	droid_lkm_devtmpfs_internal_type.init_fs_context =
		droid_lkm_devtmpfs_ks.shmem_init_fs_context;
	droid_lkm_devtmpfs_internal_type.kill_sb = droid_lkm_devtmpfs_ks.kill_litter_super;

	/*
	 * flags 0, and not SB_KERNMOUNT. upstream reaches this object through
	 * kern_mount(), which passes SB_KERNMOUNT, and it was tried here: a mount
	 * of the public type then fails with -EINVAL before anything else happens,
	 * because the kernel that comes back is one userspace is not allowed to
	 * graft. the internal mount is made for userspace to mount through this
	 * type, so the flag does not belong
	 */
	droid_lkm_devtmpfs_mnt = droid_lkm_devtmpfs_ks.vfs_kern_mount(
		&droid_lkm_devtmpfs_internal_type, 0, "devtmpfs", opts);
	if (IS_ERR(droid_lkm_devtmpfs_mnt)) {
		err = PTR_ERR(droid_lkm_devtmpfs_mnt);
		droid_lkm_devtmpfs_mnt = NULL;
		droid_lkm_misc_err("devtmpfs: internal mount failed: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "internal mount failed");
		return err;
	}

	err = droid_lkm_devtmpfs_mount_check();
	if (err) {
		droid_lkm_misc_report("devtmpfs", "unsupported", "internal mount");
		goto out_unmount;
	}

	err = droid_lkm_devtmpfs_ks.register_filesystem(&droid_lkm_devtmpfs_type);
	if (err) {
		droid_lkm_misc_err("devtmpfs: register failed: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "register failed");
		goto out_unmount;
	}
	droid_lkm_devtmpfs_registered = true;

	droid_lkm_devtmpfs_thread = kthread_run(droid_lkm_devtmpfs_thread_fn, NULL,
						"ddevtmpfs");
	if (IS_ERR(droid_lkm_devtmpfs_thread)) {
		err = PTR_ERR(droid_lkm_devtmpfs_thread);
		droid_lkm_devtmpfs_thread = NULL;
		droid_lkm_misc_err("devtmpfs: no thread: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "no thread");
		goto out_unregister;
	}

	err = droid_lkm_inline_hook_checked(&droid_lkm_devtmpfs_add_hook, "device_add",
				    "droid_lkm_devtmpfs_device_add");
	if (err) {
		droid_lkm_misc_warn("devtmpfs: hook device_add failed: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "device_add hook failed");
		goto out_stop;
	}
	droid_lkm_devtmpfs_add_orig = (void *)droid_lkm_devtmpfs_add_hook.orig;

	err = droid_lkm_inline_hook_checked(&droid_lkm_devtmpfs_del_hook, "device_del",
				    "droid_lkm_devtmpfs_device_del");
	if (err) {
		droid_lkm_misc_warn("devtmpfs: hook device_del failed: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "device_del hook failed");
		goto out_unhook_add;
	}
	droid_lkm_devtmpfs_del_orig = (void *)droid_lkm_devtmpfs_del_hook.orig;

	droid_lkm_devtmpfs_populate();

	scnprintf(detail, sizeof(detail), "%lu nodes, fs registered",
		  droid_lkm_devtmpfs_nodes);
	droid_lkm_misc_report("devtmpfs", "ready", detail);
	droid_lkm_misc_info("devtmpfs ready: %lu nodes\n", droid_lkm_devtmpfs_nodes);

	return 0;

out_unhook_add:
	hk_inline_unhook(&droid_lkm_devtmpfs_add_hook);
	droid_lkm_devtmpfs_add_orig = NULL;
out_stop:
	kthread_stop(droid_lkm_devtmpfs_thread);
	droid_lkm_devtmpfs_thread = NULL;
out_unregister:
	droid_lkm_devtmpfs_ks.unregister_filesystem(&droid_lkm_devtmpfs_type);
	droid_lkm_devtmpfs_registered = false;
out_unmount:
	droid_lkm_devtmpfs_ks.kern_unmount(droid_lkm_devtmpfs_mnt);
	droid_lkm_devtmpfs_mnt = NULL;
	return err;
}

/* drop the internal mount so the superblock can die and release the module ref */
int droid_lkm_misc_devtmpfs_release(void)
{
	if (!droid_lkm_devtmpfs_mnt && !droid_lkm_devtmpfs_registered) {
		droid_lkm_misc_report("devtmpfs", "released", "already released");
		return 0;
	}

	if (droid_lkm_devtmpfs_del_orig) {
		hk_inline_unhook(&droid_lkm_devtmpfs_del_hook);
		droid_lkm_devtmpfs_del_orig = NULL;
	}
	if (droid_lkm_devtmpfs_add_orig) {
		hk_inline_unhook(&droid_lkm_devtmpfs_add_hook);
		droid_lkm_devtmpfs_add_orig = NULL;
	}

	mutex_lock(&droid_lkm_devtmpfs_lock);

	if (droid_lkm_devtmpfs_thread) {
		struct task_struct *thread = droid_lkm_devtmpfs_thread;

		/* clear first, submit() must not queue behind a dead thread */
		droid_lkm_devtmpfs_thread = NULL;
		kthread_stop(thread);
		droid_lkm_devtmpfs_drain();
	}

	if (droid_lkm_devtmpfs_registered) {
		droid_lkm_devtmpfs_ks.unregister_filesystem(&droid_lkm_devtmpfs_type);
		droid_lkm_devtmpfs_registered = false;
	}

	if (droid_lkm_devtmpfs_mnt) {
		droid_lkm_devtmpfs_ks.kern_unmount(droid_lkm_devtmpfs_mnt);
		droid_lkm_devtmpfs_mnt = NULL;
	}

	mutex_unlock(&droid_lkm_devtmpfs_lock);

	droid_lkm_misc_report("devtmpfs", "released", "internal mount dropped");
	droid_lkm_misc_info("devtmpfs released\n");

	return 0;
}

void droid_lkm_misc_devtmpfs_exit(void)
{
	int err;

	if (droid_lkm_devtmpfs_del_orig) {
		hk_inline_unhook(&droid_lkm_devtmpfs_del_hook);
		droid_lkm_devtmpfs_del_orig = NULL;
	}
	if (droid_lkm_devtmpfs_add_orig) {
		hk_inline_unhook(&droid_lkm_devtmpfs_add_hook);
		droid_lkm_devtmpfs_add_orig = NULL;
	}

	mutex_lock(&droid_lkm_devtmpfs_lock);

	if (droid_lkm_devtmpfs_thread) {
		struct task_struct *thread = droid_lkm_devtmpfs_thread;

		/* clear it first: submit() must not queue behind a dead thread */
		droid_lkm_devtmpfs_thread = NULL;
		kthread_stop(thread);
		droid_lkm_devtmpfs_drain();
	}

	if (droid_lkm_devtmpfs_registered) {
		/* reaching exit means no mount is left, this only reports a never registered type */
		err = droid_lkm_devtmpfs_ks.unregister_filesystem(&droid_lkm_devtmpfs_type);
		if (err)
			droid_lkm_misc_warn("devtmpfs: unregister: %d\n", err);
		droid_lkm_devtmpfs_registered = false;
	}

	if (droid_lkm_devtmpfs_mnt) {
		droid_lkm_devtmpfs_ks.kern_unmount(droid_lkm_devtmpfs_mnt);
		droid_lkm_devtmpfs_mnt = NULL;
	}

	mutex_unlock(&droid_lkm_devtmpfs_lock);
}

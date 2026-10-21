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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#include <linux/mnt_idmapping.h>
#include <drivers/base/base.h>
#endif

#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "hk_inline.h"
#include "devtmpfs.h"

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
	/* the worker creates nodes as the loader, not as the kernel domain */
	typeof(&get_task_cred)		get_task_cred;
	typeof(&override_creds)		override_creds;
	typeof(&revert_creds)		revert_creds;
	const struct class		*block_class;
	/* every device the driver core knows, walked once to fill the tree */
	struct kset			*devices_kset;
};

static struct droid_lkm_devtmpfs_ksym droid_lkm_devtmpfs_ks;

static struct task_struct *droid_lkm_devtmpfs_thread;
static struct vfsmount *droid_lkm_devtmpfs_mnt;
static bool droid_lkm_devtmpfs_registered;
static unsigned long droid_lkm_devtmpfs_nodes;
static const struct cred *droid_lkm_devtmpfs_node_cred;

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

/* mount setup against teardown, so a release cannot kill the superblock mid mount */
static DEFINE_MUTEX(droid_lkm_devtmpfs_lock);

static struct dentry *droid_lkm_devtmpfs_public_mount(struct file_system_type *fs_type,
						      int flags, const char *dev_name,
						      void *data)
{
	struct super_block *s;
	struct dentry *root;
	int err;

	mutex_lock(&droid_lkm_devtmpfs_lock);

	if (!droid_lkm_devtmpfs_mnt) {
		mutex_unlock(&droid_lkm_devtmpfs_lock);
		return ERR_PTR(-ENODEV);
	}

	/* one instance for the whole system, the internal superblock */
	s = droid_lkm_devtmpfs_mnt->mnt_sb;
	atomic_inc(&s->s_active);
	if (down_write_killable(&s->s_umount)) {
		droid_lkm_misc_warn("devtmpfs: mount interrupted waiting for s_umount\n");
		atomic_dec(&s->s_active);
		mutex_unlock(&droid_lkm_devtmpfs_lock);
		return ERR_PTR(-EINTR);
	}

	err = droid_lkm_devtmpfs_ks.reconfigure_single(s, flags, data);
	if (err < 0) {
		atomic_dec(&s->s_active);
		up_write(&s->s_umount);
		mutex_unlock(&droid_lkm_devtmpfs_lock);
		return ERR_PTR(err);
	}

	root = dget(s->s_root);
	/*
	 * the write lock stays held, and that is the contract this callback is
	 * called under: the VFS expects get_tree to return with s_umount write-locked
	 * and releases it itself via up_write(&sb->s_umount) in do_new_mount_fc().
	 */
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

static int droid_lkm_devtmpfs_submit(struct droid_lkm_devtmpfs_req *req,
				     const char *tmp)
{
	init_completion(&req->done);

	spin_lock(&droid_lkm_devtmpfs_req_lock);
	if (!droid_lkm_devtmpfs_thread) {
		spin_unlock(&droid_lkm_devtmpfs_req_lock);
		kfree(tmp);
		return -ENODEV;
	}
	req->next = droid_lkm_devtmpfs_requests;
	droid_lkm_devtmpfs_requests = req;
	spin_unlock(&droid_lkm_devtmpfs_req_lock);

	wake_up_process(droid_lkm_devtmpfs_thread);
	wait_for_completion(&req->done);

	kfree(tmp);

	return req->err;
}

static int droid_lkm_devtmpfs_create_node(struct device *dev)
{
	const char *tmp = NULL;
	struct droid_lkm_devtmpfs_req req;

	if (!droid_lkm_devtmpfs_thread)
		return 0;

	req.mode = 0;
	req.uid = GLOBAL_ROOT_UID;
	req.gid = GLOBAL_ROOT_GID;
	req.name = droid_lkm_devtmpfs_ks.device_get_devnode(dev, &req.mode, &req.uid,
							    &req.gid, &tmp);
	if (!req.name)
		return -ENOMEM;

	if (req.mode == 0)
		req.mode = 0600;
	if (droid_lkm_devtmpfs_is_blockdev(dev))
		req.mode |= S_IFBLK;
	else
		req.mode |= S_IFCHR;

	req.dev = dev;

	return droid_lkm_devtmpfs_submit(&req, tmp);
}

static int droid_lkm_devtmpfs_delete_node(struct device *dev)
{
	const char *tmp = NULL;
	struct droid_lkm_devtmpfs_req req;

	if (!droid_lkm_devtmpfs_thread)
		return 0;

	req.name = droid_lkm_devtmpfs_ks.device_get_devnode(dev, NULL, NULL, NULL, &tmp);
	if (!req.name)
		return -ENOMEM;

	req.mode = 0;
	req.dev = dev;

	return droid_lkm_devtmpfs_submit(&req, tmp);
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static inline struct mnt_idmap *droid_lkm_devtmpfs_idmap(void)
{
	return (struct mnt_idmap *)droid_lkm_misc_ks.nop_mnt_idmap;
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static inline struct user_namespace *droid_lkm_devtmpfs_idmap(void)
{
	return &init_user_ns;
}
#endif

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

		if (!parent || !d_inode(parent)) {
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
			err = droid_lkm_devtmpfs_ks.vfs_mkdir(droid_lkm_devtmpfs_idmap(),
							      d_inode(parent), next,
							      0755);
			if (!err)
				/* mark as kernel-created inode */
				d_inode(next)->i_private = &droid_lkm_devtmpfs_thread;
		}
		inode_unlock(d_inode(parent));

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

	err = droid_lkm_devtmpfs_ks.vfs_mknod(droid_lkm_devtmpfs_idmap(), d_inode(parent), dentry,
					      mode, dev->devt);
	if (!err) {
		struct iattr newattrs;

		newattrs.ia_mode = mode;
		newattrs.ia_uid = uid;
		newattrs.ia_gid = gid;
		newattrs.ia_valid = ATTR_MODE | ATTR_UID | ATTR_GID;
		/* notify_change wants the affected inode locked by its caller */
		inode_lock(d_inode(dentry));
		droid_lkm_devtmpfs_ks.notify_change(droid_lkm_devtmpfs_idmap(), dentry, &newattrs,
						    NULL);
		inode_unlock(d_inode(dentry));

		/* mark as kernel-created inode */
		d_inode(dentry)->i_private = &droid_lkm_devtmpfs_thread;
		droid_lkm_devtmpfs_nodes++;
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
		inode_lock_nested(d_inode(up), I_MUTEX_PARENT);
		err = droid_lkm_devtmpfs_ks.vfs_rmdir(droid_lkm_devtmpfs_idmap(), d_inode(up), dir);
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
			droid_lkm_devtmpfs_ks.notify_change(droid_lkm_devtmpfs_idmap(), dentry,
							    &newattrs, NULL);
			inode_unlock(d_inode(dentry));

			err = droid_lkm_devtmpfs_ks.vfs_unlink(droid_lkm_devtmpfs_idmap(),
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
	const struct cred *old_cred = NULL;

	if (droid_lkm_devtmpfs_ks.override_creds && droid_lkm_devtmpfs_node_cred)
		old_cred = droid_lkm_devtmpfs_ks.override_creds(droid_lkm_devtmpfs_node_cred);

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

	if (droid_lkm_devtmpfs_ks.revert_creds && old_cred)
		droid_lkm_devtmpfs_ks.revert_creds(old_cred);

	return 0;
}

/* devices registered before this module loaded, refs taken under the kset lock */
static void droid_lkm_devtmpfs_populate(void)
{
	struct kobject *kobj;
	unsigned int count = 0, i = 0;
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
		if (MAJOR(devs[i]->devt))
			droid_lkm_devtmpfs_create_node(devs[i]);
		put_device(devs[i]);
	}

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
		{ "get_task_cred",	(void **)&droid_lkm_devtmpfs_ks.get_task_cred },
		{ "override_creds",	(void **)&droid_lkm_devtmpfs_ks.override_creds },
		{ "revert_creds",	(void **)&droid_lkm_devtmpfs_ks.revert_creds },
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

int droid_lkm_misc_devtmpfs_init(void)
{
	char opts[] = "mode=0755";
	char detail[64];
	int err;

	err = droid_lkm_devtmpfs_resolve();
	if (err)
		return err;

	droid_lkm_devtmpfs_internal_type.init_fs_context =
		droid_lkm_devtmpfs_ks.shmem_init_fs_context;
	droid_lkm_devtmpfs_internal_type.kill_sb = droid_lkm_devtmpfs_ks.kill_litter_super;

	droid_lkm_devtmpfs_mnt = droid_lkm_devtmpfs_ks.vfs_kern_mount(
		&droid_lkm_devtmpfs_internal_type, 0, "devtmpfs", opts);
	if (IS_ERR(droid_lkm_devtmpfs_mnt)) {
		err = PTR_ERR(droid_lkm_devtmpfs_mnt);
		droid_lkm_devtmpfs_mnt = NULL;
		droid_lkm_misc_err("devtmpfs: internal mount failed: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "internal mount failed");
		return err;
	}

	err = droid_lkm_devtmpfs_ks.register_filesystem(&droid_lkm_devtmpfs_type);
	if (err) {
		droid_lkm_misc_err("devtmpfs: register failed: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "register failed");
		goto out_unmount;
	}
	droid_lkm_devtmpfs_registered = true;

	if (droid_lkm_devtmpfs_ks.get_task_cred) {
		droid_lkm_devtmpfs_node_cred = droid_lkm_devtmpfs_ks.get_task_cred(current);
		if (!droid_lkm_devtmpfs_node_cred) {
			err = -ENOMEM;
			droid_lkm_misc_err("devtmpfs: cannot take loader credentials\n");
			goto out_unregister;
		}
	}

	droid_lkm_devtmpfs_thread = kthread_run(droid_lkm_devtmpfs_thread_fn, NULL,
						"ddevtmpfs");
	if (IS_ERR(droid_lkm_devtmpfs_thread)) {
		err = PTR_ERR(droid_lkm_devtmpfs_thread);
		droid_lkm_devtmpfs_thread = NULL;
		droid_lkm_misc_err("devtmpfs: no thread: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "no thread");
		goto out_creds;
	}

	err = hk_inline_hook(&droid_lkm_devtmpfs_add_hook, "device_add",
			     "droid_lkm_devtmpfs_device_add");
	if (err) {
		droid_lkm_misc_warn("devtmpfs: hook device_add failed: %d\n", err);
		droid_lkm_misc_report("devtmpfs", "unsupported", "device_add hook failed");
		goto out_stop;
	}
	droid_lkm_devtmpfs_add_orig = (void *)droid_lkm_devtmpfs_add_hook.orig;

	err = hk_inline_hook(&droid_lkm_devtmpfs_del_hook, "device_del",
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
out_creds:
	if (droid_lkm_devtmpfs_node_cred) {
		put_cred(droid_lkm_devtmpfs_node_cred);
		droid_lkm_devtmpfs_node_cred = NULL;
	}
out_unregister:
	droid_lkm_devtmpfs_ks.unregister_filesystem(&droid_lkm_devtmpfs_type);
	droid_lkm_devtmpfs_registered = false;
out_unmount:
	droid_lkm_devtmpfs_ks.kern_unmount(droid_lkm_devtmpfs_mnt);
	droid_lkm_devtmpfs_mnt = NULL;
	return err;
}

static void droid_lkm_devtmpfs_teardown(void)
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

		/* clear first: submit() must not queue behind a dead thread */
		droid_lkm_devtmpfs_thread = NULL;
		kthread_stop(thread);
		droid_lkm_devtmpfs_drain();
	}

	if (droid_lkm_devtmpfs_node_cred) {
		put_cred(droid_lkm_devtmpfs_node_cred);
		droid_lkm_devtmpfs_node_cred = NULL;
	}

	if (droid_lkm_devtmpfs_registered) {
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

/* drop the internal mount so the superblock can die and release the module ref */
int droid_lkm_misc_devtmpfs_release(void)
{
	if (!droid_lkm_devtmpfs_mnt && !droid_lkm_devtmpfs_registered) {
		droid_lkm_misc_report("devtmpfs", "released", "already released");
		return 0;
	}

	droid_lkm_devtmpfs_teardown();

	droid_lkm_misc_report("devtmpfs", "released", "internal mount dropped");
	droid_lkm_misc_info("devtmpfs released\n");

	return 0;
}

void droid_lkm_misc_devtmpfs_exit(void)
{
	droid_lkm_devtmpfs_teardown();
}

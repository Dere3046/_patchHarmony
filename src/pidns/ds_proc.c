// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

#include "hk_patch.h"
#include "ds.h"
#include "ds_ipcns.h"
#include "ds_pidns.h"
#include "ds_proc.h"
#include "ds_nsops.h"
#include "proc_inode.h"

static struct inode_operations *droid_lkm_ns_dir_iops;
static const struct inode_operations *droid_lkm_ns_link_iops;
static const struct dentry_operations *droid_lkm_pid_dop;
static struct dentry *(*droid_lkm_orig_ns_lookup)(struct inode *dir,
					   struct dentry *dentry,
					   unsigned int flags);
static struct inode *(*droid_lkm_proc_pid_make_inode)(struct super_block *sb,
					       struct task_struct *task,
					       umode_t mode);

static const struct proc_ns_operations *droid_lkm_host_pid_ops;
static const struct proc_ns_operations *droid_lkm_host_pidfc_ops;
static const struct proc_ns_operations *droid_lkm_host_ipc_ops;

static struct dentry *droid_lkm_ns_instantiate(struct dentry *dentry,
					struct task_struct *task,
					const struct proc_ns_operations *ops)
{
	struct inode *inode;
	struct proc_inode *ei;

	if (!ops)
		return ERR_PTR(-ENOENT);

	inode = droid_lkm_proc_pid_make_inode(dentry->d_sb, task, S_IFLNK | S_IRWXUGO);
	if (!inode)
		return ERR_PTR(-ENOENT);

	ei = PROC_I(inode);
	inode->i_op = droid_lkm_ns_link_iops;
	ei->ns_ops = ops;

	if (droid_lkm_pid_dop)
		d_set_d_op(dentry, droid_lkm_pid_dop);

	return d_splice_alias(inode, dentry);
}

enum droid_lkm_ns_kind {
	DROID_LKM_NS_NONE = 0,
	DROID_LKM_NS_PID,
	DROID_LKM_NS_PID_FOR_CHILDREN,
	DROID_LKM_NS_IPC,
};

static enum droid_lkm_ns_kind droid_lkm_ns_name_kind(const char *name, unsigned int len)
{
	if (len == 3 && !memcmp(name, "pid", 3))
		return DROID_LKM_NS_PID;
	if (len == 16 && !memcmp(name, "pid_for_children", 16))
		return DROID_LKM_NS_PID_FOR_CHILDREN;
	if (len == 3 && !memcmp(name, "ipc", 3))
		return DROID_LKM_NS_IPC;
	return DROID_LKM_NS_NONE;
}

static struct dentry *droid_lkm_ns_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	enum droid_lkm_ns_kind kind;
	struct task_struct *task;
	struct dentry *res;


	res = droid_lkm_orig_ns_lookup(dir, dentry, flags);
	if (!IS_ERR(res) || PTR_ERR(res) != -ENOENT)
		return res;

	kind = droid_lkm_ns_name_kind(dentry->d_name.name, dentry->d_name.len);
	if (!kind)
		return res;

	task = get_proc_task(dir);
	if (!task)
		return ERR_PTR(-ENOENT);

	droid_lkm_dbg("proc ns lookup: %s[%d] name=%.*s kind=%d\n", task->comm,
	       task_pid_nr(task), (int)dentry->d_name.len, dentry->d_name.name,
	       (int)kind);

	if (kind == DROID_LKM_NS_IPC) {
		res = droid_lkm_ns_instantiate(dentry, task,
					droid_lkm_ipcns_task_is_host(task)
						? droid_lkm_host_ipc_ops
						: &droid_lkm_ipcns_ops);
	} else {
		struct pid_namespace *ns;

		ns = (kind == DROID_LKM_NS_PID_FOR_CHILDREN)
			     ? droid_lkm_pidns_task_for_children(task)
			     : task_active_pid_ns(task);

		if (droid_lkm_pidns_is_ours(ns))
			res = droid_lkm_ns_instantiate(
				dentry, task,
				(kind == DROID_LKM_NS_PID_FOR_CHILDREN)
					? &droid_lkm_pidns_for_children_ops
					: &droid_lkm_pidns_ops);
		else
			res = droid_lkm_ns_instantiate(
				dentry, task,
				(kind == DROID_LKM_NS_PID_FOR_CHILDREN)
					? droid_lkm_host_pidfc_ops
					: droid_lkm_host_pid_ops);
	}

	put_task_struct(task);
	return res;
}

static int droid_lkm_proc_steal_ns_link_iops(void)
{
	struct path path;
	int ret;


	ret = kern_path("/proc/self/ns/mnt", 0, &path);
	if (ret) {
		droid_lkm_err("cannot open /proc/self/ns/mnt: %d\n", ret);
		return ret;
	}

	droid_lkm_ns_link_iops = d_inode(path.dentry)->i_op;
	path_put(&path);

	if (!droid_lkm_ns_link_iops || !droid_lkm_ns_link_iops->get_link) {
		droid_lkm_err("unexpected ns link inode ops\n");
		return -ENODATA;
	}

	return 0;
}

// pid/ipc entries use the fake ns, host entries share init identity
int droid_lkm_proc_init(void)
{
	int ret;

	droid_lkm_ns_dir_iops =
		(struct inode_operations *)droid_lkm_sym("proc_ns_dir_inode_operations");
	droid_lkm_pid_dop =
		(const struct dentry_operations *)droid_lkm_sym("pid_dentry_operations");
	droid_lkm_proc_pid_make_inode =
		(typeof(droid_lkm_proc_pid_make_inode))droid_lkm_sym("proc_pid_make_inode");

	if (!droid_lkm_ns_dir_iops || !droid_lkm_proc_pid_make_inode) {
		droid_lkm_err("proc symbols missing\n");
		return -ENODATA;
	}



	ret = droid_lkm_ns_ops_host_init();
	if (ret) {
		droid_lkm_err("cannot build host pid ns placeholder: %d\n", ret);
		return ret;
	}
	droid_lkm_host_pid_ops = droid_lkm_ns_ops_host_pid();
	droid_lkm_host_pidfc_ops = droid_lkm_ns_ops_host_pid_for_children();
	droid_lkm_host_ipc_ops = &droid_lkm_ipcns_ops;
	if (!droid_lkm_host_pid_ops || !droid_lkm_host_pidfc_ops || !droid_lkm_host_ipc_ops) {
		droid_lkm_err("host ns ops missing\n");
		return -ENODATA;
	}

	ret = droid_lkm_proc_steal_ns_link_iops();
	if (ret)
		return ret;

	droid_lkm_orig_ns_lookup = droid_lkm_ns_dir_iops->lookup;
	if (!droid_lkm_orig_ns_lookup) {
		droid_lkm_err("proc_ns_dir_inode_operations.lookup is NULL\n");
		return -ENODATA;
	}

	ret = hk_patch_write(&droid_lkm_ns_dir_iops->lookup, (unsigned long)droid_lkm_ns_lookup);
	if (ret) {
		droid_lkm_err("cannot patch ns dir lookup: %d\n", ret);
		return ret;
	}

	droid_lkm_info("proc ns lookup hooked (orig=0x%lx)\n",
		(unsigned long)droid_lkm_orig_ns_lookup);



	if (!try_module_get(THIS_MODULE)) {
		droid_lkm_err("cannot pin module for the ns hook\n");
		hk_patch_write(&droid_lkm_ns_dir_iops->lookup,
			       (unsigned long)droid_lkm_orig_ns_lookup);
		droid_lkm_orig_ns_lookup = NULL;
		return -EBUSY;
	}
	droid_lkm_dbg("ns hook is permanent: module pinned (rmmod -> -EBUSY)\n");
	return 0;
}

void droid_lkm_proc_exit(void)
{
	if (droid_lkm_ns_dir_iops && droid_lkm_orig_ns_lookup) {
		hk_patch_write(&droid_lkm_ns_dir_iops->lookup,
			       (unsigned long)droid_lkm_orig_ns_lookup);
		droid_lkm_orig_ns_lookup = NULL;
	}
}

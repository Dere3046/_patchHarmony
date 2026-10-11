// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
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
#include <asm/memory.h>

#include "hk_patch.h"
#include "ds.h"

DROID_LKM_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#include "ds_ipcns.h"
#include "ds_pidns.h"
#include "ds_proc.h"
#include "ds_nsops.h"
#include "ds_slot.h"
#include "proc_inode.h"

static struct inode_operations *droid_lkm_ns_dir_iops;
/* one shot per ns kind, see droid_lkm_ns_lookup */
static bool droid_lkm_ns_seen[4];
/*
 * true only once the lookup field has been read back and really holds ours. the
 * module pins itself for this hook and the exit path restores it, so both must
 * follow the field and not the intent
 */
static bool droid_lkm_ns_hook_active;
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

static __nocfi noinline struct dentry *droid_lkm_ns_instantiate(struct dentry *dentry,
					struct task_struct *task,
					const struct proc_ns_operations *ops)
{
	struct inode *inode;
	struct proc_inode *ei;

	/*
	 * both of these answer ENOENT, which is the same thing the caller sees when
	 * the hook was never reached at all, so each one says which it was
	 */
	if (!ops) {
		droid_lkm_warn("proc ns instantiate: no ns operations for this name\n");
		return ERR_PTR(-ENOENT);
	}

	inode = droid_lkm_proc_pid_make_inode(dentry->d_sb, task, S_IFLNK | S_IRWXUGO);
	if (!inode) {
		droid_lkm_warn("proc ns instantiate: proc_pid_make_inode returned NULL\n");
		return ERR_PTR(-ENOENT);
	}

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

/*
 * Extension point for namespace kinds owned by an optional module. The list is
 * short and is written only at load and unload time.
 */
#define DROID_LKM_NS_KIND_MAX 4

static const struct droid_lkm_ns_kind_reg *droid_lkm_ns_kinds[DROID_LKM_NS_KIND_MAX];
static DEFINE_MUTEX(droid_lkm_ns_kinds_lock);

int droid_lkm_ns_kind_register(const struct droid_lkm_ns_kind_reg *kind)
{
	int i;

	if (!kind || !kind->name || !kind->ops_for)
		return -EINVAL;

	mutex_lock(&droid_lkm_ns_kinds_lock);
	for (i = 0; i < DROID_LKM_NS_KIND_MAX; i++) {
		if (droid_lkm_ns_kinds[i] == kind) {
			mutex_unlock(&droid_lkm_ns_kinds_lock);
			return 0;
		}
	}
	for (i = 0; i < DROID_LKM_NS_KIND_MAX; i++) {
		if (!droid_lkm_ns_kinds[i]) {
			droid_lkm_ns_kinds[i] = kind;
			mutex_unlock(&droid_lkm_ns_kinds_lock);
			return 0;
		}
	}
	mutex_unlock(&droid_lkm_ns_kinds_lock);
	return -ENOSPC;
}
EXPORT_SYMBOL_GPL(droid_lkm_ns_kind_register);

void droid_lkm_ns_kind_unregister(const struct droid_lkm_ns_kind_reg *kind)
{
	int i;

	mutex_lock(&droid_lkm_ns_kinds_lock);
	for (i = 0; i < DROID_LKM_NS_KIND_MAX; i++) {
		if (droid_lkm_ns_kinds[i] == kind)
			droid_lkm_ns_kinds[i] = NULL;
	}
	mutex_unlock(&droid_lkm_ns_kinds_lock);
}
EXPORT_SYMBOL_GPL(droid_lkm_ns_kind_unregister);

static struct dentry *droid_lkm_ns_kind_lookup(struct dentry *dentry,
					  struct task_struct *task,
					  struct dentry *fallback)
{
	const struct proc_ns_operations *ops = NULL;
	int i;

	mutex_lock(&droid_lkm_ns_kinds_lock);
	for (i = 0; i < DROID_LKM_NS_KIND_MAX; i++) {
		const struct droid_lkm_ns_kind_reg *kind = droid_lkm_ns_kinds[i];

		if (!kind)
			continue;
		if (strlen(kind->name) != dentry->d_name.len ||
		    memcmp(kind->name, dentry->d_name.name, dentry->d_name.len))
			continue;
		ops = kind->ops_for(task);
		break;
	}
	mutex_unlock(&droid_lkm_ns_kinds_lock);

	if (!ops)
		return fallback;

	return droid_lkm_ns_instantiate(dentry, task, ops);
}

static __nocfi noinline struct dentry *droid_lkm_ns_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	enum droid_lkm_ns_kind kind;
	struct task_struct *task;
	struct dentry *res;

	res = droid_lkm_orig_ns_lookup(dir, dentry, flags);
	if (!IS_ERR(res) || PTR_ERR(res) != -ENOENT)
		return res;

	kind = droid_lkm_ns_name_kind(dentry->d_name.name, dentry->d_name.len);
	if (!kind) {
		struct dentry *reg;

		task = get_proc_task(dir);
		if (!task)
			return res;
		reg = droid_lkm_ns_kind_lookup(dentry, task, res);
		put_task_struct(task);
		return reg;
	}

	task = get_proc_task(dir);
	if (!task)
		return ERR_PTR(-ENOENT);

	/*
	 * one line per kind, the first time the kernel asks for it. without it a
	 * missing entry cannot be told apart from a hook that was never called, and
	 * that is the whole question when the entry does not show up
	 */
	if (kind < ARRAY_SIZE(droid_lkm_ns_seen) && !droid_lkm_ns_seen[kind]) {
		droid_lkm_ns_seen[kind] = true;
		droid_lkm_info("proc ns lookup entered for %.*s (kind=%d, task=%s[%d])\n",
			       (int)dentry->d_name.len, dentry->d_name.name, (int)kind,
			       task->comm, task_pid_nr(task));
	}

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

/*
 * three ways to put one word into read only kernel data, tried in order until the
 * field really holds it: the writer the engine is configured with, the library's
 * own fixmap slot, and the linear alias of the same physical page. the first two
 * both go through a fixmap slot, the third does not, so a kernel that swallows
 * one kind of store gets the other before the hook is called impossible.
 *
 * the alias is only writable when the kernel left the linear mapping of rodata
 * writable, which is why the store is a nofault one: a read only alias answers
 * -EFAULT instead of taking the machine down
 */
static bool droid_lkm_ns_lookup_store(unsigned long *field, unsigned long want)
{
	unsigned long *pms;
	unsigned long pa;
	unsigned long lm;
	int fret;

	if (READ_ONCE(*field) == want)
		return true;

	if (droid_lkm_patch_write(field, want))
		droid_lkm_warn("proc ns hook: the engine writer refused\n");
	if (READ_ONCE(*field) == want) {
		droid_lkm_info("proc ns hook: the engine writer landed\n");
		return true;
	}

	fret = hk_write_fixmap(field, &want, sizeof(want));
	droid_lkm_info("proc ns hook: library writer rc=%d, field now %px\n",
		       fret, (void *)READ_ONCE(*field));
	if (READ_ONCE(*field) == want)
		return true;

	pa = hk_va_to_pa((unsigned long)field);
	pms = (unsigned long *)droid_lkm_sym("memstart_addr");
	if (!pa || !pms) {
		droid_lkm_warn("proc ns hook: no linear alias to try (pa=%lx memstart_addr=%px)\n",
			       pa, pms);
		return false;
	}
	lm = ((pa - READ_ONCE(*pms)) | PAGE_OFFSET) & PAGE_MASK;
	lm += (unsigned long)field & ~PAGE_MASK;

	/*
	 * the alias is only asked to take the store when its own pte says it will. a
	 * plain store to a read only page is a fault with no way back, and the kernel
	 * keeps the linear mapping of its rodata read only unless rodata_full is off
	 */
	if (!hk_va_writable(lm)) {
		droid_lkm_info("proc ns hook: the linear alias %px is read only too\n",
			       (void *)lm);
		return false;
	} else {
		memcpy((void *)lm, &want, sizeof(want));
		dsb(ish);
	}
	droid_lkm_info("proc ns hook: linear alias %px stored, field now %px\n",
		       (void *)lm, (void *)READ_ONCE(*field));
	return READ_ONCE(*field) == want;
}

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

	/*
	 * the store is one word into read only data and a writer that reports success
	 * is not the same thing as a mapping that took the store, so the field is
	 * read back and the hook is only claimed once it really holds ours. the pages
	 * a kernel refuses to give up are not fatal: pid and ipc entries stay absent
	 * and everything else here still works, which beats a module that pins itself
	 * for a hook it never installed
	 */
	{
		struct path live;
		unsigned long want = (unsigned long)droid_lkm_ns_lookup;

		if (!droid_lkm_ns_lookup_store((unsigned long *)&droid_lkm_ns_dir_iops->lookup,
					      want)) {
			droid_lkm_warn("proc ns hook: this kernel will not take the ns dir hook (iops=%px level=%d writable=%d), /proc/<pid>/ns/{pid,ipc} stay absent\n",
				       droid_lkm_ns_dir_iops,
				       hk_va_level((unsigned long)&droid_lkm_ns_dir_iops->lookup),
				       hk_va_writable((unsigned long)&droid_lkm_ns_dir_iops->lookup));
			droid_lkm_orig_ns_lookup = NULL;
			droid_lkm_pidns_sysctl_init();
			return 0;
		}

		droid_lkm_ns_hook_active = true;
		droid_lkm_info("proc ns lookup hooked (orig=0x%lx)\n",
			(unsigned long)droid_lkm_orig_ns_lookup);

		if (kern_path("/proc/self/ns", 0, &live)) {
			droid_lkm_warn("proc ns hook: /proc/self/ns could not be resolved\n");
		} else {
			const struct inode_operations *liops = d_inode(live.dentry)->i_op;

			if (liops && liops != droid_lkm_ns_dir_iops) {
				unsigned long lgot;

				droid_lkm_warn("proc ns hook: the live ns directory carries a different iops (%px vs %px), patching that one too\n",
					       liops, droid_lkm_ns_dir_iops);
				if (liops->lookup)
					droid_lkm_orig_ns_lookup = liops->lookup;
				if (droid_lkm_patch_write((void *)&liops->lookup, want))
					droid_lkm_err("proc ns hook: the live iops could not be patched\n");
				lgot = (unsigned long)READ_ONCE(liops->lookup);
				droid_lkm_info("proc ns hook: live iops now %px (%s)\n",
					       (void *)lgot, lgot == want ? "landed" : "did not land");
			} else {
				droid_lkm_info("proc ns hook: the live iops is the same struct\n");
			}
			path_put(&live);
		}

		/*
		 * and then ask for the entry the hook is supposed to serve, from here.
		 * the answer says whether the patched lookup is the one the vfs reaches,
		 * which is the question the live iops above can only hint at
		 */
		{
			struct path probe;
			int pret = kern_path("/proc/self/ns/pid", 0, &probe);

			if (!pret)
				path_put(&probe);
			droid_lkm_info("proc ns hook self test: /proc/self/ns/pid -> %d (%s)\n",
					pret, pret ? "the hook did not serve it" : "served");
		}
	}

	if (!try_module_get(THIS_MODULE)) {
		droid_lkm_err("cannot pin module for the ns hook\n");
		droid_lkm_patch_write(&droid_lkm_ns_dir_iops->lookup,
			       (unsigned long)droid_lkm_orig_ns_lookup);
		droid_lkm_orig_ns_lookup = NULL;
		droid_lkm_ns_hook_active = false;
		return -EBUSY;
	}
	droid_lkm_dbg("ns hook is permanent: module pinned (rmmod -> -EBUSY)\n");

	/*
	 * ns_last_pid needs the sysctl helpers the ipc side resolves, and this
	 * runs after droid_lkm_ipcns_init()
	 */
	droid_lkm_pidns_sysctl_init();
	return 0;
}

void droid_lkm_proc_exit(void)
{
	droid_lkm_pidns_sysctl_exit();

	if (droid_lkm_ns_hook_active && droid_lkm_ns_dir_iops &&
	    droid_lkm_orig_ns_lookup) {
		droid_lkm_patch_write(&droid_lkm_ns_dir_iops->lookup,
			       (unsigned long)droid_lkm_orig_ns_lookup);
		droid_lkm_orig_ns_lookup = NULL;
		droid_lkm_ns_hook_active = false;
	}
}

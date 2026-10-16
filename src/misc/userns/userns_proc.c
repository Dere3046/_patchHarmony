// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * The per task id map files.
 *
 * CONFIG_USER_NS gates the pid entries that expose a task's maps
 * (fs/proc/base.c, tgid_base_stuff and tid_base_stuff), so with it off the files
 * do not exist even though the namespace they describe does, because this module
 * is what created it. They are added back here.
 *
 * The two lookup functions the pid directory inode operations point at are
 * static, but their addresses sit in those const tables, so they cannot have
 * been inlined and the hooks are guaranteed to run. Everything else is called
 * rather than hooked and is therefore immune to inlining: name_to_int() and
 * find_task_by_pid_ns() exactly as proc_pid_lookup() itself uses them, then
 * proc_pid_make_inode() to build the inode, the same primitive the kernel's own
 * entries under /proc/<pid> are built from.
 *
 * proc_pident_instantiate() would do the same in one call, but it needs a
 * struct pid_entry, private to fs/proc/base.c. Reusing the primitives it is made
 * of keeps this file free of any private layout, and the inode comes out
 * identical: like the kernel's own entries under /proc/<pid>, it is not
 * registered with the pid, and it is the pid directory being invalidated that
 * releases it.
 *
 * uid_map, gid_map and setgroups are offered. projid_map needs project ids,
 * which nothing in a Droidspaces container uses.
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/user_namespace.h>

#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "ds.h"
#include "userns.h"
#include "hk_inline.h"

/* "         0          0 4294967295\n" and room for the setgroups answer */
#define DROID_LKM_USERNS_MAP_TEXT 64

struct droid_lkm_userns_proc_file {
	const char *name;
	unsigned int len;
	const struct file_operations *fops;
};

/* not in a header a module can reach, so the prototypes below are the kernel's own */
static struct inode *(*droid_lkm_userns_proc_make_inode)(struct super_block *sb,
		struct task_struct *task, umode_t mode);
static void (*droid_lkm_userns_proc_update_inode)(struct task_struct *task,
		struct inode *inode);
static unsigned int (*droid_lkm_userns_proc_name_to_int)(const struct qstr *name);
static struct task_struct *(*droid_lkm_userns_proc_find_task)(pid_t nr,
		struct pid_namespace *ns);
static struct dentry *(*droid_lkm_userns_proc_splice_alias)(struct inode *inode,
		struct dentry *dentry);
static void (*droid_lkm_userns_proc_set_d_op)(struct dentry *dentry,
		const struct dentry_operations *op);
static const struct dentry_operations *droid_lkm_userns_proc_dentry_ops;

static struct hk_inline droid_lkm_userns_proc_tgid_hook;
static struct hk_inline droid_lkm_userns_proc_tid_hook;
static struct dentry *(*droid_lkm_userns_proc_tgid_orig)(struct inode *dir,
		struct dentry *dentry, unsigned int flags);
static struct dentry *(*droid_lkm_userns_proc_tid_orig)(struct inode *dir,
		struct dentry *dentry, unsigned int flags);

/*
 * upstream renders through seq_file and converts with from_kuid(), which is the identity stub
 * here, and every namespace reachable carries the identity map, so the extent is printed as
 * stored. stage 2b has to make this a real conversion
 */
static int droid_lkm_userns_map_text(const struct uid_gid_map *map, char *buf,
				     int size)
{
	const struct uid_gid_extent *ext = map->extent;
	unsigned int extents = map->nr_extents;
	unsigned int i;
	int len = 0;

	smp_rmb();
	if (extents > UID_GID_MAP_MAX_BASE_EXTENTS)
		ext = map->forward;

	for (i = 0; i < extents; i++) {
		if (!ext[i].count)
			continue;

		len += scnprintf(buf + len, size - len, "%10u %10u %10u\n",
			ext[i].first, ext[i].lower_first, ext[i].count);
	}

	return len;
}

/*
 * the only map this kernel can hold is the identity one, so a write asking for another is
 * refused, not ignored
 */
static bool droid_lkm_userns_map_is_identity(const char *text)
{
	u64 v[3];
	const char *s = text;
	int i;

	for (i = 0; i < 3; i++) {
		int digits = 0;

		while (*s == ' ' || *s == '\t')
			s++;

		v[i] = 0;
		while (*s >= '0' && *s <= '9') {
			v[i] = v[i] * 10 + (*s - '0');
			if (v[i] > 4294967295ULL)
				return false;
			s++;
			digits++;
		}

		if (!digits)
			return false;
	}

	while (*s == ' ' || *s == '\t' || *s == '\n')
		s++;

	return !*s && v[0] == 0 && v[1] == 0 && v[2] == 4294967295ULL;
}

static ssize_t droid_lkm_userns_map_read(char __user *ubuf, size_t count,
					 loff_t *ppos,
					 const struct uid_gid_map *map)
{
	char text[DROID_LKM_USERNS_MAP_TEXT];

	if (!map)
		return -EIO;

	return droid_lkm_misc_proc_read_text(ubuf, count, ppos, text,
		droid_lkm_userns_map_text(map, text, sizeof(text)));
}

static ssize_t droid_lkm_userns_map_write(struct file *file, const char __user *ubuf,
					  size_t count, loff_t *ppos)
{
	struct user_namespace *ns = file_inode(file)->i_private;
	char text[DROID_LKM_USERNS_MAP_TEXT];

	/* upstream: one write, at the beginning, of less than a page */
	if (*ppos != 0 || count >= sizeof(text))
		return -EINVAL;

	/*
	 * the initial namespace's map is already identity and upstream refuses a second write, so
	 * this is refused too
	 */
	if (!ns || ns == droid_lkm_userns_init_uns)
		return -EPERM;

	if (!count)
		return -EINVAL;

	if (copy_from_user(text, ubuf, count))
		return -EFAULT;
	text[count] = 0;

	if (!droid_lkm_userns_map_is_identity(text))
		return -EPERM;

	*ppos += count;

	return count;
}

static ssize_t droid_lkm_userns_uid_map_read(struct file *file, char __user *ubuf,
					     size_t count, loff_t *ppos)
{
	struct user_namespace *ns = file_inode(file)->i_private;

	return droid_lkm_userns_map_read(ubuf, count, ppos,
		ns ? &ns->uid_map : NULL);
}

static ssize_t droid_lkm_userns_gid_map_read(struct file *file, char __user *ubuf,
					     size_t count, loff_t *ppos)
{
	struct user_namespace *ns = file_inode(file)->i_private;

	return droid_lkm_userns_map_read(ubuf, count, ppos,
		ns ? &ns->gid_map : NULL);
}

static ssize_t droid_lkm_userns_setgroups_read(struct file *file, char __user *ubuf,
					       size_t count, loff_t *ppos)
{
	struct user_namespace *ns = file_inode(file)->i_private;
	const char *text = "allow\n";

	if (ns && !(READ_ONCE(ns->flags) & USERNS_SETGROUPS_ALLOWED))
		text = "deny\n";

	return droid_lkm_misc_proc_read_text(ubuf, count, ppos, text, strlen(text));
}

/*
 * upstream stores the answer in user_namespace.flags and may_setgroups() reads it back
 * through a stub that answers true, so accepting the write would set a flag nothing reads
 */
static ssize_t droid_lkm_userns_setgroups_write(struct file *file,
						const char __user *ubuf,
						size_t count, loff_t *ppos)
{
	return -EPERM;
}

static const struct file_operations droid_lkm_userns_uid_map_fops = {
	.read		= droid_lkm_userns_uid_map_read,
	.write		= droid_lkm_userns_map_write,
	.llseek		= droid_lkm_misc_proc_llseek,
	.owner		= THIS_MODULE,
};

static const struct file_operations droid_lkm_userns_gid_map_fops = {
	.read		= droid_lkm_userns_gid_map_read,
	.write		= droid_lkm_userns_map_write,
	.llseek		= droid_lkm_misc_proc_llseek,
	.owner		= THIS_MODULE,
};

static const struct file_operations droid_lkm_userns_setgroups_fops = {
	.read		= droid_lkm_userns_setgroups_read,
	.write		= droid_lkm_userns_setgroups_write,
	.llseek		= droid_lkm_misc_proc_llseek,
	.owner		= THIS_MODULE,
};

static const struct droid_lkm_userns_proc_file droid_lkm_userns_proc_files[] = {
	{ "uid_map",	7,	&droid_lkm_userns_uid_map_fops },
	{ "gid_map",	7,	&droid_lkm_userns_gid_map_fops },
	{ "setgroups",	9,	&droid_lkm_userns_setgroups_fops },
};

static struct dentry *droid_lkm_userns_proc_instantiate(struct dentry *dentry,
							struct task_struct *task,
							const struct file_operations *fops)
{
	struct user_namespace *ns;
	const struct cred *cred;
	struct inode *inode;

	cred = get_task_cred(task);
	/*
	 * stable without a reference, the field is either the static initial namespace or one this
	 * module owns
	 */
	ns = cred->user_ns;
	put_cred(cred);

	inode = droid_lkm_userns_proc_make_inode(dentry->d_sb, task,
						 S_IFREG | 0644);
	if (!inode)
		return ERR_PTR(-ENOENT);

	/* i_private is free on a pid entry inode, so the read and the write need no task lookup */
	inode->i_fop = fops;
	inode->i_private = ns;
	droid_lkm_userns_proc_update_inode(task, inode);

	droid_lkm_userns_proc_set_d_op(dentry, droid_lkm_userns_proc_dentry_ops);

	return droid_lkm_userns_proc_splice_alias(inode, dentry);
}

/*
 * __nocfi because orig is the trampoline and carries no type hash, the calls to real kernel
 * functions stay checked
 */
__nocfi noinline static struct dentry *
droid_lkm_userns_proc_lookup(struct inode *dir, struct dentry *dentry,
			     unsigned int flags,
			     struct dentry *(*orig)(struct inode *,
						    struct dentry *,
						    unsigned int))
{
	const struct file_operations *fops = NULL;
	struct pid_namespace *pid_ns;
	struct task_struct *task;
	struct dentry *res;
	unsigned int nr;
	unsigned int i;

	res = orig(dir, dentry, flags);
	/* a NULL return is a successful alias, only ENOENT means "no entry" */
	if (!IS_ERR(res) || PTR_ERR(res) != -ENOENT)
		return res;

	for (i = 0; i < ARRAY_SIZE(droid_lkm_userns_proc_files); i++) {
		if (droid_lkm_userns_proc_files[i].len != dentry->d_name.len)
			continue;
		if (memcmp(droid_lkm_userns_proc_files[i].name, dentry->d_name.name,
			   dentry->d_name.len))
			continue;

		fops = droid_lkm_userns_proc_files[i].fops;
		break;
	}
	if (!fops)
		return res;

	/* the lookup directory is the numeric one, read the way proc_pid_lookup() reads it */
	nr = droid_lkm_userns_proc_name_to_int(&dentry->d_parent->d_name);
	if (nr == ~0U)
		return res;

	pid_ns = proc_sb_info(dentry->d_sb)->pid_ns;
	if (!pid_ns)
		return res;

	rcu_read_lock();
	task = droid_lkm_userns_proc_find_task((pid_t)nr, pid_ns);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();
	if (!task)
		return res;

	res = droid_lkm_userns_proc_instantiate(dentry, task, fops);
	put_task_struct(task);

	return res;
}

__nocfi noinline struct dentry *
droid_lkm_userns_proc_tgid_lookup(struct inode *dir, struct dentry *dentry,
				  unsigned int flags)
{
	if (!droid_lkm_userns_proc_tgid_orig)
		return ERR_PTR(-ENOENT);

	return droid_lkm_userns_proc_lookup(dir, dentry, flags,
					    droid_lkm_userns_proc_tgid_orig);
}

__nocfi noinline struct dentry *
droid_lkm_userns_proc_tid_lookup(struct inode *dir, struct dentry *dentry,
				 unsigned int flags)
{
	if (!droid_lkm_userns_proc_tid_orig)
		return ERR_PTR(-ENOENT);

	return droid_lkm_userns_proc_lookup(dir, dentry, flags,
					    droid_lkm_userns_proc_tid_orig);
}

static int droid_lkm_userns_proc_resolve(void)
{
	struct {
		const char *name;
		void **slot;
	} syms[] = {
		{ "proc_pid_make_inode", (void **)&droid_lkm_userns_proc_make_inode },
		{ "pid_update_inode",	(void **)&droid_lkm_userns_proc_update_inode },
		{ "name_to_int",	(void **)&droid_lkm_userns_proc_name_to_int },
		{ "find_task_by_pid_ns", (void **)&droid_lkm_userns_proc_find_task },
		{ "d_splice_alias",	(void **)&droid_lkm_userns_proc_splice_alias },
		/*
		 * exported into ANDROID_GKI_VFS_EXPORT_ONLY, so importing it needs MODULE_IMPORT_NS() and the
		 * device refuses the module without it. resolved by name like the rest
		 */
		{ "d_set_d_op",		(void **)&droid_lkm_userns_proc_set_d_op },
		{ "pid_dentry_operations",
		 	(void **)&droid_lkm_userns_proc_dentry_ops },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(syms); i++) {
		*syms[i].slot = (void *)droid_lkm_misc_sym(syms[i].name);
		if (!*syms[i].slot) {
			droid_lkm_misc_warn("id map files: %s not found\n",
					    syms[i].name);
			droid_lkm_misc_report("userns id map files", "unsupported",
					      syms[i].name);
			return -ENOENT;
		}
	}

	return 0;
}

int droid_lkm_userns_proc_init(void)
{
	int ret;

	ret = droid_lkm_userns_proc_resolve();
	if (ret)
		return ret;

	ret = droid_lkm_inline_hook_checked(&droid_lkm_userns_proc_tgid_hook, "proc_tgid_base_lookup",
				    "droid_lkm_userns_proc_tgid_lookup");
	if (ret) {
		droid_lkm_misc_warn("hook proc_tgid_base_lookup failed: %d\n", ret);
		return ret;
	}
	droid_lkm_userns_proc_tgid_orig = (void *)droid_lkm_userns_proc_tgid_hook.orig;

	ret = droid_lkm_inline_hook_checked(&droid_lkm_userns_proc_tid_hook, "proc_tid_base_lookup",
				    "droid_lkm_userns_proc_tid_lookup");
	if (ret) {
		droid_lkm_misc_warn("hook proc_tid_base_lookup failed: %d\n", ret);
		hk_inline_unhook(&droid_lkm_userns_proc_tgid_hook);
		droid_lkm_userns_proc_tgid_orig = NULL;
		return ret;
	}
	droid_lkm_userns_proc_tid_orig = (void *)droid_lkm_userns_proc_tid_hook.orig;

	droid_lkm_misc_info("id map files ready\n");
	droid_lkm_misc_report("userns id map files", "ready",
			      "uid_map gid_map setgroups under /proc/<pid>");

	return 0;
}

/*
 * the inodes hold this module's f_op and the dentry cache keeps them. an open file pins the
 * module through f_op->owner and the kernel drops the dentry when the task exits, so once
 * those tasks are gone the module can be unloaded
 */
void droid_lkm_userns_proc_exit(void)
{
	if (droid_lkm_userns_proc_tid_orig) {
		hk_inline_unhook(&droid_lkm_userns_proc_tid_hook);
		droid_lkm_userns_proc_tid_orig = NULL;
	}
	if (droid_lkm_userns_proc_tgid_orig) {
		hk_inline_unhook(&droid_lkm_userns_proc_tgid_hook);
		droid_lkm_userns_proc_tgid_orig = NULL;
	}
}

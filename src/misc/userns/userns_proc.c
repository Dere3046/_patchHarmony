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
 * The namespace is resolved when the file is opened, not when the dentry is
 * looked up, because a task can unshare in between. The inode carries the task's
 * struct pid, borrowed from the inode's own reference, and the credential is
 * read again at open.
 *
 * uid_map, gid_map and setgroups are offered. projid_map needs project ids,
 * which nothing in a Droidspaces container uses.
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/user_namespace.h>
#include <linux/mutex.h>

#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "userns.h"
#include "hk_inline.h"

static DEFINE_MUTEX(droid_lkm_userns_proc_mutex);

/* room for up to UID_GID_MAP_MAX_BASE_EXTENTS lines and the setgroups answer */
#define DROID_LKM_USERNS_MAP_TEXT 512

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
 * upstream renders through seq_file and converts the lower id with from_kuid() as
 * seen from the opener's namespace; with CONFIG_USER_NS off from_kuid() is the
 * identity stub. the map stores global ids, so for a level-1 namespace (the only
 * shape a container gets) printing the extent as stored gives the answer both a
 * host reader and the container's own root expect. a nested namespace would need
 * the parent conversion, which stage 2b owns
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

static int droid_lkm_userns_map_parse(const char *buf, size_t count,
				      struct uid_gid_map *new_map)
{
	const char *p = buf;
	const char *end = buf + count;
	unsigned int extents = 0;
	u64 v[3];
	u32 first_i, last_i, lower_i, lower_last_i;
	u32 first_j, last_j, lower_j, lower_last_j;
	int i, j, digits;

	memset(new_map, 0, sizeof(*new_map));

	while (p < end) {
		/* Skip leading blank lines / spaces */
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;

		if (p >= end)
			break;

		if (extents >= UID_GID_MAP_MAX_BASE_EXTENTS)
			return -EINVAL;

		for (i = 0; i < 3; i++) {
			v[i] = 0;
			digits = 0;

			while (p < end && (*p == ' ' || *p == '\t'))
				p++;

			while (p < end && *p >= '0' && *p <= '9') {
				v[i] = v[i] * 10 + (*p - '0');
				if (v[i] > 4294967295ULL)
					return -EINVAL;
				p++;
				digits++;
			}

			if (!digits)
				return -EINVAL;
		}

		/* Skip trailing spaces in line */
		while (p < end && (*p == ' ' || *p == '\t'))
			p++;

		/* Expect newline or end of buffer */
		if (p < end && *p != '\n' && *p != '\r')
			return -EINVAL;

		if (p < end)
			p++;

		/* upstream's range checks: no INVALID_UID endpoint, no zero count, no wrap */
		if (v[2] == 0 || v[0] == 4294967295ULL || v[1] == 4294967295ULL ||
		    v[0] + v[2] > 4294967295ULL || v[1] + v[2] > 4294967295ULL)
			return -EINVAL;

		new_map->extent[extents].first = (u32)v[0];
		new_map->extent[extents].lower_first = (u32)v[1];
		new_map->extent[extents].count = (u32)v[2];
		extents++;
	}

	if (extents == 0)
		return -EINVAL;

	/* mappings must not overlap in either namespace */
	for (i = 0; i < extents; i++) {
		first_i = new_map->extent[i].first;
		last_i = first_i + new_map->extent[i].count - 1;
		lower_i = new_map->extent[i].lower_first;
		lower_last_i = lower_i + new_map->extent[i].count - 1;

		for (j = i + 1; j < extents; j++) {
			first_j = new_map->extent[j].first;
			last_j = first_j + new_map->extent[j].count - 1;
			lower_j = new_map->extent[j].lower_first;
			lower_last_j = lower_j + new_map->extent[j].count - 1;

			if (first_i <= last_j && first_j <= last_i)
				return -EINVAL;
			if (lower_i <= lower_last_j && lower_j <= lower_last_i)
				return -EINVAL;
		}
	}

	new_map->nr_extents = extents;
	return 0;
}

/*
 * The namespace is resolved at open time, not at lookup time. The inode's
 * i_private is the task's struct pid, borrowed: proc_pid_make_inode() holds its
 * own reference for the inode's lifetime, and a recycled pid number gets a new
 * struct pid, so get_pid_task() returns NULL after the task exits instead of a
 * stranger. The credential is read now, which is what the kernel's own id map
 * files do, so a task that unshares between lookup and open still reads the
 * namespace it is in.
 */
static struct user_namespace *droid_lkm_userns_proc_ns(struct inode *inode)
{
	struct pid *pid = inode->i_private;
	struct user_namespace *ns = NULL;
	struct droid_lkm_userns *u;
	struct task_struct *task;
	const struct cred *cred;

	if (!pid)
		return NULL;

	task = get_pid_task(pid, PIDTYPE_PID);
	if (!task)
		return NULL;

	cred = get_task_cred(task);
	if (cred) {
		ns = cred->user_ns;

		if (ns != droid_lkm_userns_init_uns) {
			u = droid_lkm_userns_from_ns(ns);
			if (!u) {
				ns = NULL;
			} else {
				/*
				 * take the reference the open will hold here, while the
				 * credential still pins the namespace, so a task exiting
				 * between this lookup and the open cannot free it
				 */
				droid_lkm_userns_get(u);
			}
		}

		put_cred(cred);
	}
	put_task_struct(task);

	return ns;
}

static int droid_lkm_userns_proc_open(struct inode *inode, struct file *file)
{
	struct user_namespace *ns = droid_lkm_userns_proc_ns(inode);

	if (!ns)
		return -ENOENT;

	/* the reference this file holds was taken by the lookup, which is where it was safe */
	file->private_data = ns;

	return 0;
}

static int droid_lkm_userns_proc_release(struct inode *inode, struct file *file)
{
	struct user_namespace *ns = file->private_data;
	struct droid_lkm_userns *u;

	if (ns) {
		u = droid_lkm_userns_from_ns(ns);
		if (u)
			droid_lkm_userns_put(u);
	}

	return 0;
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

/*
 * In kernels with CONFIG_USER_NS disabled, file_ns_capable() is inlined to constant false.
 * Route capability checks on file->f_cred through security_capable or ns_capable.
 */
static bool droid_lkm_file_ns_capable(const struct file *file,
				      struct user_namespace *ns, int cap)
{
	if (unlikely(!file || !file->f_cred || !cap_valid(cap)))
		return false;

	if (droid_lkm_misc_ks.security_capable)
		return droid_lkm_misc_ks.security_capable(file->f_cred, ns, cap, CAP_OPT_NONE) == 0;

	return ns_capable(ns, cap);
}

/*
 * The creator's privilege at namespace creation is the anchor: a namespace born
 * with CAP_SETUID and CAP_SETGID over the initial namespace may define any map,
 * exactly as the kernel's own map_write() allows a namespace root to. Everyone
 * else may only restate the single extent that maps the creator's own id, which
 * is the map the namespace was born with; a confined container can therefore
 * repeat its mapping but never widen it to the host's root.
 */
static bool droid_lkm_userns_map_permitted(const struct file *file,
					   const struct droid_lkm_userns *u,
					   int cap_setid,
					   const struct uid_gid_map *new_map)
{
	const struct cred *cred = file->f_cred;
	u32 id;

	if (u->privileged)
		return true;

	if (new_map->nr_extents == 1 && new_map->extent[0].count == 1 &&
	    uid_eq(u->uns.owner, cred->euid)) {
		id = new_map->extent[0].lower_first;	/* already global */
		if (cap_setid == CAP_SETUID && uid_eq(KUIDT_INIT(id), cred->euid))
			return true;
		if (cap_setid == CAP_SETGID &&
		    !(READ_ONCE(u->uns.flags) & USERNS_SETGROUPS_ALLOWED) &&
		    gid_eq(KGIDT_INIT(id), cred->egid))
			return true;
	}

	/* the host's root keeps the kernel's own power over every namespace */
	return droid_lkm_file_ns_capable(file, u->uns.parent ? u->uns.parent : droid_lkm_userns_init_uns, cap_setid);
}

static ssize_t droid_lkm_userns_map_write_internal(struct file *file, const char __user *ubuf,
						   size_t count, loff_t *ppos, bool is_uid)
{
	struct user_namespace *ns = file->private_data;
	struct droid_lkm_userns *u;
	struct uid_gid_map *map;
	struct uid_gid_map new_map;
	char text[DROID_LKM_USERNS_MAP_TEXT];
	bool *written;
	int ret, i;

	/* upstream: one write, at the beginning, of less than a page */
	if (*ppos != 0 || count >= sizeof(text))
		return -EINVAL;

	/*
	 * the initial namespace's map is already identity and upstream refuses a second write, so
	 * this is refused too
	 */
	if (!ns || ns == droid_lkm_userns_init_uns)
		return -EPERM;

	u = droid_lkm_userns_from_ns(ns);
	if (!u)
		return -EPERM;

	map = is_uid ? &ns->uid_map : &ns->gid_map;
	written = is_uid ? &u->uid_written : &u->gid_written;

	if (!count)
		return -EINVAL;

	if (copy_from_user(text, ubuf, count))
		return -EFAULT;
	text[count] = 0;

	if (mutex_lock_killable(&droid_lkm_userns_proc_mutex))
		return -EINTR;

	/*
	 * Upstream kernel/user_namespace.c map_write():
	 * The caller must have the capability to setuid or setgid in the
	 * parent user namespace, or the capability to setuid or setgid in
	 * the user namespace itself.
	 */
	ret = -EPERM;
	if (!droid_lkm_file_ns_capable(file, ns->parent, is_uid ? CAP_SETUID : CAP_SETGID) &&
	    !droid_lkm_file_ns_capable(file, ns, CAP_SYS_ADMIN))
		goto out;

	/* one successful write; the default map does not count as one */
	if (*written)
		goto out;

	ret = droid_lkm_userns_map_parse(text, count, &new_map);
	if (ret)
		goto out;

	/*
	 * the file carries ids in the parent's space, the map stores global ids; a
	 * range the parent does not map cannot be written
	 */
	for (i = 0; i < new_map.nr_extents; i++) {
		u32 global = droid_lkm_userns_map_parent_down(u,
				new_map.extent[i].lower_first,
				new_map.extent[i].count, is_uid);

		if (global == DROID_LKM_USERNS_ID_UNMAPPED) {
			ret = -EPERM;
			goto out;
		}
		new_map.extent[i].lower_first = global;
	}

	if (!droid_lkm_userns_map_permitted(file, u,
					    is_uid ? CAP_SETUID : CAP_SETGID,
					    &new_map)) {
		ret = -EPERM;
		goto out;
	}

	for (i = 0; i < new_map.nr_extents; i++)
		map->extent[i] = new_map.extent[i];
	smp_wmb();
	map->nr_extents = new_map.nr_extents;
	*written = true;

	*ppos += count;
	ret = count;
out:
	mutex_unlock(&droid_lkm_userns_proc_mutex);

	return ret;
}

static ssize_t droid_lkm_userns_uid_map_write(struct file *file, const char __user *ubuf,
					      size_t count, loff_t *ppos)
{
	return droid_lkm_userns_map_write_internal(file, ubuf, count, ppos, true);
}

static ssize_t droid_lkm_userns_gid_map_write(struct file *file, const char __user *ubuf,
					      size_t count, loff_t *ppos)
{
	return droid_lkm_userns_map_write_internal(file, ubuf, count, ppos, false);
}

static ssize_t droid_lkm_userns_uid_map_read(struct file *file, char __user *ubuf,
					     size_t count, loff_t *ppos)
{
	struct user_namespace *ns = file->private_data;

	return droid_lkm_userns_map_read(ubuf, count, ppos,
		ns ? &ns->uid_map : NULL);
}

static ssize_t droid_lkm_userns_gid_map_read(struct file *file, char __user *ubuf,
					     size_t count, loff_t *ppos)
{
	struct user_namespace *ns = file->private_data;

	return droid_lkm_userns_map_read(ubuf, count, ppos,
		ns ? &ns->gid_map : NULL);
}

static ssize_t droid_lkm_userns_setgroups_read(struct file *file, char __user *ubuf,
					       size_t count, loff_t *ppos)
{
	struct user_namespace *ns = file->private_data;
	const char *text = "allow\n";

	if (ns && !(READ_ONCE(ns->flags) & USERNS_SETGROUPS_ALLOWED))
		text = "deny\n";

	return droid_lkm_misc_proc_read_text(ubuf, count, ppos, text, strlen(text));
}

static ssize_t droid_lkm_userns_setgroups_write(struct file *file,
						const char __user *ubuf,
						size_t count, loff_t *ppos)
{
	struct user_namespace *ns = file->private_data;
	struct droid_lkm_userns *u;
	char kbuf[16];
	int ret;

	if (*ppos != 0 || count >= sizeof(kbuf))
		return -EINVAL;

	if (!ns || ns == droid_lkm_userns_init_uns)
		return -EPERM;

	u = droid_lkm_userns_from_ns(ns);
	if (!u)
		return -EPERM;

	if (copy_from_user(kbuf, ubuf, count))
		return -EFAULT;
	kbuf[count] = 0;

	if (mutex_lock_killable(&droid_lkm_userns_proc_mutex))
		return -EINTR;

	ret = -EPERM;
	if (!droid_lkm_file_ns_capable(file, ns, CAP_SYS_ADMIN))
		goto out;

	/* upstream: cannot change setgroups after gid_map is written */
	if (u->gid_written)
		goto out;

	if (sysfs_streq(kbuf, "deny")) {
		ns->flags &= ~USERNS_SETGROUPS_ALLOWED;
	} else if (sysfs_streq(kbuf, "allow")) {
		/* A user namespace cannot re-enable setgroups */
		if (!(ns->flags & USERNS_SETGROUPS_ALLOWED))
			goto out;
	} else {
		ret = -EINVAL;
		goto out;
	}

	*ppos += count;
	ret = count;
out:
	mutex_unlock(&droid_lkm_userns_proc_mutex);

	return ret;
}

static const struct file_operations droid_lkm_userns_uid_map_fops = {
	.open		= droid_lkm_userns_proc_open,
	.release	= droid_lkm_userns_proc_release,
	.read		= droid_lkm_userns_uid_map_read,
	.write		= droid_lkm_userns_uid_map_write,
	.llseek		= droid_lkm_misc_proc_llseek,
	.owner		= THIS_MODULE,
};

static const struct file_operations droid_lkm_userns_gid_map_fops = {
	.open		= droid_lkm_userns_proc_open,
	.release	= droid_lkm_userns_proc_release,
	.read		= droid_lkm_userns_gid_map_read,
	.write		= droid_lkm_userns_gid_map_write,
	.llseek		= droid_lkm_misc_proc_llseek,
	.owner		= THIS_MODULE,
};

static const struct file_operations droid_lkm_userns_setgroups_fops = {
	.open		= droid_lkm_userns_proc_open,
	.release	= droid_lkm_userns_proc_release,
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
	struct inode *inode;

	inode = droid_lkm_userns_proc_make_inode(dentry->d_sb, task,
						 S_IFREG | 0644);
	if (!inode)
		return ERR_PTR(-ENOENT);

	/*
	 * i_private is free on a pid entry inode. the struct pid is borrowed from
	 * the reference proc_pid_make_inode() just took, so it stays valid for as
	 * long as the inode does; the namespace itself is resolved at open time
	 */
	inode->i_fop = fops;
	inode->i_private = task_pid(task);
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

	ret = hk_inline_hook(&droid_lkm_userns_proc_tgid_hook, "proc_tgid_base_lookup",
			     "droid_lkm_userns_proc_tgid_lookup");
	if (ret) {
		droid_lkm_misc_warn("hook proc_tgid_base_lookup failed: %d\n", ret);
		return ret;
	}
	droid_lkm_userns_proc_tgid_orig = (void *)droid_lkm_userns_proc_tgid_hook.orig;

	ret = hk_inline_hook(&droid_lkm_userns_proc_tid_hook, "proc_tid_base_lookup",
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

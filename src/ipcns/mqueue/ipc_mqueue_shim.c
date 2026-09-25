// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#include <linux/module.h>
#include <linux/statfs.h>
#include <linux/kernel.h>

#include "ds.h"
#include "ds_ksym.h"
#include "ipc_mqueue_compat.h"

typeof(&getname) droid_lkm_p_getname;
typeof(&putname) droid_lkm_p_putname;
typeof(&get_next_ino) droid_lkm_p_get_next_ino;
typeof(&vfs_mkobj) droid_lkm_p_vfs_mkobj;
typeof(&fs_context_for_mount) droid_lkm_p_fs_context_for_mount;
typeof(&get_tree_keyed) droid_lkm_p_get_tree_keyed;
typeof(&get_ucounts) droid_lkm_p_get_ucounts;
typeof(&put_ucounts) droid_lkm_p_put_ucounts;
typeof(&inc_rlimit_ucounts) droid_lkm_p_inc_rlimit_ucounts;
typeof(&dec_rlimit_ucounts) droid_lkm_p_dec_rlimit_ucounts;
typeof(&netlink_getsockbyfilp) droid_lkm_p_netlink_getsockbyfilp;
typeof(&netlink_attachskb) droid_lkm_p_netlink_attachskb;
typeof(&netlink_sendskb) droid_lkm_p_netlink_sendskb;
typeof(&netlink_detachskb) droid_lkm_p_netlink_detachskb;
typeof(&schedule_hrtimeout_range_clock) droid_lkm_p_schedule_hrtimeout_range_clock;
#ifdef CONFIG_COMPAT
typeof(&get_compat_sigevent) droid_lkm_p_get_compat_sigevent;
#endif

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

int droid_lkm_mqueue_shim_init(void)
{
	int missing = 0;

	if (!droid_lkm_mqueue_enabled) {
		droid_lkm_info("POSIX mqueue disabled by param\n");
		return -ENODATA;
	}

	DROID_LKM_MQ_RESOLVE(droid_lkm_p_getname, "getname");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_putname, "putname");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_get_next_ino, "get_next_ino");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_vfs_mkobj, "vfs_mkobj");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_fs_context_for_mount,
			     "fs_context_for_mount");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_get_tree_keyed, "get_tree_keyed");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_get_ucounts, "get_ucounts");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_put_ucounts, "put_ucounts");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_inc_rlimit_ucounts,
			     "inc_rlimit_ucounts");
	DROID_LKM_MQ_RESOLVE(droid_lkm_p_dec_rlimit_ucounts,
			     "dec_rlimit_ucounts");
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

	droid_lkm_mqueue_shim_ok = true;
	droid_lkm_info("mqueue shim ready (16 unexported VFS/ucount/netlink/audit helpers resolved)\n");
	return 0;
}

bool droid_lkm_mqueue_ready(void)
{
	return droid_lkm_mqueue_shim_ok;
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

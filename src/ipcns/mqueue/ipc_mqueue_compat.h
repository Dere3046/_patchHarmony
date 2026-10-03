// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */
#ifndef DROID_LKM_IPC_MQUEUE_COMPAT_H
#define DROID_LKM_IPC_MQUEUE_COMPAT_H

#include <linux/types.h>
#include <linux/ipc_namespace.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/user_namespace.h>
#include <linux/netlink.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include "ds_caps.h"

/*
 * vfs entry points that gained an argument on the way to 6.6, plus the inode
 * timestamp helpers 6.6 introduced. an argument count is not something a
 * runtime capability can select, so the shape is folded here and every call
 * site keeps one spelling. the mapping argument is a user namespace from 5.12
 * to 6.2 and an id mapping from 6.3 on, and droid_lkm_caps.idmap_none holds the
 * right one for each.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#define DROID_LKM_MQ_IDMAP_ARG		struct mnt_idmap *idmap,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#define DROID_LKM_MQ_IDMAP_ARG		struct user_namespace *mnt_userns,
#else
#define DROID_LKM_MQ_IDMAP_ARG
#endif

static inline void droid_lkm_mq_inode_init_ts(struct inode *inode)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	simple_inode_init_ts(inode);
#else
	inode->i_mtime = inode->i_ctime = inode->i_atime = current_time(inode);
#endif
}

static inline void droid_lkm_mq_inode_touch(struct inode *inode)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	inode_set_atime_to_ts(inode, inode_set_ctime_current(inode));
#else
	inode->i_atime = inode->i_ctime = current_time(inode);
#endif
}

static inline void *droid_lkm_mq_alloc_inode(struct super_block *sb,
					     struct kmem_cache *cache)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	return alloc_inode_sb(sb, cache, GFP_KERNEL);
#else
	return kmem_cache_alloc(cache, GFP_KERNEL);
#endif
}

static inline int droid_lkm_mq_inode_permission(struct dentry *dentry, int acc)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	return inode_permission(droid_lkm_caps.idmap_none, d_inode(dentry), acc);
#else
	return inode_permission(d_inode(dentry), acc);
#endif
}

static inline int droid_lkm_mq_vfs_unlink(struct dentry *dentry)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	return vfs_unlink(droid_lkm_caps.idmap_none,
			  d_inode(dentry->d_parent), dentry, NULL);
#else
	return vfs_unlink(d_inode(dentry->d_parent), dentry, NULL);
#endif
}

extern typeof(&getname) droid_lkm_p_getname;
extern typeof(&putname) droid_lkm_p_putname;
extern typeof(&get_next_ino) droid_lkm_p_get_next_ino;
extern typeof(&vfs_mkobj) droid_lkm_p_vfs_mkobj;
extern typeof(&fs_context_for_mount) droid_lkm_p_fs_context_for_mount;
extern typeof(&get_tree_keyed) droid_lkm_p_get_tree_keyed;
/*
 * rlimit ucounts. 5.10 does not have the machinery at all (no
 * current_ucounts(), no UCOUNT_RLIMIT_MSGQUEUE, get_ucounts and put_ucounts are
 * static with a different shape), so the shim resolves what the running kernel
 * has and reports the rest as "not accounted" by handing back no ucounts. the
 * type argument of the original helpers does not survive into this interface
 * because its enum is named per branch.
 */
struct ucounts *droid_lkm_mq_ucounts_get(void);
long droid_lkm_mq_ucounts_charge(struct ucounts *ucounts, unsigned long bytes);
void droid_lkm_mq_ucounts_uncharge(struct ucounts *ucounts, unsigned long bytes);
void droid_lkm_mq_ucounts_put(struct ucounts *ucounts);
extern typeof(&netlink_getsockbyfilp) droid_lkm_p_netlink_getsockbyfilp;
extern typeof(&netlink_attachskb) droid_lkm_p_netlink_attachskb;
extern typeof(&netlink_sendskb) droid_lkm_p_netlink_sendskb;
extern typeof(&netlink_detachskb) droid_lkm_p_netlink_detachskb;
extern typeof(&schedule_hrtimeout_range_clock) droid_lkm_p_schedule_hrtimeout_range_clock;
#ifdef CONFIG_COMPAT
extern typeof(&get_compat_sigevent) droid_lkm_p_get_compat_sigevent;
#define get_compat_sigevent(e, u) droid_lkm_p_get_compat_sigevent((e), (u))
#endif

#define getname(f)			droid_lkm_p_getname(f)
#define putname(n)			droid_lkm_p_putname(n)
#define get_next_ino()			droid_lkm_p_get_next_ino()
#define vfs_mkobj(d, m, f, a)		droid_lkm_p_vfs_mkobj((d), (m), (f), (a))
#define fs_context_for_mount(t, f)	droid_lkm_p_fs_context_for_mount((t), (f))
#define get_tree_keyed(c, f, k)		droid_lkm_p_get_tree_keyed((c), (f), (k))
#define put_ucounts(u)			droid_lkm_mq_ucounts_put(u)
#define netlink_getsockbyfilp(f)	droid_lkm_p_netlink_getsockbyfilp(f)
#define netlink_attachskb(s, b, t, ss)	droid_lkm_p_netlink_attachskb((s), (b), (t), (ss))
#define netlink_sendskb(s, b)		droid_lkm_p_netlink_sendskb((s), (b))
#define netlink_detachskb(s, b)		droid_lkm_p_netlink_detachskb((s), (b))
#define schedule_hrtimeout_range_clock(e, d, m, c) \
					droid_lkm_p_schedule_hrtimeout_range_clock((e), (d), (m), (c))


#define simple_statfs		droid_lkm_mq_simple_statfs
#define simple_lookup		droid_lkm_mq_simple_lookup
#define kill_litter_super	droid_lkm_mq_kill_sb

int droid_lkm_mq_simple_statfs(struct dentry *dentry, struct kstatfs *buf);
struct dentry *droid_lkm_mq_simple_lookup(struct inode *dir,
					  struct dentry *dentry,
					  unsigned int flags);
void droid_lkm_mq_kill_sb(struct super_block *sb);

int droid_lkm_mqueue_shim_init(void);
bool droid_lkm_mqueue_ready(void);
void droid_lkm_mqueue_shim_retract(void);


#define DROID_LKM_DFLT_QUEUESMAX	256
#define DROID_LKM_MIN_MSGMAX		1
#define DROID_LKM_DFLT_MSG		10U
#define DROID_LKM_DFLT_MSGMAX		10
#define DROID_LKM_HARD_MSGMAX		65536
#define DROID_LKM_MIN_MSGSIZEMAX	128
#define DROID_LKM_DFLT_MSGSIZE		8192U
#define DROID_LKM_DFLT_MSGSIZEMAX	8192
#define DROID_LKM_HARD_MSGSIZEMAX	(16 * 1024 * 1024)


int droid_lkm_mq_init_ns(struct ipc_namespace *ns);
void droid_lkm_mq_clear_sbinfo(struct ipc_namespace *ns);
void droid_lkm_mq_put_mnt(struct ipc_namespace *ns);
int __init droid_lkm_mqueue_fs_init(void);
void droid_lkm_mqueue_fs_exit(void);

bool droid_lkm_setup_mq_sysctls(struct ipc_namespace *ns);
void droid_lkm_retire_mq_sysctls(struct ipc_namespace *ns);

#endif

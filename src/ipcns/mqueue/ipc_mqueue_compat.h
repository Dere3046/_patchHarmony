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
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif


extern typeof(&getname) droid_lkm_p_getname;
extern typeof(&putname) droid_lkm_p_putname;
extern typeof(&get_next_ino) droid_lkm_p_get_next_ino;
extern typeof(&vfs_mkobj) droid_lkm_p_vfs_mkobj;
extern typeof(&fs_context_for_mount) droid_lkm_p_fs_context_for_mount;
extern typeof(&get_tree_keyed) droid_lkm_p_get_tree_keyed;
extern typeof(&get_ucounts) droid_lkm_p_get_ucounts;
extern typeof(&put_ucounts) droid_lkm_p_put_ucounts;
extern typeof(&inc_rlimit_ucounts) droid_lkm_p_inc_rlimit_ucounts;
extern typeof(&dec_rlimit_ucounts) droid_lkm_p_dec_rlimit_ucounts;
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
#define get_ucounts(u)			droid_lkm_p_get_ucounts(u)
#define put_ucounts(u)			droid_lkm_p_put_ucounts(u)
#define inc_rlimit_ucounts(u, t, v)	droid_lkm_p_inc_rlimit_ucounts((u), (t), (v))
#define dec_rlimit_ucounts(u, t, v)	droid_lkm_p_dec_rlimit_ucounts((u), (t), (v))
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

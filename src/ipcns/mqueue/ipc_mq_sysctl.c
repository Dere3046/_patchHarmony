// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2007 IBM Corporation
 *
 *  Author: Cedric Le Goater <clg@fr.ibm.com>
 *
 * ---------------------------------------------------------------------------
 *
 */

#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/sysctl.h>

#include <linux/stat.h>
#include <linux/capability.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/sem.h>

#include "ds.h"
#include "ds_compat.h"
#include "ipc_util.h"
#include "ipc_sysctl.h"

#include "ipc_mqueue_compat.h"

static int msg_max_limit_min = DROID_LKM_MIN_MSGMAX;
static int msg_max_limit_max = DROID_LKM_HARD_MSGMAX;

static int msg_maxsize_limit_min = DROID_LKM_MIN_MSGSIZEMAX;
static int msg_maxsize_limit_max = DROID_LKM_HARD_MSGSIZEMAX;

static struct ctl_table mq_sysctls[] = {
	{
		.procname	= "queues_max",
		.data		= &init_ipc_ns.mq_queues_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "msg_max",
		.data		= &init_ipc_ns.mq_msg_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_max",
		.data		= &init_ipc_ns.mq_msgsize_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
	{
		.procname	= "msg_default",
		.data		= &init_ipc_ns.mq_msg_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_max_limit_min,
		.extra2		= &msg_max_limit_max,
	},
	{
		.procname	= "msgsize_default",
		.data		= &init_ipc_ns.mq_msgsize_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &msg_maxsize_limit_min,
		.extra2		= &msg_maxsize_limit_max,
	},
};

static struct ctl_table_set *set_lookup(struct ctl_table_root *root)
{
	return &current->nsproxy->ipc_ns->mq_set;
}

static int set_is_seen(struct ctl_table_set *set)
{
	return &current->nsproxy->ipc_ns->mq_set == set;
}

static void mq_set_ownership(struct ctl_table_header *head,
			     kuid_t *uid, kgid_t *gid)
{
	struct ipc_namespace *ns =
		container_of(head->set, struct ipc_namespace, mq_set);

	kuid_t ns_root_uid = make_kuid(ns->user_ns, 0);
	kgid_t ns_root_gid = make_kgid(ns->user_ns, 0);

	*uid = uid_valid(ns_root_uid) ? ns_root_uid : GLOBAL_ROOT_UID;
	*gid = gid_valid(ns_root_gid) ? ns_root_gid : GLOBAL_ROOT_GID;
}

static int mq_permissions(struct ctl_table_header *head, DROID_LKM_CTL_TABLE *table)
{
	int mode = table->mode;
	kuid_t ns_root_uid;
	kgid_t ns_root_gid;

	mq_set_ownership(head, &ns_root_uid, &ns_root_gid);

	if (uid_eq(current_euid(), ns_root_uid))
		mode >>= 6;

	else if (droid_lkm_sysctl_in_egroup_p(ns_root_gid))
		mode >>= 3;

	mode &= 7;

	return (mode << 6) | (mode << 3) | mode;
}

/*
 * 6.6 passes the table to ctl_table_root::set_ownership, 6.12 does not. adapt
 * the 6.12 shaped handler to the signature the build kernel expects.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define DROID_LKM_MQ_SET_OWNERSHIP	mq_set_ownership
#else
static void mq_set_ownership_compat(struct ctl_table_header *head,
				    struct ctl_table *table, kuid_t *uid, kgid_t *gid)
{
	mq_set_ownership(head, uid, gid);
}
#define DROID_LKM_MQ_SET_OWNERSHIP	mq_set_ownership_compat
#endif

static struct ctl_table_root set_root = {
	.lookup = set_lookup,
	.permissions = mq_permissions,
	.set_ownership = DROID_LKM_MQ_SET_OWNERSHIP,
};

bool droid_lkm_setup_mq_sysctls(struct ipc_namespace *ns)
{
	struct ctl_table *tbl;

	
	if (!droid_lkm_sysctl_ready())
		return true;

	droid_lkm_sysctl_setup_set(&ns->mq_set, &set_root, set_is_seen);

	tbl = kmemdup(mq_sysctls, sizeof(mq_sysctls), GFP_KERNEL);
	if (tbl) {
		int i;

		for (i = 0; i < ARRAY_SIZE(mq_sysctls); i++) {
			if (tbl[i].data == &init_ipc_ns.mq_queues_max)
				tbl[i].data = &ns->mq_queues_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msg_max)
				tbl[i].data = &ns->mq_msg_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msgsize_max)
				tbl[i].data = &ns->mq_msgsize_max;

			else if (tbl[i].data == &init_ipc_ns.mq_msg_default)
				tbl[i].data = &ns->mq_msg_default;

			else if (tbl[i].data == &init_ipc_ns.mq_msgsize_default)
				tbl[i].data = &ns->mq_msgsize_default;
			else
				tbl[i].data = NULL;
		}

		ns->mq_sysctls = droid_lkm_sysctl_register_table(
			&ns->mq_set, "fs/mqueue", tbl, ARRAY_SIZE(mq_sysctls));
	}
	if (!ns->mq_sysctls) {
		kfree(tbl);
		droid_lkm_sysctl_retire_set(&ns->mq_set);
		return false;
	}

	return true;
}

void droid_lkm_retire_mq_sysctls(struct ipc_namespace *ns)
{
	const struct ctl_table *tbl;

	if (!ns->mq_sysctls)
		return;

	tbl = ns->mq_sysctls->ctl_table_arg;
	unregister_sysctl_table(ns->mq_sysctls);
	ns->mq_sysctls = NULL;
	droid_lkm_sysctl_retire_set(&ns->mq_set);
	kfree(tbl);
}

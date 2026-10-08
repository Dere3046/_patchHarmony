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

#include "ds_caps.h"
#include "ds.h"
#include "ds_compat.h"
#include "ipc_util.h"
#include "ipc_sysctl.h"
#include "ds_ipcns.h"
#include "ds_ti.h"

#include "ipc_mqueue_compat.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)

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
	/*
	 * the three argument __register_sysctl_table() below 6.6 walks the table
	 * until procname is NULL, so it ends with an empty entry and the count
	 * handed to the registration excludes it.
	 */
	{ }
};

/* the sentinel is not an entry */
#define DROID_LKM_MQ_SYSCTL_COUNT	(ARRAY_SIZE(mq_sysctls) - 1)

// ipc_namespace members go through the layout table
// a build offset is what the 6.12 device crashed on
#define DROID_LKM_NS_MQ_SET(_ns)                                               \
	((struct ctl_table_set *)droid_lkm_layout_ptr((_ns),                   \
			DROID_LKM_F_IPC_NS_MQ_SET))
#define DROID_LKM_NS_MQ_SYSCTLS(_ns)                                           \
	((struct ctl_table_header **)droid_lkm_layout_ptr((_ns),               \
			DROID_LKM_F_IPC_NS_MQ_SYSCTLS))
#define DROID_LKM_NS_FIELD(_ns, _id)                                           \
	((void *)droid_lkm_layout_ptr((_ns), (_id)))
#define DROID_LKM_INIT_NS_FIELD(_id)                                           \
	((void *)((char *)&init_ipc_ns + droid_lkm_layout_off(_id)))

static bool droid_lkm_mq_sysctl_fields_ok(void)
{
	if (!droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MQ_SET) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MQ_SYSCTLS) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_USER_NS) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MQ_QUEUES_MAX) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MQ_MSG_MAX) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MQ_MSGSIZE_MAX) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MQ_MSG_DEFAULT) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MQ_MSGSIZE_DEFAULT))
		return false;

	return true;
}

static struct ctl_table_set *set_lookup(struct ctl_table_root *root)
{
	return DROID_LKM_NS_MQ_SET(droid_lkm_ipcns_current());
}

static int set_is_seen(struct ctl_table_set *set)
{
	return DROID_LKM_NS_MQ_SET(droid_lkm_ipcns_current()) == set;
}

static void mq_set_ownership(struct ctl_table_header *head,
			     kuid_t *uid, kgid_t *gid)
{
	struct ipc_namespace *ns =
		(struct ipc_namespace *)((char *)head->set -
			droid_lkm_layout_off(DROID_LKM_F_IPC_NS_MQ_SET));

	struct user_namespace *user_ns = *(struct user_namespace **)
		DROID_LKM_NS_FIELD(ns, DROID_LKM_F_IPC_NS_USER_NS);
	kuid_t ns_root_uid = make_kuid(user_ns, 0);
	kgid_t ns_root_gid = make_kgid(user_ns, 0);

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
static void mq_set_ownership_compat(struct ctl_table_header *head,
				    DROID_LKM_CTL_TABLE *table, kuid_t *uid, kgid_t *gid)
{
	mq_set_ownership(head, uid, gid);
}

/* the kernel builds with -Wcast-function-type-strict, assign through void * */
#define DROID_LKM_SET_HOOK(_field, _fn)	(*(void **)&(_field) = (void *)(_fn))

static struct ctl_table_root set_root = {
	.lookup = set_lookup,
};

bool droid_lkm_setup_mq_sysctls(struct ipc_namespace *ns)
{
	/* 6.12 dropped the table argument, install the shape the kernel calls */
	DROID_LKM_SET_HOOK(set_root.permissions, mq_permissions);
	if (droid_lkm_caps.ctl_takes_table)
		DROID_LKM_SET_HOOK(set_root.set_ownership, mq_set_ownership_compat);
	else
		DROID_LKM_SET_HOOK(set_root.set_ownership, mq_set_ownership);
	struct ctl_table *tbl;

	
	if (!droid_lkm_sysctl_ready())
		return true;

	if (!droid_lkm_mq_sysctl_fields_ok()) {
		droid_lkm_warn("mqueue sysctls refused: the running kernel's layout does not place ipc_namespace.mq_set\n");
		return false;
	}

	droid_lkm_sysctl_setup_set(DROID_LKM_NS_MQ_SET(ns), &set_root, set_is_seen);

	tbl = kmemdup(mq_sysctls,
		      DROID_LKM_MQ_SYSCTL_COUNT * sizeof(mq_sysctls[0]),
		      GFP_KERNEL);
	if (tbl) {
		int i;

		for (i = 0; i < DROID_LKM_MQ_SYSCTL_COUNT; i++) {
			if (tbl[i].data ==
			    DROID_LKM_INIT_NS_FIELD(DROID_LKM_F_IPC_NS_MQ_QUEUES_MAX))
				tbl[i].data = DROID_LKM_NS_FIELD(ns,
					DROID_LKM_F_IPC_NS_MQ_QUEUES_MAX);

			else if (tbl[i].data ==
				 DROID_LKM_INIT_NS_FIELD(DROID_LKM_F_IPC_NS_MQ_MSG_MAX))
				tbl[i].data = DROID_LKM_NS_FIELD(ns,
					DROID_LKM_F_IPC_NS_MQ_MSG_MAX);

			else if (tbl[i].data ==
				 DROID_LKM_INIT_NS_FIELD(DROID_LKM_F_IPC_NS_MQ_MSGSIZE_MAX))
				tbl[i].data = DROID_LKM_NS_FIELD(ns,
					DROID_LKM_F_IPC_NS_MQ_MSGSIZE_MAX);

			else if (tbl[i].data ==
				 DROID_LKM_INIT_NS_FIELD(DROID_LKM_F_IPC_NS_MQ_MSG_DEFAULT))
				tbl[i].data = DROID_LKM_NS_FIELD(ns,
					DROID_LKM_F_IPC_NS_MQ_MSG_DEFAULT);

			else if (tbl[i].data ==
				 DROID_LKM_INIT_NS_FIELD(DROID_LKM_F_IPC_NS_MQ_MSGSIZE_DEFAULT))
				tbl[i].data = DROID_LKM_NS_FIELD(ns,
					DROID_LKM_F_IPC_NS_MQ_MSGSIZE_DEFAULT);
			else
				tbl[i].data = NULL;
		}

		*DROID_LKM_NS_MQ_SYSCTLS(ns) = droid_lkm_sysctl_register_table(
			DROID_LKM_NS_MQ_SET(ns), "fs/mqueue", tbl,
			DROID_LKM_MQ_SYSCTL_COUNT);
	}
	if (!*DROID_LKM_NS_MQ_SYSCTLS(ns)) {
		kfree(tbl);
		droid_lkm_sysctl_retire_set(DROID_LKM_NS_MQ_SET(ns));
		return false;
	}

	return true;
}

void droid_lkm_retire_mq_sysctls(struct ipc_namespace *ns)
{
	const struct ctl_table *tbl;

	if (!*DROID_LKM_NS_MQ_SYSCTLS(ns))
		return;

	tbl = (*DROID_LKM_NS_MQ_SYSCTLS(ns))->ctl_table_arg;
	unregister_sysctl_table(*DROID_LKM_NS_MQ_SYSCTLS(ns));
	*DROID_LKM_NS_MQ_SYSCTLS(ns) = NULL;
	droid_lkm_sysctl_retire_set(DROID_LKM_NS_MQ_SET(ns));
	kfree(tbl);
}

#else /* < 6.1 */

/*
 * 5.10 and 5.15 have no per namespace sysctl set. they register the ipc and
 * mqueue sysctls once at boot, and those handlers relocate table->data from
 * init_ipc_ns to current->nsproxy->ipc_ns (ipc/mq_sysctl.c: get_mq()), so the
 * kernel's own table already serves a fake ipc namespace. nothing to add here,
 * and the caller must not treat that as a failure.
 */

bool droid_lkm_setup_mq_sysctls(struct ipc_namespace *ns)
{
	return true;
}

void droid_lkm_retire_mq_sysctls(struct ipc_namespace *ns)
{
}

#endif

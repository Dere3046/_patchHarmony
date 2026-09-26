// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007 Eric Biederman <ebiederm@xmision.com>
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysctl.h>
#include <linux/ipc.h>
#include <linux/sem.h>
#include <linux/shm.h>
#include <linux/msg.h>
#include <linux/ipc_namespace.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/user_namespace.h>
#include <linux/uidgid.h>

#include "ds.h"
#include "ds_compat.h"
#include "ds_ksym.h"
#include "ds_ipcns.h"
#include "ipc_util.h"
#include "ipc_sysctl.h"


void droid_lkm_shm_destroy_orphaned(struct ipc_namespace *ns);


static void (*droid_lkm_setup_sysctl_set_fn)(
	struct ctl_table_set *set, struct ctl_table_root *root,
	int (*is_seen)(struct ctl_table_set *));
static void (*droid_lkm_retire_sysctl_set_fn)(struct ctl_table_set *set);
static struct ctl_table_header *(*droid_lkm_register_sysctl_table_fn)(
	struct ctl_table_set *set, const char *path, struct ctl_table *table,
	size_t table_size);
static int (*droid_lkm_in_egroup_p_fn)(kgid_t grp);

static bool droid_lkm_ipc_sysctls_ready;


static int droid_lkm_sysctl_zero;
static int droid_lkm_sysctl_one = 1;
static int droid_lkm_sysctl_int_max = INT_MAX;
static int droid_lkm_sysctl_mni;

static int droid_lkm_ipc_dointvec_minmax_orphans(DROID_LKM_CTL_TABLE *table,
						 int write, void *buffer,
						 size_t *lenp, loff_t *ppos)
{
	struct ipc_namespace *ns =
		container_of(table->data, struct ipc_namespace, shm_rmid_forced);
	int err;

	err = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (err < 0)
		return err;
	if (ns->shm_rmid_forced)
		droid_lkm_shm_destroy_orphaned(ns);
	return err;
}

static int droid_lkm_ipc_auto_msgmni(DROID_LKM_CTL_TABLE *table, int write,
				     void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table ipc_table;
	int dummy = 0;

	memcpy(&ipc_table, table, sizeof(ipc_table));
	ipc_table.data = &dummy;

	if (write)
		pr_info_once("writing to auto_msgmni has no effect");

	return proc_dointvec_minmax(&ipc_table, write, buffer, lenp, ppos);
}

static int droid_lkm_ipc_sem_dointvec(DROID_LKM_CTL_TABLE *table, int write,
				      void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ipc_namespace *ns =
		container_of(table->data, struct ipc_namespace, sem_ctls);
	int ret, semmni;

	semmni = ns->sem_ctls[3];
	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!ret)
		ret = sem_check_semmni(ns);
	if (ret)
		ns->sem_ctls[3] = semmni;
	return ret;
}

static struct ctl_table droid_lkm_ipc_sysctls[] = {
	{
		.procname	= "shmmax",
		.data		= &init_ipc_ns.shm_ctlmax,
		.maxlen		= sizeof(init_ipc_ns.shm_ctlmax),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "shmall",
		.data		= &init_ipc_ns.shm_ctlall,
		.maxlen		= sizeof(init_ipc_ns.shm_ctlall),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "shmmni",
		.data		= &init_ipc_ns.shm_ctlmni,
		.maxlen		= sizeof(init_ipc_ns.shm_ctlmni),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_mni,
	},
	{
		.procname	= "shm_rmid_forced",
		.data		= &init_ipc_ns.shm_rmid_forced,
		.maxlen		= sizeof(init_ipc_ns.shm_rmid_forced),
		.mode		= 0644,
		.proc_handler	= droid_lkm_ipc_dointvec_minmax_orphans,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_one,
	},
	{
		.procname	= "msgmax",
		.data		= &init_ipc_ns.msg_ctlmax,
		.maxlen		= sizeof(init_ipc_ns.msg_ctlmax),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_int_max,
	},
	{
		.procname	= "msgmni",
		.data		= &init_ipc_ns.msg_ctlmni,
		.maxlen		= sizeof(init_ipc_ns.msg_ctlmni),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_mni,
	},
	{
		.procname	= "auto_msgmni",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= droid_lkm_ipc_auto_msgmni,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_one,
	},
	{
		.procname	= "msgmnb",
		.data		= &init_ipc_ns.msg_ctlmnb,
		.maxlen		= sizeof(init_ipc_ns.msg_ctlmnb),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_int_max,
	},
	{
		.procname	= "sem",
		.data		= &init_ipc_ns.sem_ctls,
		.maxlen		= 4 * sizeof(int),
		.mode		= 0644,
		.proc_handler	= droid_lkm_ipc_sem_dointvec,
	},
};

static struct ctl_table_set *droid_lkm_ipc_set_lookup(struct ctl_table_root *root)
{
	return &droid_lkm_ipcns_task_ns(current)->ipc_set;
}

static int droid_lkm_ipc_set_is_seen(struct ctl_table_set *set)
{
	return &droid_lkm_ipcns_task_ns(current)->ipc_set == set;
}

static void droid_lkm_ipc_set_ownership(struct ctl_table_header *head,
					kuid_t *uid, kgid_t *gid)
{
	struct ipc_namespace *ns =
		container_of(head->set, struct ipc_namespace, ipc_set);
	
	kuid_t ns_root_uid = make_kuid(ns->user_ns, 0);
	kgid_t ns_root_gid = make_kgid(ns->user_ns, 0);

	*uid = uid_valid(ns_root_uid) ? ns_root_uid : GLOBAL_ROOT_UID;
	*gid = gid_valid(ns_root_gid) ? ns_root_gid : GLOBAL_ROOT_GID;
}

static int droid_lkm_ipc_permissions(struct ctl_table_header *head,
				     DROID_LKM_CTL_TABLE *table)
{
	int mode = table->mode;
	kuid_t ns_root_uid;
	kgid_t ns_root_gid;

	droid_lkm_ipc_set_ownership(head, &ns_root_uid, &ns_root_gid);

	if (uid_eq(current_euid(), ns_root_uid))
		mode >>= 6;
	else if (droid_lkm_in_egroup_p_fn &&
		 droid_lkm_in_egroup_p_fn(ns_root_gid))
		mode >>= 3;

	mode &= 7;
	return (mode << 6) | (mode << 3) | mode;
}

/*
 * 6.6 passes the table to ctl_table_root::set_ownership, 6.12 does not. adapt
 * the 6.12 shaped handler to the signature the build kernel expects.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define DROID_LKM_IPC_SET_OWNERSHIP	droid_lkm_ipc_set_ownership
#else
static void droid_lkm_ipc_set_ownership_compat(struct ctl_table_header *head,
					       struct ctl_table *table,
					       kuid_t *uid, kgid_t *gid)
{
	droid_lkm_ipc_set_ownership(head, uid, gid);
}
#define DROID_LKM_IPC_SET_OWNERSHIP	droid_lkm_ipc_set_ownership_compat
#endif

static struct ctl_table_root droid_lkm_ipc_set_root = {
	.lookup		= droid_lkm_ipc_set_lookup,
	.permissions	= droid_lkm_ipc_permissions,
	.set_ownership	= DROID_LKM_IPC_SET_OWNERSHIP,
};

bool droid_lkm_ipc_sysctls_setup(struct ipc_namespace *ns)
{
	struct ctl_table *tbl;
	int i;

	if (!droid_lkm_ipc_sysctls_ready)
		return false;

	droid_lkm_setup_sysctl_set_fn(&ns->ipc_set, &droid_lkm_ipc_set_root,
				      droid_lkm_ipc_set_is_seen);

	tbl = kmemdup(droid_lkm_ipc_sysctls, sizeof(droid_lkm_ipc_sysctls),
		      GFP_KERNEL);
	if (!tbl) {
		droid_lkm_retire_sysctl_set_fn(&ns->ipc_set);
		return false;
	}

	for (i = 0; i < ARRAY_SIZE(droid_lkm_ipc_sysctls); i++) {
		if (tbl[i].data == &init_ipc_ns.shm_ctlmax)
			tbl[i].data = &ns->shm_ctlmax;
		else if (tbl[i].data == &init_ipc_ns.shm_ctlall)
			tbl[i].data = &ns->shm_ctlall;
		else if (tbl[i].data == &init_ipc_ns.shm_ctlmni)
			tbl[i].data = &ns->shm_ctlmni;
		else if (tbl[i].data == &init_ipc_ns.shm_rmid_forced)
			tbl[i].data = &ns->shm_rmid_forced;
		else if (tbl[i].data == &init_ipc_ns.msg_ctlmax)
			tbl[i].data = &ns->msg_ctlmax;
		else if (tbl[i].data == &init_ipc_ns.msg_ctlmni)
			tbl[i].data = &ns->msg_ctlmni;
		else if (tbl[i].data == &init_ipc_ns.msg_ctlmnb)
			tbl[i].data = &ns->msg_ctlmnb;
		else if (tbl[i].data == &init_ipc_ns.sem_ctls)
			tbl[i].data = &ns->sem_ctls;
		else
			tbl[i].data = NULL;
	}

	ns->ipc_sysctls = droid_lkm_register_sysctl_table_fn(
		&ns->ipc_set, "kernel", tbl,
		ARRAY_SIZE(droid_lkm_ipc_sysctls));
	if (!ns->ipc_sysctls) {
		kfree(tbl);
		droid_lkm_retire_sysctl_set_fn(&ns->ipc_set);
		return false;
	}

	return true;
}

void droid_lkm_ipc_sysctls_retire(struct ipc_namespace *ns)
{
	const struct ctl_table *tbl;

	if (!ns->ipc_sysctls)
		return;

	tbl = ns->ipc_sysctls->ctl_table_arg;
	unregister_sysctl_table(ns->ipc_sysctls);
	ns->ipc_sysctls = NULL;
	droid_lkm_retire_sysctl_set_fn(&ns->ipc_set);
	kfree(tbl);
}

bool droid_lkm_ipc_sysctls_ok(void)
{
	return droid_lkm_ipc_sysctls_ready;
}


bool droid_lkm_sysctl_ready(void)
{
	return droid_lkm_ipc_sysctls_ready;
}

void droid_lkm_sysctl_setup_set(struct ctl_table_set *set,
				struct ctl_table_root *root,
				int (*is_seen)(struct ctl_table_set *))
{
	droid_lkm_setup_sysctl_set_fn(set, root, is_seen);
}

void droid_lkm_sysctl_retire_set(struct ctl_table_set *set)
{
	droid_lkm_retire_sysctl_set_fn(set);
}

struct ctl_table_header *droid_lkm_sysctl_register_table(
	struct ctl_table_set *set, const char *path, struct ctl_table *table,
	size_t table_size)
{
	return droid_lkm_register_sysctl_table_fn(set, path, table, table_size);
}

int droid_lkm_sysctl_in_egroup_p(kgid_t grp)
{
	if (!droid_lkm_in_egroup_p_fn)
		return 0;
	return droid_lkm_in_egroup_p_fn(grp);
}

int droid_lkm_ipc_sysctls_init(void)
{
	droid_lkm_sysctl_mni = ipc_mni;

	droid_lkm_setup_sysctl_set_fn =
		(void *)droid_lkm_sym("setup_sysctl_set");
	droid_lkm_retire_sysctl_set_fn =
		(void *)droid_lkm_sym("retire_sysctl_set");
	droid_lkm_register_sysctl_table_fn =
		(void *)droid_lkm_sym("__register_sysctl_table");
	droid_lkm_in_egroup_p_fn = (void *)droid_lkm_sym("in_egroup_p");

	
	if (!droid_lkm_setup_sysctl_set_fn ||
	    !droid_lkm_retire_sysctl_set_fn ||
	    !droid_lkm_register_sysctl_table_fn) {
		droid_lkm_warn("ipc sysctls unavailable (setup=%p retire=%p reg=%p) - ipc namespaces will be created without /proc/sys/kernel/{shmmax,...}, equivalent to SYSVIPC=y + SYSVIPC_SYSCTL=n\n",
			       droid_lkm_setup_sysctl_set_fn,
			       droid_lkm_retire_sysctl_set_fn,
			       droid_lkm_register_sysctl_table_fn);
		return 0;
	}
	if (!droid_lkm_in_egroup_p_fn)
		droid_lkm_warn("ipc sysctls: in_egroup_p unresolved, group check disabled\n");

	droid_lkm_ipc_sysctls_ready = true;

	
	if (!droid_lkm_ipc_sysctls_setup(&init_ipc_ns)) {
		droid_lkm_ipc_sysctls_ready = false;
		droid_lkm_warn("host ipc sysctls registration failed - falling back to the SYSVIPC_SYSCTL=n equivalent (ipc namespaces keep working, the /proc/sys/kernel/{shmmax,...} files will be missing)\n");
		return 0;
	}

	droid_lkm_info("ipc sysctls registered for the host ipc ns (shmmax/shmall/shmmni/shm_rmid_forced/msgmax/msgmnb/msgmni/sem/auto_msgmni)\n");
	return 0;
}

void droid_lkm_ipc_sysctls_exit(void)
{
	if (!droid_lkm_ipc_sysctls_ready)
		return;

	droid_lkm_ipc_sysctls_retire(&init_ipc_ns);
	droid_lkm_ipc_sysctls_ready = false;
}

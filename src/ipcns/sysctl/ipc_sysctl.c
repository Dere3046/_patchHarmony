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

#include "ds_caps.h"
#include "ds.h"
#include "ds_ti.h"
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
	struct ctl_table_set *set, const char *path, struct ctl_table *table
	DROID_LKM_REG_SYSCTL_PARAM);
static int (*droid_lkm_in_egroup_p_fn)(kgid_t grp);

static bool droid_lkm_ipc_sysctls_ready;

/*
 * 6.1 gave an ipc namespace its own ctl_table_set, so a fake namespace needs
 * its tables installed here, one copy per namespace. 5.10 and 5.15 have no
 * such set: the module registers the shared table once on the default set and
 * the handlers relocate table->data to the namespace of the calling task, the
 * model the kernel's own ipc table uses on those kernels.
 */
/*
 * below 6.1 there is no per namespace ctl_table_set, so exactly one table is
 * registered on the default set and the data pointer is relocated to the
 * namespace of the task that reads it, which is the model the kernel's own ipc
 * table uses there. from 6.1 on the table is copied per namespace and the
 * pointer is already the right one.
 */
#define DROID_LKM_NS_FIELD(_ns, _id)                                           \
	((void *)droid_lkm_layout_ptr((_ns), (_id)))
#define DROID_LKM_INIT_NS_FIELD(_id)                                           \
	((void *)((char *)&init_ipc_ns + droid_lkm_layout_off(_id)))

static inline void *droid_lkm_ipc_data(DROID_LKM_CTL_TABLE *table)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	return table->data;
#else
	return (char *)table->data - (char *)&init_ipc_ns +
	       (char *)droid_lkm_ipcns_task_ns(current);
#endif
}

// namespace from a field pointer through the running kernel offset
static struct ipc_namespace *droid_lkm_ipc_ns_of(void *field,
						 enum droid_lkm_field_id id)
{
	return (struct ipc_namespace *)((char *)field -
					droid_lkm_layout_off(id));
}

/*
 * the two standard handlers are reached through the thunk table because a stock
 * image may not export them (5.10 and 5.15 do not), and the copy carries the
 * relocated data pointer for the branches that have no per namespace table.
 */
__nocfi noinline static int droid_lkm_ipc_dointvec_minmax(DROID_LKM_CTL_TABLE *table, int write,
					 void *buffer, size_t *lenp,
					 loff_t *ppos)
{
	struct ctl_table copy = *table;

	if (!droid_lkm_ks.proc_dointvec_minmax)
		return -ENOSYS;
	copy.data = droid_lkm_ipc_data(table);
	return droid_lkm_ks.proc_dointvec_minmax(&copy, write, buffer, lenp,
						 ppos);
}

__nocfi noinline static int droid_lkm_ipc_doulongvec_minmax(DROID_LKM_CTL_TABLE *table,
					   int write, void *buffer,
					   size_t *lenp, loff_t *ppos)
{
	struct ctl_table copy = *table;

	if (!droid_lkm_ks.proc_doulongvec_minmax)
		return -ENOSYS;
	copy.data = droid_lkm_ipc_data(table);
	return droid_lkm_ks.proc_doulongvec_minmax(&copy, write, buffer, lenp,
						   ppos);
}

#define DROID_LKM_IPC_DOINTVEC_MINMAX		droid_lkm_ipc_dointvec_minmax
#define DROID_LKM_IPC_DOULONGVEC_MINMAX		droid_lkm_ipc_doulongvec_minmax

static int droid_lkm_sysctl_zero;
static int droid_lkm_sysctl_one = 1;
static int droid_lkm_sysctl_int_max = INT_MAX;
static int droid_lkm_sysctl_mni;

__nocfi noinline static int droid_lkm_ipc_dointvec_minmax_orphans(DROID_LKM_CTL_TABLE *table,
						 int write, void *buffer,
						 size_t *lenp, loff_t *ppos)
{
	struct ipc_namespace *ns =
		droid_lkm_ipc_ns_of(droid_lkm_ipc_data(table),
				    DROID_LKM_F_IPC_NS_SHM_RMID_FORCED);
	int err;

	struct ctl_table copy = *table;

	copy.data = droid_lkm_ipc_data(table);
	if (!droid_lkm_ks.proc_dointvec_minmax)
		return -ENOSYS;
	err = droid_lkm_ks.proc_dointvec_minmax(&copy, write, buffer, lenp, ppos);
	if (err < 0)
		return err;
	if (*(int *)DROID_LKM_NS_FIELD(ns, DROID_LKM_F_IPC_NS_SHM_RMID_FORCED))
		droid_lkm_shm_destroy_orphaned(ns);
	return err;
}

__nocfi noinline static int droid_lkm_ipc_auto_msgmni(DROID_LKM_CTL_TABLE *table, int write,
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

__nocfi noinline static int droid_lkm_ipc_sem_dointvec(DROID_LKM_CTL_TABLE *table, int write,
				      void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ipc_namespace *ns =
		droid_lkm_ipc_ns_of(droid_lkm_ipc_data(table),
				    DROID_LKM_F_IPC_NS_SEM_CTLS);
	int ret, semmni;

	semmni = ((int *)DROID_LKM_NS_FIELD(ns, DROID_LKM_F_IPC_NS_SEM_CTLS))[3];
	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!ret)
		ret = sem_check_semmni(ns);
	if (ret)
		((int *)DROID_LKM_NS_FIELD(ns, DROID_LKM_F_IPC_NS_SEM_CTLS))[3] = semmni;
	return ret;
}

// one field id per table entry in the same order
static const enum droid_lkm_field_id droid_lkm_ipc_sysctl_fields[] = {
	DROID_LKM_F_IPC_NS_SHM_CTLMAX,
	DROID_LKM_F_IPC_NS_SHM_CTLALL,
	DROID_LKM_F_IPC_NS_SHM_CTLMNI,
	DROID_LKM_F_IPC_NS_SHM_RMID_FORCED,
	DROID_LKM_F_IPC_NS_MSG_CTLMAX,
	DROID_LKM_F_IPC_NS_MSG_CTLMNI,
	DROID_LKM_FIELD_COUNT,		/* auto_msgmni keeps a NULL data */
	DROID_LKM_F_IPC_NS_MSG_CTLMNB,
	DROID_LKM_F_IPC_NS_SEM_CTLS,
};

static struct ctl_table droid_lkm_ipc_sysctls[] = {
	{
		.procname	= "shmmax",
		.data		= NULL,
		.maxlen		= sizeof(size_t),
		.mode		= 0644,
		.proc_handler	= DROID_LKM_IPC_DOULONGVEC_MINMAX,
	},
	{
		.procname	= "shmall",
		.data		= NULL,
		.maxlen		= sizeof(size_t),
		.mode		= 0644,
		.proc_handler	= DROID_LKM_IPC_DOULONGVEC_MINMAX,
	},
	{
		.procname	= "shmmni",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= DROID_LKM_IPC_DOINTVEC_MINMAX,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_mni,
	},
	{
		.procname	= "shm_rmid_forced",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= (proc_handler *)droid_lkm_ipc_dointvec_minmax_orphans,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_one,
	},
	{
		.procname	= "msgmax",
		.data		= NULL,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= DROID_LKM_IPC_DOINTVEC_MINMAX,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_int_max,
	},
	{
		.procname	= "msgmni",
		.data		= NULL,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= DROID_LKM_IPC_DOINTVEC_MINMAX,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_mni,
	},
	{
		.procname	= "auto_msgmni",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= (proc_handler *)droid_lkm_ipc_auto_msgmni,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_one,
	},
	{
		.procname	= "msgmnb",
		.data		= NULL,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= DROID_LKM_IPC_DOINTVEC_MINMAX,
		.extra1		= &droid_lkm_sysctl_zero,
		.extra2		= &droid_lkm_sysctl_int_max,
	},
	{
		.procname	= "sem",
		.data		= NULL,
		.maxlen		= 4 * sizeof(int),
		.mode		= 0644,
		.proc_handler	= (proc_handler *)droid_lkm_ipc_sem_dointvec,
	},
	/*
	 * the three argument __register_sysctl_table() every branch below 6.6
	 * walks the array until procname is NULL, so the table has to end with an
	 * empty entry even though 6.6 and up take the count explicitly.
	 */
	{ }
};

/* the sentinel is not an entry */
#define DROID_LKM_IPC_SYSCTL_COUNT	(ARRAY_SIZE(droid_lkm_ipc_sysctls) - 1)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)

// ipc_namespace members go through the layout table
// the 6.12 device crashed on a set pointer built from a build offset
#define DROID_LKM_NS_IPC_SET(_ns)                                              \
	((struct ctl_table_set *)droid_lkm_layout_ptr((_ns),                   \
			DROID_LKM_F_IPC_NS_IPC_SET))
#define DROID_LKM_NS_IPC_SYSCTLS(_ns)                                          \
	((struct ctl_table_header **)droid_lkm_layout_ptr((_ns),               \
			DROID_LKM_F_IPC_NS_IPC_SYSCTLS))
static bool droid_lkm_ipc_sysctl_fields_ok(void)
{
	if (!droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_IPC_SET) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_IPC_SYSCTLS) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_USER_NS) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_SHM_CTLMAX) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_SHM_CTLALL) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_SHM_CTLMNI) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_SHM_RMID_FORCED) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MSG_CTLMAX) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MSG_CTLMNI) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_MSG_CTLMNB) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_IPC_NS_SEM_CTLS))
		return false;

	return true;
}

static struct ctl_table_set *droid_lkm_ipc_set_lookup(struct ctl_table_root *root)
{
	return DROID_LKM_NS_IPC_SET(droid_lkm_ipcns_task_ns(current));
}

static int droid_lkm_ipc_set_is_seen(struct ctl_table_set *set)
{
	return DROID_LKM_NS_IPC_SET(droid_lkm_ipcns_task_ns(current)) == set;
}

static void droid_lkm_ipc_set_ownership(struct ctl_table_header *head,
					kuid_t *uid, kgid_t *gid)
{
	struct ipc_namespace *ns =
		(struct ipc_namespace *)((char *)head->set -
			droid_lkm_layout_off(DROID_LKM_F_IPC_NS_IPC_SET));
	struct user_namespace *user_ns = *(struct user_namespace **)
		DROID_LKM_NS_FIELD(ns, DROID_LKM_F_IPC_NS_USER_NS);
	kuid_t ns_root_uid = make_kuid(user_ns, 0);
	kgid_t ns_root_gid = make_kgid(user_ns, 0);

	*uid = uid_valid(ns_root_uid) ? ns_root_uid : GLOBAL_ROOT_UID;
	*gid = gid_valid(ns_root_gid) ? ns_root_gid : GLOBAL_ROOT_GID;
}

static __nocfi noinline int droid_lkm_ipc_permissions(struct ctl_table_header *head,
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
static void droid_lkm_ipc_set_ownership_compat(struct ctl_table_header *head,
					       DROID_LKM_CTL_TABLE *table,
					       kuid_t *uid, kgid_t *gid)
{
	droid_lkm_ipc_set_ownership(head, uid, gid);
}

/* the kernel builds with -Wcast-function-type-strict, assign through void * */
#define DROID_LKM_SET_HOOK(_field, _fn)	(*(void **)&(_field) = (void *)(_fn))

static struct ctl_table_root droid_lkm_ipc_set_root = {
	.lookup		= droid_lkm_ipc_set_lookup,
};

__nocfi noinline bool droid_lkm_ipc_sysctls_setup(struct ipc_namespace *ns)
{
	/* 6.12 dropped the table argument, install the shape the kernel calls */
	DROID_LKM_SET_HOOK(droid_lkm_ipc_set_root.permissions, droid_lkm_ipc_permissions);
	if (droid_lkm_caps.ctl_takes_table)
		DROID_LKM_SET_HOOK(droid_lkm_ipc_set_root.set_ownership, droid_lkm_ipc_set_ownership_compat);
	else
		DROID_LKM_SET_HOOK(droid_lkm_ipc_set_root.set_ownership, droid_lkm_ipc_set_ownership);
	struct ctl_table *tbl;
	int i;

	if (!droid_lkm_ipc_sysctls_ready)
		return false;

	if (!droid_lkm_ipc_sysctl_fields_ok()) {
		droid_lkm_warn("ipc sysctls refused: the running kernel's layout does not place ipc_namespace.ipc_set\n");
		return false;
	}

	droid_lkm_setup_sysctl_set_fn(DROID_LKM_NS_IPC_SET(ns),
				      &droid_lkm_ipc_set_root,
				      droid_lkm_ipc_set_is_seen);

	tbl = kmemdup(droid_lkm_ipc_sysctls,
		      DROID_LKM_IPC_SYSCTL_COUNT * sizeof(droid_lkm_ipc_sysctls[0]),
		      GFP_KERNEL);
	if (!tbl) {
		droid_lkm_retire_sysctl_set_fn(DROID_LKM_NS_IPC_SET(ns));
		return false;
	}

	for (i = 0; i < DROID_LKM_IPC_SYSCTL_COUNT; i++) {
		enum droid_lkm_field_id id = droid_lkm_ipc_sysctl_fields[i];

		if (id == DROID_LKM_FIELD_COUNT)
			continue;
		tbl[i].data = DROID_LKM_NS_FIELD(ns, id);
	}

	*DROID_LKM_NS_IPC_SYSCTLS(ns) = droid_lkm_sysctl_register_table(
		DROID_LKM_NS_IPC_SET(ns), "kernel", tbl,
		DROID_LKM_IPC_SYSCTL_COUNT);
	if (!*DROID_LKM_NS_IPC_SYSCTLS(ns)) {
		kfree(tbl);
		droid_lkm_retire_sysctl_set_fn(DROID_LKM_NS_IPC_SET(ns));
		return false;
	}

	return true;
}

__nocfi noinline void droid_lkm_ipc_sysctls_retire(struct ipc_namespace *ns)
{
	const struct ctl_table *tbl;

	if (!*DROID_LKM_NS_IPC_SYSCTLS(ns))
		return;

	tbl = (*DROID_LKM_NS_IPC_SYSCTLS(ns))->ctl_table_arg;
	unregister_sysctl_table(*DROID_LKM_NS_IPC_SYSCTLS(ns));
	*DROID_LKM_NS_IPC_SYSCTLS(ns) = NULL;
	droid_lkm_retire_sysctl_set_fn(DROID_LKM_NS_IPC_SET(ns));
	kfree(tbl);
}

/*
 * the host ipc namespace is set up at load time; every other namespace gets
 * its tables when it is created
 */
static int droid_lkm_ipc_sysctls_host_setup(void)
{
	droid_lkm_sysctl_mni = ipc_mni;

	if (!droid_lkm_ipc_sysctls_setup(droid_lkm_ipcns_host_ns()))
		return -ENOMEM;

	droid_lkm_info("ipc sysctls registered for the host ipc ns (shmmax/shmall/shmmni/shm_rmid_forced/msgmax/msgmnb/msgmni/sem/auto_msgmni)\n");
	return 0;
}

static void droid_lkm_ipc_sysctls_host_teardown(void)
{
	droid_lkm_ipc_sysctls_retire(droid_lkm_ipcns_host_ns());
}

#else /* < 6.1 */

/* the one global table, registered on the default set */
static struct ctl_table_header *droid_lkm_ipc_global_header;

static __nocfi noinline int droid_lkm_ipc_sysctls_host_setup(void)
{
	if (!droid_lkm_ks.register_sysctl) {
		droid_lkm_warn("register_sysctl unavailable, the SysV limits stay unexposed\n");
		return 0;
	}

	droid_lkm_sysctl_mni = IPCMNI;
	droid_lkm_ipc_global_header =
		droid_lkm_ks.register_sysctl("kernel", droid_lkm_ipc_sysctls);
	if (!droid_lkm_ipc_global_header) {
		droid_lkm_warn("global SysV sysctl registration failed, limits stay unexposed\n");
		return 0;
	}

	droid_lkm_info("ipc sysctls registered on the default set (shmmax/shmall/shmmni/shm_rmid_forced/msgmax/msgmnb/msgmni/sem/auto_msgmni)\n");
	return 0;
}

static __nocfi noinline void droid_lkm_ipc_sysctls_host_teardown(void)
{
	if (droid_lkm_ipc_global_header && droid_lkm_ks.unregister_sysctl_table) {
		droid_lkm_ks.unregister_sysctl_table(droid_lkm_ipc_global_header);
		droid_lkm_ipc_global_header = NULL;
	}
}

/*
 * report the feature as present: the kernel's global ipc table relocates
 * table->data to current->nsproxy->ipc_ns, so a fake namespace is served and
 * failing here would refuse to create namespaces that work fine
 */
bool droid_lkm_ipc_sysctls_setup(struct ipc_namespace *ns)
{
	return true;
}

void droid_lkm_ipc_sysctls_retire(struct ipc_namespace *ns)
{
}

#endif /* < 6.1 */

bool droid_lkm_ipc_sysctls_ok(void)
{
	return droid_lkm_ipc_sysctls_ready;
}


bool droid_lkm_sysctl_ready(void)
{
	return droid_lkm_ipc_sysctls_ready;
}

__nocfi noinline void droid_lkm_sysctl_setup_set(struct ctl_table_set *set,
				struct ctl_table_root *root,
				int (*is_seen)(struct ctl_table_set *))
{
	droid_lkm_setup_sysctl_set_fn(set, root, is_seen);
}

__nocfi noinline void droid_lkm_sysctl_retire_set(struct ctl_table_set *set)
{
	droid_lkm_retire_sysctl_set_fn(set);
}

__nocfi noinline struct ctl_table_header *droid_lkm_sysctl_register_table(
	struct ctl_table_set *set, const char *path, struct ctl_table *table,
	size_t table_size)
{
	return droid_lkm_register_sysctl_table_fn(set, path, table
						  DROID_LKM_REG_SYSCTL_PASS);
}

__nocfi noinline int droid_lkm_sysctl_in_egroup_p(kgid_t grp)
{
	if (!droid_lkm_in_egroup_p_fn)
		return 0;
	return droid_lkm_in_egroup_p_fn(grp);
}

int droid_lkm_ipc_sysctls_init(void)
{
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

	
	if (droid_lkm_ipc_sysctls_host_setup()) {
		droid_lkm_ipc_sysctls_ready = false;
		droid_lkm_warn("host ipc sysctls registration failed - falling back to the SYSVIPC_SYSCTL=n equivalent (ipc namespaces keep working, the /proc/sys/kernel/{shmmax,...} files will be missing)\n");
		return 0;
	}

	return 0;
}

void droid_lkm_ipc_sysctls_exit(void)
{
	if (!droid_lkm_ipc_sysctls_ready)
		return;

	droid_lkm_ipc_sysctls_host_teardown();
	droid_lkm_ipc_sysctls_ready = false;
}

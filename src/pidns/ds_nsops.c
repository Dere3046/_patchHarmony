// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/proc_ns.h>
#include <linux/ns_common.h>
#include <linux/refcount.h>
#include <linux/capability.h>
#include <linux/err.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>
#include <linux/sched.h>
#include <linux/user_namespace.h>
#include <linux/ipc_namespace.h>

#include "ds.h"
#include "ds_compat.h"
#include "ds_nsops.h"
#include "ds_pidns.h"

/*
 * owner() is called on whatever namespace the ops are attached to, so it has
 * to reach the concrete type. ns_common sits at a different offset in each one,
 * hence one accessor per type, picked where the ops table is built.
 */
static struct user_namespace *droid_lkm_pidns_ns_owner(struct ns_common *ns)
{
	return container_of(ns, struct pid_namespace, ns)->user_ns;
}

static struct user_namespace *droid_lkm_ipcns_ns_owner(struct ns_common *ns)
{
	return container_of(ns, struct ipc_namespace, ns)->user_ns;
}

struct droid_lkm_ns_ops_alias {
	struct proc_ns_operations ops;
	char name[20];
};

/*
 * a neutralized namespace outlives the module, so the ops it carries are read
 * long after unload: an nsfs inode opened from it still reaches install (setns)
 * and get_parent (NS_GET_PARENT), and both refuse. owner is the one callback
 * that may read the namespace, because the alias was built for the concrete
 * type of the leak and the leak keeps that object alive.
 */
static struct ns_common *droid_lkm_ns_neutral_get(struct task_struct *task)
{
	return NULL;
}

static void droid_lkm_ns_neutral_put(struct ns_common *ns)
{
}

static int droid_lkm_ns_neutral_install(struct nsset *nsset, struct ns_common *ns)
{
	return -EINVAL;
}

static struct ns_common *droid_lkm_ns_neutral_get_parent(struct ns_common *ns)
{
	return ERR_PTR(-EPERM);
}

const struct proc_ns_operations *droid_lkm_ns_ops_alias(const char *name,
						       int type)
{
	struct droid_lkm_ns_ops_alias *alias;

	alias = kzalloc(sizeof(*alias), GFP_KERNEL);
	if (!alias)
		return NULL;

	strscpy(alias->name, name, sizeof(alias->name));
	alias->ops.name = alias->name;
	alias->ops.real_ns_name = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
	alias->ops.type = type;
#endif
	alias->ops.get = droid_lkm_ns_neutral_get;
	alias->ops.put = droid_lkm_ns_neutral_put;
	alias->ops.install = droid_lkm_ns_neutral_install;
	alias->ops.get_parent = droid_lkm_ns_neutral_get_parent;

	switch (type) {
	case CLONE_NEWPID:
		alias->ops.owner = droid_lkm_pidns_ns_owner;
		break;
	case CLONE_NEWIPC:
		alias->ops.owner = droid_lkm_ipcns_ns_owner;
		break;
	default:
		/* an unknown type would make owner() read the wrong object */
		kfree(alias);
		return NULL;
	}

	return &alias->ops;
}

void droid_lkm_ns_neutralize(struct ns_common *ns, const struct proc_ns_operations *ops)
{
	if (!ns || !ops)
		return;

	/*
	 * 5.10 has no ns_common::count, and the counters of the concrete types
	 * are only decremented by put paths that do not exist without
	 * CONFIG_PID_NS and CONFIG_IPC_NS, so the pin is only needed from 5.15
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	refcount_set(&ns->__ns_ref, DROID_LKM_NS_LEAK_REFCOUNT);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	refcount_set(&ns->count, DROID_LKM_NS_LEAK_REFCOUNT);
#endif
	ns->ops = ops;
}

/*
 * the host placeholder stands in for init_pid_ns, so it is a whole struct
 * pid_namespace: owner() reaches the user namespace through container_of
 */
static struct pid_namespace *droid_lkm_host_pid_ns_object;

static struct ns_common *droid_lkm_host_pid_get(struct task_struct *task)
{

	if (!droid_lkm_host_pid_ns_object)
		return NULL;
	return &droid_lkm_host_pid_ns_object->ns;
}

static void droid_lkm_host_ns_put(struct ns_common *ns)
{

}

static int droid_lkm_host_pid_install(struct nsset *nsset, struct ns_common *ns)
{
	struct pid_namespace *active = task_active_pid_ns(current);

	if (!ns_capable(&init_user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(nsset->cred->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * pidns_install() only allows entering the current active namespace or a
	 * child of it, so a task that sits in a fake level L namespace must not
	 * be able to setns back onto the level 0 host entry
	 */
	if (active->level > 0 || active != &init_pid_ns)
		return -EINVAL;

	nsset->nsproxy->pid_ns_for_children = &init_pid_ns;
	return 0;
}

static struct ns_common *droid_lkm_host_ns_get_parent(struct ns_common *ns)
{
	return ERR_PTR(-EPERM);
}

struct droid_lkm_host_ns_ops {
	struct proc_ns_operations ops;
	char name[20];
	char real_name[20];
};

static struct proc_ns_operations *droid_lkm_host_ops_build(const char *name,
						    const char *real_name)
{
	struct droid_lkm_host_ns_ops *b;

	b = kzalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return NULL;

	strscpy(b->name, name, sizeof(b->name));
	b->ops.name = b->name;
	if (real_name) {
		strscpy(b->real_name, real_name, sizeof(b->real_name));
		b->ops.real_ns_name = b->real_name;
	} else {
		b->ops.real_ns_name = NULL;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
	b->ops.type = CLONE_NEWPID;
#endif
	b->ops.get = droid_lkm_host_pid_get;
	b->ops.put = droid_lkm_host_ns_put;
	b->ops.install = droid_lkm_host_pid_install;
	b->ops.owner = droid_lkm_pidns_ns_owner;
	b->ops.get_parent = droid_lkm_host_ns_get_parent;
	return &b->ops;
}

static struct proc_ns_operations *droid_lkm_host_pid_ops_built;
static struct proc_ns_operations *droid_lkm_host_pidfc_ops_built;

int droid_lkm_ns_ops_host_init(void)
{
	struct pid_namespace *ns;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return -ENOMEM;

	droid_lkm_host_pid_ops_built = droid_lkm_host_ops_build("pid", NULL);
	droid_lkm_host_pidfc_ops_built =
		droid_lkm_host_ops_build("pid_for_children", "pid");
	if (!droid_lkm_host_pid_ops_built || !droid_lkm_host_pidfc_ops_built) {
		kfree(droid_lkm_host_pid_ops_built);
		kfree(droid_lkm_host_pidfc_ops_built);
		droid_lkm_host_pid_ops_built = NULL;
		droid_lkm_host_pidfc_ops_built = NULL;
		kfree(ns);
		return -ENOMEM;
	}

	ns->user_ns = &init_user_ns;
	droid_lkm_ns_stash_clear(&ns->ns);
	ns->ns.inum = PROC_PID_INIT_INO;
	refcount_set(droid_lkm_pidns_refcount(ns), DROID_LKM_NS_LEAK_REFCOUNT);

	droid_lkm_ns_set_ops(&ns->ns, droid_lkm_host_pid_ops_built, CLONE_NEWPID);
	droid_lkm_host_pid_ns_object = ns;

	droid_lkm_info("host pid ns placeholder ready (inum=%u)\n", ns->ns.inum);
	return 0;
}

const struct proc_ns_operations *droid_lkm_ns_ops_host_pid(void)
{
	return droid_lkm_host_pid_ops_built;
}

const struct proc_ns_operations *droid_lkm_ns_ops_host_pid_for_children(void)
{
	return droid_lkm_host_pidfc_ops_built;
}

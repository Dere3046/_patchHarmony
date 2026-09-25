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

#include "ds.h"
#include "ds_nsops.h"

struct droid_lkm_ns_ops_alias {
	struct proc_ns_operations ops;
	char name[20];
};

/*
 * a neutralized namespace outlives the module, so its ops must never reach a
 * callback that reads fields of the namespace type it was built for. the
 * kernel keeps such an nsfs inode on the file it was opened from and can still
 * call install (setns), owner (NS_GET_OWNER_UID/NS_GET_NSTYPE) and get_parent
 * (NS_GET_PARENT) on it, so every callback is inert.
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

static struct user_namespace *droid_lkm_ns_neutral_owner(struct ns_common *ns)
{
	return &init_user_ns;
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
	alias->ops.type = type;
	alias->ops.get = droid_lkm_ns_neutral_get;
	alias->ops.put = droid_lkm_ns_neutral_put;
	alias->ops.install = droid_lkm_ns_neutral_install;
	alias->ops.owner = droid_lkm_ns_neutral_owner;
	alias->ops.get_parent = droid_lkm_ns_neutral_get_parent;

	return &alias->ops;
}

void droid_lkm_ns_neutralize(struct ns_common *ns, const struct proc_ns_operations *ops)
{
	if (!ns || !ops)
		return;



	refcount_set(&ns->count, DROID_LKM_NS_LEAK_REFCOUNT);
	ns->ops = ops;
}

static struct ns_common *droid_lkm_host_pid_ns_object;

static struct ns_common *droid_lkm_host_pid_get(struct task_struct *task)
{

	return droid_lkm_host_pid_ns_object;
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

static struct user_namespace *droid_lkm_host_ns_owner(struct ns_common *ns)
{
	return &init_user_ns;
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
	b->ops.type = CLONE_NEWPID;
	b->ops.get = droid_lkm_host_pid_get;
	b->ops.put = droid_lkm_host_ns_put;
	b->ops.install = droid_lkm_host_pid_install;
	b->ops.owner = droid_lkm_host_ns_owner;
	b->ops.get_parent = droid_lkm_host_ns_get_parent;
	return &b->ops;
}

static struct proc_ns_operations *droid_lkm_host_pid_ops_built;
static struct proc_ns_operations *droid_lkm_host_pidfc_ops_built;

int droid_lkm_ns_ops_host_init(void)
{
	struct ns_common *ns;

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

	ns->stashed = NULL;
	ns->inum = PROC_PID_INIT_INO;
	refcount_set(&ns->count, DROID_LKM_NS_LEAK_REFCOUNT);

	ns->ops = droid_lkm_host_pid_ops_built;
	droid_lkm_host_pid_ns_object = ns;

	droid_lkm_info("host pid ns placeholder ready (inum=%u)\n", ns->inum);
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

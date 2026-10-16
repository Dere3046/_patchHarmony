// SPDX-License-Identifier: GPL-2.0-only
/*
 * nsfs side of the fake user namespace: the /proc/<pid>/ns/user entry, with a
 * placeholder for tasks that stay in the initial namespace. The kind is
 * registered with the main module, which owns the ns directory lookup.
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/nsproxy.h>
#include <linux/proc_ns.h>
#include <linux/user_namespace.h>

#include "misc.h"
#include "misc_ksym.h"
#include "userns.h"
#include "ds_proc.h"

static struct ns_common *droid_lkm_userns_host_placeholder;
static struct proc_ns_operations *droid_lkm_userns_host_ops;

static struct ns_common *droid_lkm_userns_get(struct task_struct *task)
{
	struct droid_lkm_userns *u = droid_lkm_userns_of(task);

	if (u)
		return &u->uns.ns;

	return droid_lkm_userns_host_placeholder;
}

static void droid_lkm_userns_put(struct ns_common *ns)
{
	/* a fake namespace is owned by the module and lives until unload */
}

static int droid_lkm_userns_install(struct nsset *nsset, struct ns_common *ns)
{
	/* setns into a user namespace is not wired yet, keep the fd inert */
	return -EINVAL;
}

static struct user_namespace *droid_lkm_userns_owner(struct ns_common *ns)
{
	return container_of(ns, struct user_namespace, ns)->parent;
}

static struct ns_common *droid_lkm_userns_get_parent(struct ns_common *ns)
{
	struct user_namespace *uns = container_of(ns, struct user_namespace, ns);

	if (uns->parent)
		return &uns->parent->ns;

	return ERR_PTR(-EPERM);
}

static const struct proc_ns_operations droid_lkm_userns_ops_real = {
	.name = "user",
	.type = CLONE_NEWUSER,
	.get = droid_lkm_userns_get,
	.put = droid_lkm_userns_put,
	.install = droid_lkm_userns_install,
	.owner = droid_lkm_userns_owner,
	.get_parent = droid_lkm_userns_get_parent,
};

static struct ns_common *droid_lkm_userns_host_get(struct task_struct *task)
{
	return droid_lkm_userns_host_placeholder;
}

static void droid_lkm_userns_host_put(struct ns_common *ns)
{
}

static int droid_lkm_userns_host_install(struct nsset *nsset, struct ns_common *ns)
{
	return -EINVAL;
}

static struct user_namespace *droid_lkm_userns_host_owner(struct ns_common *ns)
{
	/* the initial namespace has no parent, NS_GET_USERNS reports EPERM */
	return NULL;
}

static struct ns_common *droid_lkm_userns_host_get_parent(struct ns_common *ns)
{
	return ERR_PTR(-EPERM);
}

const struct proc_ns_operations *droid_lkm_userns_ops(void)
{
	return &droid_lkm_userns_ops_real;
}

const struct proc_ns_operations *droid_lkm_userns_ops_for(struct task_struct *task)
{
	if (task && droid_lkm_userns_of(task))
		return &droid_lkm_userns_ops_real;

	return droid_lkm_userns_host_ops;
}

static const struct droid_lkm_ns_kind_reg droid_lkm_userns_kind = {
	.name = "user",
	.ops_for = droid_lkm_userns_ops_for,
};

int droid_lkm_userns_nsops_init(void)
{
	struct ns_common *ns;

	droid_lkm_userns_host_ops = kzalloc(sizeof(*droid_lkm_userns_host_ops),
					    GFP_KERNEL);
	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!droid_lkm_userns_host_ops || !ns) {
		kfree(droid_lkm_userns_host_ops);
		droid_lkm_userns_host_ops = NULL;
		kfree(ns);
		return -ENOMEM;
	}

	droid_lkm_userns_host_ops->name = "user";
	/*
	 * 6.18 moved the namespace type out of proc_ns_operations and onto the
	 * namespace itself, see droid_lkm_ns_set_ops() in core/ds.h; the whole
	 * tree guards this member the same way
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
	droid_lkm_userns_host_ops->type = CLONE_NEWUSER;
#endif
	droid_lkm_userns_host_ops->get = droid_lkm_userns_host_get;
	droid_lkm_userns_host_ops->put = droid_lkm_userns_host_put;
	droid_lkm_userns_host_ops->install = droid_lkm_userns_host_install;
	droid_lkm_userns_host_ops->owner = droid_lkm_userns_host_owner;
	droid_lkm_userns_host_ops->get_parent = droid_lkm_userns_host_get_parent;

	ns->inum = PROC_USER_INIT_INO;
	ns->ops = droid_lkm_userns_host_ops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	ns->ns_type = CLONE_NEWUSER;
#endif
	refcount_set(droid_lkm_userns_ns_ref(ns), 1);
	droid_lkm_userns_host_placeholder = ns;

	if (!droid_lkm_misc_ks.ns_kind_register)
		return -ENODEV;

	return droid_lkm_misc_ks.ns_kind_register(&droid_lkm_userns_kind);
}

void droid_lkm_userns_nsops_exit(void)
{
	if (droid_lkm_misc_ks.ns_kind_unregister)
		droid_lkm_misc_ks.ns_kind_unregister(&droid_lkm_userns_kind);

	/*
	 * droid_lkm_userns_host_ops and the placeholder namespace are deliberately
	 * not freed here. the kind's ops_for() hands that table to a proc inode's
	 * ns_ops, and the /proc/<pid>/ns lookup drops droid_lkm_ns_kinds_lock
	 * before it instantiates the entry, so an unregister that freed the table
	 * could race the instantiation that is already holding its address. both
	 * are a few hundred bytes held for the module's lifetime, and this module
	 * has no unload path to spend them at
	 */
}

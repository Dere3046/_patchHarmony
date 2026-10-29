// SPDX-License-Identifier: GPL-2.0-only
/*
 * nsfs side of the fake user namespace: the /proc/<pid>/ns/user entry, with a
 * placeholder for tasks that stay in the initial namespace. The kind is
 * registered with the main module, which owns the ns directory lookup.
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/capability.h>
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

static struct ns_common *droid_lkm_userns_nsops_get(struct task_struct *task)
{
	struct droid_lkm_userns *u = droid_lkm_userns_of(task);

	if (u) {
		droid_lkm_userns_get(u);
		return &u->uns.ns;
	}

	return droid_lkm_userns_host_placeholder;
}

static void droid_lkm_userns_nsops_put(struct ns_common *ns)
{
	struct user_namespace *uns;
	struct droid_lkm_userns *u;

	if (!ns || ns == droid_lkm_userns_host_placeholder)
		return;

	uns = container_of(ns, struct user_namespace, ns);
	u = droid_lkm_userns_from_ns(uns);
	if (u)
		droid_lkm_userns_put(u);
}

/*
 * setns(CLONE_NEWUSER): join a namespace this module made. The fd is an nsfs inode whose
 * ns_common is the fake namespace's own, so the work is the gate and the credential.
 *
 * Nothing is committed by the caller: prepare_nsset() built nsset->cred, a private copy,
 * and nsset_cred() is the kernel's own accessor for it. The commit is ours to do, because
 * commit_nsset() commits that credential only inside #ifdef CONFIG_USER_NS, and this module
 * refuses to run when that is enabled. Without it the install is applied and then thrown
 * away, and setns() reports success with nothing changed.
 */
static int droid_lkm_userns_install(struct nsset *nsset, struct ns_common *ns)
{
	struct user_namespace *uns = container_of(ns, struct user_namespace, ns);
	struct droid_lkm_userns *u;
	struct cred *cred;

	/* the initial namespace is not one of ours: climbing back out is refused, as upstream */
	if (uns == droid_lkm_userns_init_uns)
		return -EPERM;

	u = droid_lkm_userns_from_ns(uns);
	if (!u || !nsset)
		return -EINVAL;

	/* reentering the namespace a task already sits in would hand it its own authority again */
	if (current_cred()->user_ns == &u->uns)
		return -EINVAL;

	/* every thread of a group has to agree on its user namespace */
	if (!thread_group_empty(current))
		return -EINVAL;

	/*
	 * two gates, the way the pid namespace install does it: the caller has to hold
	 * CAP_SYS_ADMIN where it stands and over the namespace it is entering. The first one
	 * keeps this consistent with unshare(), which refuses a creator that has euid 0 but no
	 * capability over the initial namespace instead of handing it a namespace it would then
	 * hold full authority in. The second is answered by the security_capable chokepoint,
	 * which walks the fake chain: the initial namespace's root may join any of them and a
	 * task may join a namespace below its own, while an ancestor or a sibling is refused
	 */
	if (!ns_capable(current_cred()->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(&u->uns, CAP_SYS_ADMIN))
		return -EPERM;

	cred = nsset_cred(nsset);
	if (!cred)
		return -EINVAL;

	/*
	 * the joined task gets the same credential a creator gets. It has to: this kernel does
	 * no id translation, so authority inside a fake namespace is expressed entirely as the
	 * capability set plus the user_ns pointer, and the gate above already established that
	 * the caller holds CAP_SYS_ADMIN over the namespace it is entering
	 */
	droid_lkm_userns_install_cred(cred, u);

	/*
	 * commit_nsset() would commit this for us on a kernel with CONFIG_USER_NS, where it
	 * runs the same call; here that branch is compiled out. Taking the reference out of the
	 * set afterwards is what keeps put_nsset() from dropping a credential the task now owns
	 */
	droid_lkm_misc_ks.commit_creds(cred);
	nsset->cred = NULL;

	return 0;
}

static struct user_namespace *droid_lkm_userns_owner(struct ns_common *ns)
{
	return container_of(ns, struct user_namespace, ns)->parent;
}

static struct ns_common *droid_lkm_userns_get_parent(struct ns_common *ns)
{
	struct user_namespace *uns = container_of(ns, struct user_namespace, ns);

	if (uns->parent && uns->parent != droid_lkm_userns_init_uns)
		return &uns->parent->ns;

	return ERR_PTR(-EPERM);
}

static const struct proc_ns_operations droid_lkm_userns_ops_real = {
	.name = "user",
	.type = CLONE_NEWUSER,
	.get = droid_lkm_userns_nsops_get,
	.put = droid_lkm_userns_nsops_put,
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
	int ret;

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
	droid_lkm_userns_host_ops->type = CLONE_NEWUSER;
	droid_lkm_userns_host_ops->get = droid_lkm_userns_host_get;
	droid_lkm_userns_host_ops->put = droid_lkm_userns_host_put;
	droid_lkm_userns_host_ops->install = droid_lkm_userns_host_install;
	droid_lkm_userns_host_ops->owner = droid_lkm_userns_host_owner;
	droid_lkm_userns_host_ops->get_parent = droid_lkm_userns_host_get_parent;

	ns->inum = PROC_USER_INIT_INO;
	ns->ops = droid_lkm_userns_host_ops;
	refcount_set(droid_lkm_userns_ns_ref(ns), 1);
	droid_lkm_userns_host_placeholder = ns;

	if (!droid_lkm_misc_ks.ns_kind_register) {
		ret = -ENODEV;
		goto err_free;
	}

	ret = droid_lkm_misc_ks.ns_kind_register(&droid_lkm_userns_kind);
	if (ret)
		goto err_free;

	return 0;

err_free:
	kfree(droid_lkm_userns_host_placeholder);
	droid_lkm_userns_host_placeholder = NULL;
	kfree(droid_lkm_userns_host_ops);
	droid_lkm_userns_host_ops = NULL;
	return ret;
}

void droid_lkm_userns_nsops_exit(void)
{
	if (droid_lkm_misc_ks.ns_kind_unregister)
		droid_lkm_misc_ks.ns_kind_unregister(&droid_lkm_userns_kind);

	kfree(droid_lkm_userns_host_placeholder);
	droid_lkm_userns_host_placeholder = NULL;
	kfree(droid_lkm_userns_host_ops);
	droid_lkm_userns_host_ops = NULL;
}

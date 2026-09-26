// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2006 Pavel Emelyanov <xemul@openvz.org> OpenVZ, SWsoft Inc.
 */


#include <linux/module.h>
#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/ipc_namespace.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/proc_ns.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/user_namespace.h>
#include <linux/sched/task.h>
#include <linux/rhashtable.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "ds.h"
#include "ds_compat.h"
#include "ds_ksym.h"
#include "ds_ipcns.h"
#include "ipc_sysctl.h"
#include "ipc_mqueue_compat.h"
#include "ds_nsops.h"
#include "ipc_util.h"


struct droid_lkm_ipcns_entry {
	struct list_head node;
	struct ipc_namespace *ns;
};

static LIST_HEAD(droid_lkm_ipcns_list);
static DEFINE_SPINLOCK(droid_lkm_ipcns_lock);


static struct ipc_namespace *droid_lkm_ipcns_host;
static struct ipc_namespace *droid_lkm_ipcns_orig;


static const struct proc_ns_operations *droid_lkm_ipcns_neutral_ops;


static struct nsproxy *droid_lkm_init_nsproxy;

static struct ipc_namespace *to_ipc_ns(struct ns_common *ns)
{
	return container_of(ns, struct ipc_namespace, ns);
}

static void droid_lkm_ipcns_destroy_ids(struct ipc_namespace *ns)
{
	int i;

	if (ns->mq_mnt) {
		droid_lkm_retire_mq_sysctls(ns);
		droid_lkm_mq_clear_sbinfo(ns);
		droid_lkm_mq_put_mnt(ns);
		ns->mq_mnt = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(ns->ids); i++) {
		struct ipc_ids *ids = &ns->ids[i];

		rhashtable_destroy((struct rhashtable *)&ids->key_ht);
		idr_destroy(&ids->ipcs_idr);
	}
}

struct ipc_namespace *droid_lkm_ipcns_create(void)
{
	struct droid_lkm_ipcns_entry *e;
	struct ipc_namespace *ns;
	int err;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL_ACCOUNT);
	if (!ns)
		return ERR_PTR(-ENOMEM);

	err = droid_lkm_ks.proc_alloc_inum(&ns->ns.inum);
	if (err)
		goto out_free;

	ns->ns.ops = &droid_lkm_ipcns_ops;
	refcount_set(&ns->ns.count, 1);
	ns->user_ns = &init_user_ns;
	ns->ucounts = NULL;

	err = msg_init_ns(ns);
	if (err)
		goto out_inum;
	sem_init_ns(ns);
	shm_init_ns(ns);

	if (droid_lkm_mqueue_ready()) {
		err = droid_lkm_mq_init_ns(ns);
		if (err)
			goto out_ids;
		if (!droid_lkm_setup_mq_sysctls(ns)) {
			droid_lkm_warn("ipcns %p: mq sysctls required but registration failed\n",
				       ns);
			err = -ENOMEM;
			goto out_ids;
		}
	}

	if (droid_lkm_ipc_sysctls_ok() && !droid_lkm_ipc_sysctls_setup(ns)) {
		droid_lkm_warn("ipcns %p: sysctls required but registration failed, refusing to create a half namespace\n",
			       ns);
		err = -ENOMEM;
		goto out_ids;
	}

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		err = -ENOMEM;
		goto out_ids;
	}
	e->ns = ns;
	INIT_LIST_HEAD(&e->node);

	spin_lock_irq(&droid_lkm_ipcns_lock);
	list_add_tail(&e->node, &droid_lkm_ipcns_list);
	spin_unlock_irq(&droid_lkm_ipcns_lock);

	droid_lkm_dbg("ipcns %p created inum=%u\n", ns, ns->ns.inum);
	return ns;

out_ids:
	
	droid_lkm_ipc_sysctls_retire(ns);
	droid_lkm_ipcns_destroy_ids(ns);
out_inum:
	droid_lkm_ks.proc_free_inum(ns->ns.inum);
out_free:
	kfree(ns);
	return ERR_PTR(err);
}

bool droid_lkm_ipcns_is_ours(struct ipc_namespace *ns)
{
	struct droid_lkm_ipcns_entry *e;
	unsigned long flags;
	bool found = false;

	if (!ns)
		return false;

	spin_lock_irqsave(&droid_lkm_ipcns_lock, flags);
	list_for_each_entry(e, &droid_lkm_ipcns_list, node) {
		if (e->ns == ns) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&droid_lkm_ipcns_lock, flags);
	return found;
}


bool droid_lkm_ipcns_is_host(struct ipc_namespace *ns)
{
	return ns && ns == droid_lkm_ipcns_host;
}

bool droid_lkm_ipcns_task_is_host(struct task_struct *task)
{
	return droid_lkm_ipcns_is_host(droid_lkm_ipcns_task_ns(task));
}

struct ipc_namespace *droid_lkm_ipcns_get(struct ipc_namespace *ns)
{
	if (ns)
		refcount_inc(&ns->ns.count);
	return ns;
}

void droid_lkm_ipcns_put(struct ipc_namespace *ns)
{
	if (ns && !refcount_dec_not_one(&ns->ns.count))
		droid_lkm_dbg("ipcns %p refcount one not dropped\n", ns);
}

/*
 * proc_ns_operations: /proc/<pid>/ns/ipc
 */

struct ipc_namespace *droid_lkm_ipcns_task_ns(struct task_struct *task)
{
	struct ipc_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy)
		ns = nsproxy->ipc_ns;
	task_unlock(task);

	if (!ns)
		ns = droid_lkm_ipcns_host;
	return ns;
}

static struct ns_common *droid_lkm_ipcns_get_task(struct task_struct *task)
{
	struct ipc_namespace *ns = droid_lkm_ipcns_get(droid_lkm_ipcns_task_ns(task));

	return ns ? &ns->ns : NULL;
}

static void droid_lkm_ipcns_put_common(struct ns_common *ns)
{
	droid_lkm_ipcns_put(to_ipc_ns(ns));
}

static int droid_lkm_ipcns_install(struct nsset *nsset, struct ns_common *new)
{
	struct nsproxy *nsproxy = nsset->nsproxy;
	struct ipc_namespace *ns = to_ipc_ns(new);

	droid_lkm_dbg("setns(ipc): %s[%d] -> ns=%p\n", current->comm, current->pid, ns);

	if (!ns_capable(ns->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(nsset->cred->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	nsproxy->ipc_ns = ns;
	return 0;
}

static struct user_namespace *droid_lkm_ipcns_owner(struct ns_common *ns)
{
	return to_ipc_ns(ns)->user_ns;
}

const struct proc_ns_operations droid_lkm_ipcns_ops = {
	.name = "ipc",
	.type = CLONE_NEWIPC,
	.get = droid_lkm_ipcns_get_task,
	.put = droid_lkm_ipcns_put_common,
	.install = droid_lkm_ipcns_install,
	.owner = droid_lkm_ipcns_owner,
};

bool droid_lkm_ipcns_in_use(struct ipc_namespace *ns)
{
	int i;

	if (!ns)
		return false;

	for (i = 0; i < ARRAY_SIZE(ns->ids); i++) {
		struct ipc_ids *ids = &ns->ids[i];
		bool used;

		down_read(&ids->rwsem);
		used = ids->in_use > 0;
		up_read(&ids->rwsem);
		if (used)
			return true;
	}
	return false;
}

struct ipc_namespace *droid_lkm_ipcns_current(void)
{
	return droid_lkm_ipcns_task_ns(current);
}


static bool droid_lkm_ipcns_ids_used(struct ipc_namespace *ns)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ns->ids); i++) {
		if (READ_ONCE(ns->ids[i].in_use) > 0)
			return true;
	}
	return false;
}


bool droid_lkm_ipcns_busy(void)
{
	struct droid_lkm_ipcns_entry *e;
	unsigned long flags;
	bool host_used = false;
	bool created_used = false;

	spin_lock_irqsave(&droid_lkm_ipcns_lock, flags);
	list_for_each_entry(e, &droid_lkm_ipcns_list, node) {
		if (!droid_lkm_ipcns_ids_used(e->ns))
			continue;
		if (e->ns == droid_lkm_ipcns_host) {
			host_used = true;
			break;
		}
		created_used = true;
	}
	spin_unlock_irqrestore(&droid_lkm_ipcns_lock, flags);

	if (host_used)
		return true;

	return created_used && droid_lkm_any_task_ns_refs();
}

int droid_lkm_msgutil_init(void);
int droid_lkm_ipc_util_init(void);
void droid_lkm_ipc_proc_exit(void);

static struct droid_lkm_ipcns_entry droid_lkm_ipcns_init_entry;

int droid_lkm_ipcns_init(void)
{
	int ret;

	ret = droid_lkm_msgutil_init();
	if (ret) {
		droid_lkm_err("msgutil init failed: %d\n", ret);
		return ret;
	}
	ret = droid_lkm_ipc_util_init();
	if (ret) {
		droid_lkm_err("ipc util init failed: %d\n", ret);
		return ret;
	}

	droid_lkm_ipcns_neutral_ops = droid_lkm_ns_ops_alias("ipc", CLONE_NEWIPC);
	if (!droid_lkm_ipcns_neutral_ops) {
		droid_lkm_err("cannot build neutral ipc ns ops\n");
		return -ENOMEM;
	}

	shm_init_ns(&init_ipc_ns);

	droid_lkm_ipcns_host = &init_ipc_ns;
	
	droid_lkm_ipcns_host->ns.inum = PROC_IPC_INIT_INO;
	
	droid_lkm_ipcns_host->ns.ops = &droid_lkm_ipcns_ops;
	refcount_set(&droid_lkm_ipcns_host->ns.count, 1);
	droid_lkm_ipcns_host->user_ns = &init_user_ns;

	droid_lkm_ipcns_init_entry.ns = droid_lkm_ipcns_host;
	INIT_LIST_HEAD(&droid_lkm_ipcns_init_entry.node);
	spin_lock_irq(&droid_lkm_ipcns_lock);
	list_add_tail(&droid_lkm_ipcns_init_entry.node, &droid_lkm_ipcns_list);
	spin_unlock_irq(&droid_lkm_ipcns_lock);

	droid_lkm_init_nsproxy = (struct nsproxy *)droid_lkm_sym("init_nsproxy");
	if (!droid_lkm_init_nsproxy) {
		droid_lkm_err("init_nsproxy not found\n");
		droid_lkm_ipcns_exit();
		return -ENODATA;
	}
	droid_lkm_ipcns_orig = droid_lkm_init_nsproxy->ipc_ns;
	droid_lkm_init_nsproxy->ipc_ns = droid_lkm_ipcns_host;
	droid_lkm_info("host ipcns %p installed into init_nsproxy\n", droid_lkm_ipcns_host);

	droid_lkm_ipc_sysctls_init();
	return 0;
}


void droid_lkm_ipcns_exit(void)
{
	struct droid_lkm_ipcns_entry *e;
	unsigned long flags;
	int leaked = 0;

	droid_lkm_ipc_proc_exit();

	droid_lkm_ipc_sysctls_exit();

	if (droid_lkm_init_nsproxy && droid_lkm_ipcns_is_ours(droid_lkm_init_nsproxy->ipc_ns))
		droid_lkm_init_nsproxy->ipc_ns = droid_lkm_ipcns_orig;
	droid_lkm_reset_task_ns_refs();

	rcu_barrier();

	for (;;) {
		spin_lock_irqsave(&droid_lkm_ipcns_lock, flags);
		if (list_empty(&droid_lkm_ipcns_list)) {
			spin_unlock_irqrestore(&droid_lkm_ipcns_lock, flags);
			break;
		}
		e = list_first_entry(&droid_lkm_ipcns_list, struct droid_lkm_ipcns_entry, node);
		list_del_init(&e->node);
		spin_unlock_irqrestore(&droid_lkm_ipcns_lock, flags);

		droid_lkm_ipc_sysctls_retire(e->ns);

		if (e == &droid_lkm_ipcns_init_entry || droid_lkm_ipcns_is_host(e->ns)) {
			
			if (!droid_lkm_ipcns_in_use(e->ns))
				droid_lkm_ipcns_destroy_ids(e->ns);
			continue;
		}

		if (droid_lkm_ns_stashed(&e->ns->ns) ||
		    refcount_read(&e->ns->ns.count) > 1 ||
		    droid_lkm_ipcns_in_use(e->ns)) {
			
			droid_lkm_warn("ipcns %p neutralized+leaked (stashed=%p count=%d in_use=%d)\n",
				e->ns, droid_lkm_ns_stash_ptr(&e->ns->ns),
				refcount_read(&e->ns->ns.count),
				droid_lkm_ipcns_in_use(e->ns) ? 1 : 0);
			droid_lkm_ns_neutralize(&e->ns->ns, droid_lkm_ipcns_neutral_ops);
			leaked++;
			continue;
		}

		droid_lkm_ipcns_destroy_ids(e->ns);
		if (e->ns->ns.inum)
			droid_lkm_ks.proc_free_inum(e->ns->ns.inum);
		kfree(e->ns);
		kfree(e);
	}

	if (leaked)
		droid_lkm_warn("%d ipcns leaked on unload\n", leaked);
}

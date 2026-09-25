// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>

#include "ds.h"
#include "ds_ksym.h"
#include "ds_ipc_compat.h"

#define DROID_LKM_TASK_IPC_BITS 8

struct droid_lkm_task_ipc {
	struct hlist_node node;       
	struct list_head defer_node;  
	struct task_struct *task;
	void *undo_list;
	struct list_head shm_clist;
	struct ipc_namespace *exit_ns;
	bool defer_queued;
};

static DEFINE_HASHTABLE(droid_lkm_task_ipc_ht, DROID_LKM_TASK_IPC_BITS);
static DEFINE_SPINLOCK(droid_lkm_task_ipc_lock);

static LIST_HEAD(droid_lkm_deferred_list);
static DEFINE_SPINLOCK(droid_lkm_deferred_lock);


static struct droid_lkm_task_ipc *droid_lkm_task_ipc_find_locked(struct task_struct *tsk)
{
	struct droid_lkm_task_ipc *e;

	hash_for_each_possible(droid_lkm_task_ipc_ht, e, node, (unsigned long)tsk) {
		if (e->task == tsk)
			return e;
	}
	return NULL;
}

static struct droid_lkm_task_ipc *droid_lkm_task_ipc_get(struct task_struct *tsk, bool create)
{
	struct droid_lkm_task_ipc *e;
	struct droid_lkm_task_ipc *dup;

	spin_lock(&droid_lkm_task_ipc_lock);
	e = droid_lkm_task_ipc_find_locked(tsk);
	spin_unlock(&droid_lkm_task_ipc_lock);
	if (e || !create)
		return e;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return NULL;

	e->task = tsk;
	INIT_LIST_HEAD(&e->shm_clist);
	INIT_LIST_HEAD(&e->defer_node);

	spin_lock(&droid_lkm_task_ipc_lock);
	dup = droid_lkm_task_ipc_find_locked(tsk);
	if (dup) {
		spin_unlock(&droid_lkm_task_ipc_lock);
		kfree(e);
		return dup;
	}
	hash_add(droid_lkm_task_ipc_ht, &e->node, (unsigned long)tsk);
	spin_unlock(&droid_lkm_task_ipc_lock);

	return e;
}

unsigned long droid_lkm_do_mmap(struct file *file, unsigned long addr,
			 unsigned long len, unsigned long prot,
			 unsigned long flags, unsigned long vm_flags,
			 unsigned long pgoff, unsigned long *populate,
			 struct list_head *uf)
{
	if (!droid_lkm_ks.do_mmap)
		return -ENOSYS;
	return droid_lkm_ks.do_mmap(file, addr, len, prot, flags, vm_flags, pgoff,
			     populate, uf);
}

void *droid_lkm_task_undo_list(struct task_struct *tsk)
{
	struct droid_lkm_task_ipc *e;
	void *ul = NULL;

	spin_lock(&droid_lkm_task_ipc_lock);
	e = droid_lkm_task_ipc_find_locked(tsk);
	if (e)
		ul = e->undo_list;
	spin_unlock(&droid_lkm_task_ipc_lock);

	return ul;
}

void droid_lkm_task_undo_list_set(struct task_struct *tsk, void *ul)
{
	struct droid_lkm_task_ipc *e = droid_lkm_task_ipc_get(tsk, true);

	if (e) {
		spin_lock(&droid_lkm_task_ipc_lock);
		e->undo_list = ul;
		spin_unlock(&droid_lkm_task_ipc_lock);
	}
}


struct list_head *droid_lkm_task_shm_clist(struct task_struct *tsk)
{
	struct droid_lkm_task_ipc *e = droid_lkm_task_ipc_get(tsk, true);

	return e ? &e->shm_clist : NULL;
}


bool droid_lkm_task_ipc_exit_defer(struct task_struct *tsk)
{
	struct droid_lkm_task_ipc *e;
	bool queued = false;

	spin_lock(&droid_lkm_task_ipc_lock);
	e = droid_lkm_task_ipc_find_locked(tsk);
	if (e && !e->defer_queued) {
		e->defer_queued = true;
		e->exit_ns = tsk->nsproxy ? tsk->nsproxy->ipc_ns : NULL;
		queued = true;
	}
	spin_unlock(&droid_lkm_task_ipc_lock);

	if (!queued)
		return false;

	get_task_struct(tsk);
	spin_lock(&droid_lkm_deferred_lock);
	list_add_tail(&e->defer_node, &droid_lkm_deferred_list);
	spin_unlock(&droid_lkm_deferred_lock);

	return true;
}

bool droid_lkm_task_ipc_deferred_pending(void)
{
	bool pending;

	spin_lock(&droid_lkm_deferred_lock);
	pending = !list_empty(&droid_lkm_deferred_list);
	spin_unlock(&droid_lkm_deferred_lock);

	return pending;
}


void droid_lkm_task_ipc_deferred_run(void)
{
	for (;;) {
		struct droid_lkm_task_ipc *e;
		struct task_struct *tsk;

		spin_lock(&droid_lkm_deferred_lock);
		if (list_empty(&droid_lkm_deferred_list)) {
			spin_unlock(&droid_lkm_deferred_lock);
			break;
		}
		e = list_first_entry(&droid_lkm_deferred_list, struct droid_lkm_task_ipc,
				     defer_node);
		list_del_init(&e->defer_node);
		spin_unlock(&droid_lkm_deferred_lock);

		tsk = e->task;
		droid_lkm_exit_sem_ns(tsk, e->exit_ns);
		droid_lkm_exit_shm(tsk);

		spin_lock(&droid_lkm_task_ipc_lock);
		hash_del(&e->node);
		spin_unlock(&droid_lkm_task_ipc_lock);
		kfree(e);
		put_task_struct(tsk);
	}
}

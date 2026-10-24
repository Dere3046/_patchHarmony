// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/proc_ns.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/user_namespace.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/wait.h>
#include <uapi/linux/wait.h>
#include <linux/reboot.h>
#include <linux/signal.h>
#include <linux/sysctl.h>
#include <linux/namei.h>
#include <linux/path.h>

#include "ds.h"
#include "ds_shapes.h"
#include "ds_ksym.h"
#include "ds_ipcns.h"
#include "ds_nsops.h"

/* for_each_process_thread embeds &init_task, a trimmed image may not export it */
#define droid_lkm_for_each_process_thread(p, t)				\
	for ((p) = droid_lkm_ks.init_task;				\
	     ((p) = droid_lkm_next_task(p)) != droid_lkm_ks.init_task; )	\
		for ((t) = (p); (t); (t) = NULL)
#include "ds_pidns.h"
#include "ipc_sysctl.h"

bool droid_lkm_task_ipc_exit_defer(struct task_struct *tsk);
void droid_lkm_task_ipc_deferred_run(void);
bool droid_lkm_task_ipc_deferred_pending(void);

static struct kmem_cache *droid_lkm_pidns_cachep;

static const struct proc_ns_operations *droid_lkm_pidns_neutral_ops;

static struct task_struct *droid_lkm_pidns_retire_reaper;

static bool droid_lkm_skip_do_exit;
bool droid_lkm_pidns_skip_do_exit(void) { return droid_lkm_skip_do_exit; }
module_param_named(skip_do_exit, droid_lkm_skip_do_exit, bool, 0444);
MODULE_PARM_DESC(skip_do_exit, "do not register the do_exit kprobe (diagnostic only)");
static struct kmem_cache *droid_lkm_pid_cache[MAX_PID_NS_LEVEL];
static DEFINE_MUTEX(droid_lkm_pid_caches_mutex);

struct droid_lkm_pidns_entry {
	struct list_head node;
	struct list_head zap_node;
	struct pid_namespace *ns;
	bool zap_queued;
};

static LIST_HEAD(droid_lkm_pidns_list);
static DEFINE_SPINLOCK(droid_lkm_pidns_lock);

static struct task_struct *droid_lkm_reaper_task;

static LIST_HEAD(droid_lkm_zap_list);
static DEFINE_SPINLOCK(droid_lkm_zap_lock);
static DECLARE_WAIT_QUEUE_HEAD(droid_lkm_zap_wq);

static struct kprobe droid_lkm_kp_do_exit;

static struct pid_namespace *to_pid_ns(struct ns_common *ns)
{
	return (struct pid_namespace *)((char *)ns -
		droid_lkm_layout_off(DROID_LKM_F_PID_NS_NS));
}

static struct kmem_cache *droid_lkm_create_pid_cachep(unsigned int level)
{
	struct kmem_cache **pkc;
	struct kmem_cache *kc;
	char name[32];

	if (!level || level > MAX_PID_NS_LEVEL)
		return NULL;

	pkc = &droid_lkm_pid_cache[level - 1];
	kc = READ_ONCE(*pkc);
	if (kc)
		return kc;

	snprintf(name, sizeof(name), "droid_lkm_pid_%u", level + 1);
	mutex_lock(&droid_lkm_pid_caches_mutex);
	if (!*pkc)
		*pkc = kmem_cache_create(name,
					 /* struct_size_t() is 6.6 and later */
					 sizeof(struct pid) +
						 (level + 1) * sizeof(struct upid),
					 __alignof__(struct pid),
					 SLAB_HWCACHE_ALIGN | SLAB_ACCOUNT, NULL);
	mutex_unlock(&droid_lkm_pid_caches_mutex);

	return READ_ONCE(*pkc);
}

static struct droid_lkm_pidns_entry *droid_lkm_pidns_entry_of(struct pid_namespace *ns)
{
	struct droid_lkm_pidns_entry *e;

	if (!ns || ns == &init_pid_ns)
		return NULL;

	list_for_each_entry(e, &droid_lkm_pidns_list, node) {
		if (e->ns == ns)
			return e;
	}
	return NULL;
}

bool droid_lkm_pidns_is_ours(struct pid_namespace *ns)
{
	unsigned long flags;
	bool found;

	if (!ns || ns == &init_pid_ns)
		return false;

	spin_lock_irqsave(&droid_lkm_pidns_lock, flags);
	found = droid_lkm_pidns_entry_of(ns) != NULL;
	spin_unlock_irqrestore(&droid_lkm_pidns_lock, flags);
	return found;
}

__nocfi noinline struct pid_namespace *droid_lkm_pidns_create(struct pid_namespace *parent)
{
	struct droid_lkm_pidns_entry *e;
	struct pid_namespace *ns;
	unsigned int level;
	int err;

	if (!parent)
		parent = &init_pid_ns;

	level = droid_lkm_pidns_level(parent) + 1;
	if (level > MAX_PID_NS_LEVEL)
		return ERR_PTR(-ENOSPC);

	ns = kmem_cache_zalloc(droid_lkm_pidns_cachep, GFP_KERNEL);
	if (!ns)
		return ERR_PTR(-ENOMEM);

	idr_init(droid_lkm_pidns_idr(ns));

	droid_lkm_pidns_set_pid_cachep(ns, droid_lkm_create_pid_cachep(level));
	if (!droid_lkm_pidns_pid_cachep(ns)) {
		err = -ENOMEM;
		goto out_idr;
	}

	err = droid_lkm_ks.proc_alloc_inum(&droid_lkm_pidns_ns(ns)->inum);
	if (err)
		goto out_idr;

	droid_lkm_ns_set_ops(droid_lkm_pidns_ns(ns), &droid_lkm_pidns_ops,
			     CLONE_NEWPID);
	refcount_set(droid_lkm_pidns_refcount(ns), 1);
	droid_lkm_pidns_set_level(ns, level);
	droid_lkm_pidns_set_parent(ns, parent);
	/*
	 * Ownership follows the creating task's user namespace, so ns_capable()
	 * on this namespace is correct whether or not the userns module is
	 * loaded. No reference is taken: a fake namespace never reaches the
	 * kernel destroy path, and the userns module clears this field before it
	 * releases a user namespace.
	 */
	droid_lkm_pidns_set_user_ns(ns, droid_lkm_task_cred(current)->user_ns);
	droid_lkm_pidns_set_ucounts(ns, NULL);
	droid_lkm_pidns_set_pid_allocated(ns, PIDNS_ADDING);

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		err = -ENOMEM;
		goto out_inum;
	}
	e->ns = ns;
	INIT_LIST_HEAD(&e->node);
	INIT_LIST_HEAD(&e->zap_node);

	spin_lock_irq(&droid_lkm_pidns_lock);
	list_add_tail(&e->node, &droid_lkm_pidns_list);
	spin_unlock_irq(&droid_lkm_pidns_lock);

	droid_lkm_dbg("pidns %p created level=%u inum=%u\n", ns, level,
		      droid_lkm_pidns_ns(ns)->inum);
	return ns;

out_inum:
	droid_lkm_ks.proc_free_inum(droid_lkm_pidns_ns(ns)->inum);
out_idr:
	idr_destroy(droid_lkm_pidns_idr(ns));
	kmem_cache_free(droid_lkm_pidns_cachep, ns);
	return ERR_PTR(err);
}

struct pid_namespace *droid_lkm_pidns_create_for_current(void)
{
	struct pid_namespace *parent = NULL;

	if (droid_lkm_task_nsproxy(current))
		parent = droid_lkm_nsproxy_pid_ns_for_children(
				droid_lkm_task_nsproxy(current));
	if (!droid_lkm_pidns_is_ours(parent))
		parent = &init_pid_ns;

	return droid_lkm_pidns_create(parent);
}

struct pid_namespace *droid_lkm_pidns_get(struct pid_namespace *ns)
{
	if (ns && ns != &init_pid_ns)
		refcount_inc(droid_lkm_pidns_refcount(ns));
	return ns;
}

void droid_lkm_pidns_put(struct pid_namespace *ns)
{

	if (ns && ns != &init_pid_ns && !refcount_dec_not_one(droid_lkm_pidns_refcount(ns)))
		droid_lkm_dbg("pidns %p refcount one not dropped\n", ns);
}

struct pid_namespace *droid_lkm_pidns_task_for_children(struct task_struct *task)
{
	struct pid_namespace *ns = NULL;

	task_lock(task);
	if (droid_lkm_task_nsproxy(task))
		ns = droid_lkm_nsproxy_pid_ns_for_children(
				droid_lkm_task_nsproxy(task));
	task_unlock(task);

	return ns;
}

bool droid_lkm_pidns_busy(void)
{
	struct droid_lkm_pidns_entry *e;
	unsigned long flags;
	bool busy = false;

	spin_lock_irqsave(&droid_lkm_pidns_lock, flags);
	list_for_each_entry(e, &droid_lkm_pidns_list, node) {
		if (!idr_is_empty(droid_lkm_pidns_idr(e->ns))) {
			busy = true;
			break;
		}
	}
	spin_unlock_irqrestore(&droid_lkm_pidns_lock, flags);

	return busy;
}

bool droid_lkm_any_task_ns_refs(void)
{
	struct task_struct *p, *t;
	bool found = false;

	if (!droid_lkm_layout_trusted())
		return false;

	rcu_read_lock();
	droid_lkm_for_each_process_thread(p, t) {
		struct pid_namespace *pns = NULL;
		struct ipc_namespace *ins = NULL;

		task_lock(t);
		if (droid_lkm_task_nsproxy(t)) {
			pns = droid_lkm_nsproxy_pid_ns_for_children(
					droid_lkm_task_nsproxy(t));
			ins = droid_lkm_nsproxy_ipc_ns(droid_lkm_task_nsproxy(t));
		}
		task_unlock(t);

		if (droid_lkm_pidns_is_ours(pns) ||
		    (droid_lkm_ipcns_is_ours(ins) && !droid_lkm_ipcns_is_host(ins))) {
			found = true;
			goto out;
		}
	}
out:
	rcu_read_unlock();

	return found;
}

void droid_lkm_reset_task_ns_refs(void)
{
	struct task_struct *p, *t;

	if (!droid_lkm_layout_trusted())
		return;

	rcu_read_lock();
	droid_lkm_for_each_process_thread(p, t) {
		struct pid_namespace *pns = NULL;
		struct ipc_namespace *ins = NULL;

		task_lock(t);
		if (droid_lkm_task_nsproxy(t)) {
			pns = droid_lkm_nsproxy_pid_ns_for_children(
					droid_lkm_task_nsproxy(t));
			ins = droid_lkm_nsproxy_ipc_ns(droid_lkm_task_nsproxy(t));
		}
		task_unlock(t);

		if (!droid_lkm_pidns_is_ours(pns) && !droid_lkm_ipcns_is_ours(ins))
			continue;

		task_lock(t);
		if (droid_lkm_task_nsproxy(t)) {
			struct nsproxy *nsp = droid_lkm_task_nsproxy(t);

			if (droid_lkm_pidns_is_ours(
				    droid_lkm_nsproxy_pid_ns_for_children(nsp))) {
				droid_lkm_pidns_put(
					droid_lkm_nsproxy_pid_ns_for_children(nsp));
				droid_lkm_nsproxy_set_pid_ns_for_children(
					nsp, &init_pid_ns);
			}
			if (droid_lkm_ipcns_is_ours(droid_lkm_nsproxy_ipc_ns(nsp)))
				droid_lkm_nsproxy_set_ipc_ns(nsp, NULL);
		}
		task_unlock(t);
	}
	rcu_read_unlock();
}

static bool droid_lkm_keepalive_held;

void droid_lkm_keepalive_pin(void)
{
	if (droid_lkm_keepalive_held)
		return;

	if (try_module_get(THIS_MODULE)) {
		droid_lkm_keepalive_held = true;
		droid_lkm_dbg("keepalive: pinned, rmmod will -EBUSY\n");
	}
}

void droid_lkm_keepalive_sync(void)
{
	bool busy = droid_lkm_pidns_busy() || droid_lkm_ipcns_busy();

	if (busy) {
		droid_lkm_keepalive_pin();
		return;
	}

	if (droid_lkm_keepalive_held) {
		droid_lkm_keepalive_held = false;
		droid_lkm_dbg("keepalive: released\n");
		module_put(THIS_MODULE);
	}
}

void droid_lkm_keepalive_release(void)
{
	if (droid_lkm_keepalive_held) {
		droid_lkm_keepalive_held = false;
		module_put(THIS_MODULE);
	}
}

static struct ns_common *droid_lkm_pidns_get_active(struct task_struct *task)
{
	struct pid_namespace *ns;

	rcu_read_lock();
	ns = droid_lkm_pidns_get(task_active_pid_ns(task));
	rcu_read_unlock();

	return ns ? droid_lkm_pidns_ns(ns) : NULL;
}

static struct ns_common *droid_lkm_pidns_get_for_children(struct task_struct *task)
{
	struct pid_namespace *ns = NULL;

	task_lock(task);
	if (droid_lkm_task_nsproxy(task))
		ns = droid_lkm_pidns_get(droid_lkm_nsproxy_pid_ns_for_children(
					 droid_lkm_task_nsproxy(task)));
	task_unlock(task);

	/*
	 * upstream refuses to hand out a namespace whose reaper is gone
	 * (pidns_for_children_get), so a dead namespace reads as ENOENT
	 */
	if (ns && !droid_lkm_pidns_child_reaper(ns)) {
		droid_lkm_pidns_put(ns);
		ns = NULL;
	}

	return ns ? droid_lkm_pidns_ns(ns) : NULL;
}

static void droid_lkm_pidns_put_common(struct ns_common *ns)
{
	droid_lkm_pidns_put(to_pid_ns(ns));
}

static int droid_lkm_pidns_install(struct nsset *nsset, struct ns_common *ns)
{
	struct nsproxy *nsproxy = nsset->nsproxy;
	struct pid_namespace *active = task_active_pid_ns(current);
	struct pid_namespace *ancestor, *new = to_pid_ns(ns);

	droid_lkm_dbg("setns(pid): %s[%d] active=%p -> ns=%p level=%u\n",
	       droid_lkm_task_comm(current), droid_lkm_task_pid(current),
	       active, new, droid_lkm_pidns_level(new));

	if (!ns_capable(droid_lkm_pidns_user_ns(new), CAP_SYS_ADMIN) ||
	    !ns_capable(nsset->cred->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	if (droid_lkm_pidns_level(new) < droid_lkm_pidns_level(active))
		return -EINVAL;

	ancestor = new;
	while (droid_lkm_pidns_level(ancestor) >
	       droid_lkm_pidns_level(active))
		ancestor = droid_lkm_pidns_parent(ancestor);
	if (ancestor != active)
		return -EINVAL;

	/*
	 * nsproxy pointers borrow. get_pid_ns()/put_pid_ns() are no-ops in a
	 * CONFIG_PID_NS=n kernel, so a ref taken here would never be returned:
	 * every setns would add one and the ns could only be neutralized at
	 * unload. the real reference is the one the nsfs inode holds through
	 * droid_lkm_pidns_get_active()/put_common(), and live use is tracked
	 * through the ns idr plus droid_lkm_any_task_ns_refs().
	 */
	droid_lkm_nsproxy_set_pid_ns_for_children(nsproxy, new);
	return 0;
}

static struct ns_common *droid_lkm_pidns_get_parent(struct ns_common *ns)
{
	struct pid_namespace *active = task_active_pid_ns(current);
	struct pid_namespace *pid_ns, *p;

	pid_ns = p = droid_lkm_pidns_parent(to_pid_ns(ns));
	for (;;) {
		if (!p)
			return ERR_PTR(-EPERM);
		if (p == active)
			break;
		p = droid_lkm_pidns_parent(p);
	}

	pid_ns = droid_lkm_pidns_get(pid_ns);
	return pid_ns ? droid_lkm_pidns_ns(pid_ns) : ERR_PTR(-ENOMEM);
}

static struct user_namespace *droid_lkm_pidns_owner(struct ns_common *ns)
{
	return droid_lkm_pidns_user_ns(to_pid_ns(ns));
}

const struct proc_ns_operations droid_lkm_pidns_ops = {
	.name = "pid",
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
	.type = CLONE_NEWPID,
#endif
	.get = droid_lkm_pidns_get_active,
	.put = droid_lkm_pidns_put_common,
	.install = droid_lkm_pidns_install,
	.owner = droid_lkm_pidns_owner,
	.get_parent = droid_lkm_pidns_get_parent,
};

const struct proc_ns_operations droid_lkm_pidns_for_children_ops = {
	.name = "pid_for_children",
	.real_ns_name = "pid",
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
	.type = CLONE_NEWPID,
#endif
	.get = droid_lkm_pidns_get_for_children,
	.put = droid_lkm_pidns_put_common,
	.install = droid_lkm_pidns_install,
	.owner = droid_lkm_pidns_owner,
	.get_parent = droid_lkm_pidns_get_parent,
};

int droid_lkm_pidns_reboot(struct pid_namespace *ns, int cmd)
{
	switch (cmd) {
	case LINUX_REBOOT_CMD_RESTART2:
	case LINUX_REBOOT_CMD_RESTART:
		droid_lkm_pidns_set_reboot(ns, SIGHUP);
		break;
	case LINUX_REBOOT_CMD_POWER_OFF:
	case LINUX_REBOOT_CMD_HALT:
		droid_lkm_pidns_set_reboot(ns, SIGINT);
		break;
	default:
		return -EINVAL;
	}

	if (droid_lkm_pidns_child_reaper(ns)) {
		struct task_struct *reaper = droid_lkm_pidns_child_reaper(ns);

		send_sig(SIGKILL, reaper, 1);
		droid_lkm_info("pidns %p reboot=%d, reaper %d killed\n", ns,
			droid_lkm_pidns_reboot_value(ns), droid_lkm_task_pid(reaper));
	}
	return 0;
}

static __nocfi noinline void droid_lkm_pidns_do_zap(struct pid_namespace *ns)
{
	struct pid *pid;
	struct task_struct *task;
	int nr = 2;
	int rounds;
	long rc;

	droid_lkm_dbg("zap begin ns=%p allocated=%u\n", ns,
		      droid_lkm_pidns_pid_allocated(ns));

	droid_lkm_ks.disable_pid_allocation(ns);

	rcu_read_lock();
	/*
	 * a stock image may not export tasklist_lock. without it the walk stays
	 * rcu protected but loses the task list serialization, which is what the
	 * zapper can afford: it only sends SIGKILL to what it finds
	 */
	if (droid_lkm_ks_tasklist_lock)
		read_lock(droid_lkm_ks_tasklist_lock);
	idr_for_each_entry_continue(droid_lkm_pidns_idr(ns), pid, nr) {
		task = pid_task(pid, PIDTYPE_PID);
		if (task && !__fatal_signal_pending(task))
			droid_lkm_ks.group_send_sig_info(SIGKILL, SEND_SIG_PRIV, task,
						  PIDTYPE_MAX);
	}
	if (droid_lkm_ks_tasklist_lock)
		read_unlock(droid_lkm_ks_tasklist_lock);
	rcu_read_unlock();

	do {
		rc = droid_lkm_ks.kernel_wait4(-1, NULL, __WALL, NULL);
	} while (rc != -ECHILD);

	for (rounds = 0; rounds < 500; rounds++) {
		if (droid_lkm_pidns_pid_allocated(ns) == 0)
			break;
		msleep(10);
	}

	droid_lkm_dbg("zap done ns=%p allocated=%u after %d rounds\n", ns,
		droid_lkm_pidns_pid_allocated(ns), rounds);
}

static int droid_lkm_zap_fn(void *unused)
{
	struct droid_lkm_pidns_entry *e;

	{
		struct sighand_struct *sighand = droid_lkm_task_sighand(current);

		spin_lock_irq(&sighand->siglock);
		sighand->action[SIGCHLD - 1].sa.sa_handler = SIG_IGN;
		spin_unlock_irq(&sighand->siglock);
	}

	while (!kthread_should_stop()) {
		wait_event_interruptible_timeout(droid_lkm_zap_wq,
						 !list_empty(&droid_lkm_zap_list) ||
						 droid_lkm_task_ipc_deferred_pending() ||
						 kthread_should_stop(),
						 HZ / 4);

		for (;;) {
			unsigned long flags;

			spin_lock_irqsave(&droid_lkm_zap_lock, flags);
			if (list_empty(&droid_lkm_zap_list)) {
				spin_unlock_irqrestore(&droid_lkm_zap_lock, flags);
				break;
			}
			e = list_first_entry(&droid_lkm_zap_list,
					     struct droid_lkm_pidns_entry, zap_node);
			list_del_init(&e->zap_node);
			spin_unlock_irqrestore(&droid_lkm_zap_lock, flags);

			droid_lkm_pidns_do_zap(e->ns);
		}

		droid_lkm_task_ipc_deferred_run();

		droid_lkm_keepalive_sync();
	}

	return 0;
}

void droid_lkm_reaper_wake(void)
{
	wake_up_interruptible(&droid_lkm_zap_wq);
}

void droid_lkm_pidns_queue_zap(struct pid_namespace *ns)
{
	struct droid_lkm_pidns_entry *e;
	unsigned long flags;

	spin_lock_irqsave(&droid_lkm_pidns_lock, flags);
	e = droid_lkm_pidns_entry_of(ns);
	spin_unlock_irqrestore(&droid_lkm_pidns_lock, flags);
	if (!e)
		return;

	spin_lock_irqsave(&droid_lkm_zap_lock, flags);
	if (!e->zap_queued) {
		e->zap_queued = true;
		list_add_tail(&e->zap_node, &droid_lkm_zap_list);
	}
	spin_unlock_irqrestore(&droid_lkm_zap_lock, flags);

	wake_up_interruptible(&droid_lkm_zap_wq);
}

static __nocfi noinline int droid_lkm_do_exit_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct pid_namespace *ns = task_active_pid_ns(current);

	if (droid_lkm_task_ipc_exit_defer(current))
		droid_lkm_reaper_wake();

	if (!droid_lkm_pidns_is_ours(ns))
		return 0;

	droid_lkm_dbg("do_exit: %s[%d] ns=%p reaper=%d\n",
	       droid_lkm_task_comm(current), droid_lkm_task_pid(current), ns,
	       droid_lkm_pidns_child_reaper(ns) == current);

	if (droid_lkm_pidns_child_reaper(ns) != current)
		return 0;

	droid_lkm_ks.disable_pid_allocation(ns);

	if (droid_lkm_reaper_task)
		droid_lkm_pidns_set_child_reaper(ns, droid_lkm_reaper_task);

	if (droid_lkm_pidns_reboot_value(ns))
		droid_lkm_task_signal(current)->group_exit_code =
			droid_lkm_pidns_reboot_value(ns);

	droid_lkm_pidns_queue_zap(ns);
	return 0;
}

// fake pid ns, no isolation, just so the container gets PID 1
int droid_lkm_pidns_init(void)
{
	droid_lkm_pidns_cachep = kmem_cache_create("droid_lkm_pid_namespace",
					    droid_lkm_pidns_size(),
					    __alignof__(struct pid_namespace),
					    SLAB_HWCACHE_ALIGN | SLAB_ACCOUNT, NULL);
	if (!droid_lkm_pidns_cachep) {
		droid_lkm_err("cannot create droid_lkm_pid_namespace cache\n");
		return -ENOMEM;
	}

	droid_lkm_pidns_neutral_ops = droid_lkm_ns_ops_alias("pid", CLONE_NEWPID);
	if (!droid_lkm_pidns_neutral_ops) {
		droid_lkm_err("cannot build neutral pid ns ops\n");
		kmem_cache_destroy(droid_lkm_pidns_cachep);
		droid_lkm_pidns_cachep = NULL;
		return -ENOMEM;
	}

	/*
	 * free_pid() calls wake_up_process(ns->child_reaper) whenever a
	 * namespace drains, and leaked namespaces outlive this module, so a task
	 * that never goes away has to stay there. waking the idle task changes
	 * nothing, its state does not intersect TASK_NORMAL.
	 */
	droid_lkm_pidns_retire_reaper = (struct task_struct *)droid_lkm_sym("init_task");
	if (!droid_lkm_pidns_retire_reaper)
		droid_lkm_warn("init_task not found, leaked pidns keep a null reaper\n");

	droid_lkm_reaper_task = kthread_run(droid_lkm_zap_fn, NULL, "ds-zap");
	if (IS_ERR(droid_lkm_reaper_task)) {
		int err = PTR_ERR(droid_lkm_reaper_task);

		droid_lkm_reaper_task = NULL;
		kmem_cache_destroy(droid_lkm_pidns_cachep);
		droid_lkm_pidns_cachep = NULL;
		droid_lkm_err("cannot start ds-zap thread: %d\n", err);
		return err;
	}

	if (droid_lkm_skip_do_exit) {
		droid_lkm_warn("skip_do_exit=1: no do_exit kprobe (diagnostic only!)\n");
		goto out;
	}

	droid_lkm_kp_do_exit.symbol_name = "do_exit";
	droid_lkm_kp_do_exit.pre_handler = droid_lkm_do_exit_pre;
	if (register_kprobe(&droid_lkm_kp_do_exit)) {
		droid_lkm_err("cannot register do_exit kprobe\n");
		kthread_stop(droid_lkm_reaper_task);
		droid_lkm_reaper_task = NULL;
		kmem_cache_destroy(droid_lkm_pidns_cachep);
		droid_lkm_pidns_cachep = NULL;
		return -ENODATA;
	}

out:
	droid_lkm_info("pidns ready (reaper=%d)\n",
			 droid_lkm_task_pid(droid_lkm_reaper_task));
	return 0;
}

/*
 * the reaper kthread dies below, but a leaked namespace keeps allocating and
 * freeing pids after that, so every fake namespace gets a reaper that outlives
 * the module first
 */
static void droid_lkm_pidns_retire_reapers(void)
{
	struct droid_lkm_pidns_entry *e;
	unsigned long flags;

	if (!droid_lkm_pidns_retire_reaper)
		return;

	spin_lock_irqsave(&droid_lkm_pidns_lock, flags);
	list_for_each_entry(e, &droid_lkm_pidns_list, node)
		droid_lkm_pidns_set_child_reaper(e->ns,
						droid_lkm_pidns_retire_reaper);
	spin_unlock_irqrestore(&droid_lkm_pidns_lock, flags);
}

__nocfi noinline void droid_lkm_pidns_exit(void)
{
	struct droid_lkm_pidns_entry *e;
	unsigned long flags;
	int i, leaked = 0;
	bool caches_clean = true;

	if (!droid_lkm_pidns_cachep)
		return;

	unregister_kprobe(&droid_lkm_kp_do_exit);

	droid_lkm_pidns_retire_reapers();

	if (droid_lkm_reaper_task) {
		kthread_stop(droid_lkm_reaper_task);
		droid_lkm_reaper_task = NULL;
	}

	spin_lock_irqsave(&droid_lkm_zap_lock, flags);
	INIT_LIST_HEAD(&droid_lkm_zap_list);
	spin_unlock_irqrestore(&droid_lkm_zap_lock, flags);

	droid_lkm_reset_task_ns_refs();
	rcu_barrier();

	for (;;) {
		spin_lock_irqsave(&droid_lkm_pidns_lock, flags);
		if (list_empty(&droid_lkm_pidns_list)) {
			spin_unlock_irqrestore(&droid_lkm_pidns_lock, flags);
			break;
		}
		e = list_first_entry(&droid_lkm_pidns_list, struct droid_lkm_pidns_entry, node);
		list_del_init(&e->node);
		spin_unlock_irqrestore(&droid_lkm_pidns_lock, flags);

		if (droid_lkm_ns_stashed(droid_lkm_pidns_ns(e->ns)) ||
		    refcount_read(droid_lkm_pidns_refcount(e->ns)) > 1 ||
		    !idr_is_empty(droid_lkm_pidns_idr(e->ns))) {

			droid_lkm_warn("pidns %p neutralized+leaked (stashed=%p count=%d)\n",
				e->ns, droid_lkm_ns_stash_ptr(
					droid_lkm_pidns_ns(e->ns)),
				refcount_read(droid_lkm_pidns_refcount(e->ns)));
			droid_lkm_pidns_set_child_reaper(
				e->ns, droid_lkm_pidns_retire_reaper);
			droid_lkm_ns_neutralize(droid_lkm_pidns_ns(e->ns),
						droid_lkm_pidns_neutral_ops);
			leaked++;
			caches_clean = false;
			continue;
		}

		idr_destroy(droid_lkm_pidns_idr(e->ns));
		if (droid_lkm_pidns_ns(e->ns)->inum)
			droid_lkm_ks.proc_free_inum(
				droid_lkm_pidns_ns(e->ns)->inum);
		kmem_cache_free(droid_lkm_pidns_cachep, e->ns);
		kfree(e);
	}

	if (leaked) {
		droid_lkm_warn("%d pidns leaked on unload, pid caches kept\n", leaked);
		return;
	}

	for (i = 0; i < MAX_PID_NS_LEVEL; i++) {
		if (droid_lkm_pid_cache[i]) {
			kmem_cache_destroy(droid_lkm_pid_cache[i]);
			droid_lkm_pid_cache[i] = NULL;
		}
	}

	if (droid_lkm_pidns_cachep && caches_clean) {
		kmem_cache_destroy(droid_lkm_pidns_cachep);
		droid_lkm_pidns_cachep = NULL;
	}
}

/*
 * /proc/sys/kernel/ns_last_pid. the kernel registers it under
 * CONFIG_CHECKPOINT_RESTORE only, so a stock GKI kernel leaves a container no
 * way to choose the next pid. the table is global like upstream's, and the
 * handler resolves the namespace of the caller on every access.
 *
 * upstream reads the cursor through idr_get_cursor()/idr_set_cursor(), which are
 * static inline in include/linux/idr.h on every branch we build for, so there is
 * no symbol to resolve and the field is touched directly as those do.
 */

static int droid_lkm_pidns_sysctl_zero;
static int droid_lkm_pidns_pid_max = PID_MAX_LIMIT;

static int droid_lkm_pidns_ns_last_pid(DROID_LKM_CTL_TABLE *table, int write,
				       void *buffer, size_t *lenp, loff_t *ppos)
{
	struct pid_namespace *ns = task_active_pid_ns(current);
	struct ctl_table tmp = *table;
	int ret, next;

	if (!ns)
		return -ENOENT;

	/* the owner of the namespace authorizes the write, as upstream does */
	if (write && (!droid_lkm_pidns_user_ns(ns) ||
		      !ns_capable(droid_lkm_pidns_user_ns(ns),
				  CAP_CHECKPOINT_RESTORE)))
		return -EPERM;

	/*
	 * the cursor is volatile in a live namespace and writers are expected to
	 * serialize among themselves, so no lock is taken here
	 */
	next = READ_ONCE(droid_lkm_pidns_idr(ns)->idr_next) - 1;

	tmp.data = &next;
	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);
	if (!ret && write)
		WRITE_ONCE(droid_lkm_pidns_idr(ns)->idr_next, next + 1);

	return ret;
}

static struct ctl_table droid_lkm_pidns_sysctls[] = {
	{
		.procname	= "ns_last_pid",
		.maxlen		= sizeof(int),
		.mode		= 0666,	/* the handler checks the capability */
		.proc_handler	= droid_lkm_pidns_ns_last_pid,
		.extra1		= &droid_lkm_pidns_sysctl_zero,
		.extra2		= &droid_lkm_pidns_pid_max,
	},
	{ }	/* 5.10 finds the end of a table through a null procname */
};

static struct ctl_table_set droid_lkm_pidns_sysctl_set;
static struct ctl_table_header *droid_lkm_pidns_sysctl_header;
static struct ctl_table *droid_lkm_pidns_sysctl_tbl;

static struct ctl_table_set *droid_lkm_pidns_sysctl_lookup(struct ctl_table_root *root)
{
	return &droid_lkm_pidns_sysctl_set;
}

static int droid_lkm_pidns_sysctl_is_seen(struct ctl_table_set *set)
{
	return set == &droid_lkm_pidns_sysctl_set;
}

static struct ctl_table_root droid_lkm_pidns_sysctl_root = {
	.lookup		= droid_lkm_pidns_sysctl_lookup,
};

void droid_lkm_pidns_sysctl_init(void)
{
	struct path path;
	struct ctl_table *tbl;
	int *pid_max;

	if (!droid_lkm_sysctl_ready()) {
		droid_lkm_warn("no ns_last_pid: sysctl helpers unavailable\n");
		return;
	}

	/*
	 * a kernel built with CONFIG_CHECKPOINT_RESTORE already registered the
	 * file, and its handler resolves the same active namespace
	 */
	if (!kern_path("/proc/sys/kernel/ns_last_pid", 0, &path)) {
		path_put(&path);
		droid_lkm_dbg("ns_last_pid already provided by the kernel\n");
		return;
	}

	tbl = kmemdup(droid_lkm_pidns_sysctls, sizeof(droid_lkm_pidns_sysctls),
		      GFP_KERNEL);
	if (!tbl) {
		droid_lkm_warn("no ns_last_pid: out of memory\n");
		return;
	}

	/*
	 * the live pid_max is the upper bound upstream uses, and the static one
	 * only has to be permissive enough for a kernel without the symbol
	 */
	pid_max = (int *)droid_lkm_sym("pid_max");
	if (pid_max)
		tbl[0].extra2 = pid_max;

	droid_lkm_sysctl_setup_set(&droid_lkm_pidns_sysctl_set,
				   &droid_lkm_pidns_sysctl_root,
				   droid_lkm_pidns_sysctl_is_seen);
	droid_lkm_pidns_sysctl_header = droid_lkm_sysctl_register_table(
		&droid_lkm_pidns_sysctl_set, "kernel", tbl,
		ARRAY_SIZE(droid_lkm_pidns_sysctls) - 1);
	if (!droid_lkm_pidns_sysctl_header) {
		droid_lkm_sysctl_retire_set(&droid_lkm_pidns_sysctl_set);
		kfree(tbl);
		droid_lkm_warn("ns_last_pid registration failed\n");
		return;
	}
	droid_lkm_pidns_sysctl_tbl = tbl;

	droid_lkm_info("ns_last_pid registered\n");
}

void droid_lkm_pidns_sysctl_exit(void)
{
	if (!droid_lkm_pidns_sysctl_header)
		return;

	unregister_sysctl_table(droid_lkm_pidns_sysctl_header);
	droid_lkm_pidns_sysctl_header = NULL;
	droid_lkm_sysctl_retire_set(&droid_lkm_pidns_sysctl_set);
	kfree(droid_lkm_pidns_sysctl_tbl);
	droid_lkm_pidns_sysctl_tbl = NULL;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pid namespaces
 *
 * Authors:
 *    (C) 2007 Pavel Emelyanov <xemul@openvz.org>, OpenVZ, SWsoft Inc.
 *    (C) 2007 Sukadev Bhattiprolu <sukadev@us.ibm.com>, IBM
 *     Many thanks to Oleg Nesterov for comments and help
 *
 */

#define CONFIG_PID_NS 1
#include "names.h"
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/syscalls.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/acct.h>
#include <linux/slab.h>
#include <linux/proc_ns.h>
#include <linux/reboot.h>
#include <linux/export.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>
#include <linux/idr.h>
#include <uapi/linux/wait.h>
#include "native_compat.h"
#include "compat/pid_namespace.h"

extern rwlock_t *droid_lkm_ks_tasklist_lock;
int droid_native_ns_alloc_inum(struct ns_common *ns);
void droid_native_ns_free_inum(struct ns_common *ns);
void droid_native_disable_pid_allocation(struct pid_namespace *ns);
int droid_native_group_send_sig_info(int sig, struct kernel_siginfo *info,
				     struct task_struct *p, enum pid_type type);
long droid_native_kernel_wait4(pid_t upid, int __user *stat_addr, int options,
			       struct rusage __user *ru);
void droid_native_do_exit(long code);
struct ucounts *droid_native_inc_ucount(struct user_namespace *ns, kuid_t uid,
				       enum ucount_type type);
struct user_namespace *droid_native_get_user_ns(struct user_namespace *ns);

static DEFINE_MUTEX(pid_caches_mutex);
static struct kmem_cache *pid_ns_cachep;
/* Write once array, filled from the beginning. */
static struct kmem_cache *pid_cache[MAX_PID_NS_LEVEL];

/*
 * creates the kmem cache to allocate pids from.
 * @level: pid namespace level
 */

static struct kmem_cache *create_pid_cachep(unsigned int level)
{
	/* Level 0 is init_pid_ns.pid_cachep */
	struct kmem_cache **pkc = &pid_cache[level - 1];
	struct kmem_cache *kc;
	char name[4 + 10 + 1];
	unsigned int len;

	kc = READ_ONCE(*pkc);
	if (kc)
		return kc;

	snprintf(name, sizeof(name), "pid_%u", level + 1);
	len = sizeof(struct pid) + (level + 1) * sizeof(struct upid);
	mutex_lock(&pid_caches_mutex);
	/* Name collision forces to do allocation under mutex. */
	if (!*pkc)
		*pkc = kmem_cache_create(name, len, 0,
					 SLAB_HWCACHE_ALIGN | SLAB_ACCOUNT, NULL);
	mutex_unlock(&pid_caches_mutex);
	/* current can fail, but someone else can succeed. */
	return READ_ONCE(*pkc);
}

static struct ucounts *inc_pid_namespaces(struct user_namespace *ns)
{
	return droid_native_inc_ucount(ns, current_euid(),
				       UCOUNT_PID_NAMESPACES);
}

static void dec_pid_namespaces(struct ucounts *ucounts)
{
	if (ucounts)
		droid_native_dec_ucount(ucounts, UCOUNT_PID_NAMESPACES);
}

static struct pid_namespace *create_pid_namespace(struct user_namespace *user_ns,
	struct pid_namespace *parent_pid_ns)
{
	struct pid_namespace *ns;
	struct ucounts *ucounts;
	unsigned int level = parent_pid_ns->level + 1;
	int err;

	err = -EINVAL;
	if (!in_userns(parent_pid_ns->user_ns, user_ns))
		goto out;

	err = -ENOSPC;
	if (level > MAX_PID_NS_LEVEL)
		goto out;
	ucounts = inc_pid_namespaces(user_ns);
	if (!ucounts)
		goto out;

	err = -ENOMEM;
	ns = kmem_cache_zalloc(pid_ns_cachep, GFP_KERNEL);
	if (ns == NULL)
		goto out_dec;

	idr_init(&ns->idr);

	ns->pid_cachep = create_pid_cachep(level);
	if (ns->pid_cachep == NULL)
		goto out_free_idr;

	err = droid_native_pidns_common_init(ns);
	if (err)
		goto out_free_idr;

	droid_native_pidns_ref_init(ns);
	ns->level = level;
	ns->parent = droid_native_pid_namespace_get(parent_pid_ns);
	ns->user_ns = droid_native_get_user_ns(user_ns);
	ns->ucounts = ucounts;
	ns->pid_allocated = PIDNS_ADDING;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	ns->pid_max = PID_MAX_LIMIT;
	err = droid_native_pidns_sysctl_register(ns);
	if (err)
		goto out_free_inum;
#endif
	droid_native_pidns_memfd_init(ns, parent_pid_ns);
	droid_native_pidns_work_init(ns);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	err = droid_native_pidns_tree_add(ns);
	if (err)
		goto out_unregister;
#endif
	return ns;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
out_unregister:
	droid_native_pidns_sysctl_unregister(ns);
out_free_inum:
	droid_native_pidns_common_free(ns);
#endif
out_free_idr:
	idr_destroy(&ns->idr);
	kmem_cache_free(pid_ns_cachep, ns);
out_dec:
	dec_pid_namespaces(ucounts);
out:
	return ERR_PTR(err);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
void droid_native_pidns_destroy_work(struct work_struct *work)
{
	struct pid_namespace *ns = container_of(work, struct pid_namespace, work);

	droid_native_pid_namespace_destroy(ns);
}
#endif

struct pid_namespace *copy_pid_ns(droid_native_ns_flags_t flags,
	struct user_namespace *user_ns, struct pid_namespace *old_ns)
{
	if (droid_native_pidns_runtime_supported())
		return droid_native_real_copy_pid_ns(flags, user_ns, old_ns);
	if (!(flags & CLONE_NEWPID))
		return droid_native_pid_namespace_get(old_ns);
	if (task_active_pid_ns(current) != old_ns)
		return ERR_PTR(-EINVAL);
	return create_pid_namespace(user_ns, old_ns);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
void droid_native_pidns_kref_release(struct kref *kref)
{
	struct pid_namespace *ns = container_of(kref, struct pid_namespace, kref);

	droid_native_pid_namespace_destroy(ns);
}
#endif

void put_pid_ns(struct pid_namespace *ns)
{
	if (droid_native_pidns_runtime_supported()) {
		if (ns && ns != &init_pid_ns)
			droid_native_real_put_pid_ns(ns);
		return;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	if (ns && ns != &init_pid_ns &&
	    droid_native_pid_namespace_put_ref(ns))
		schedule_work(&ns->work);
#else
	struct pid_namespace *parent;

	while (ns != &init_pid_ns) {
		parent = ns->parent;
		if (!droid_native_pid_namespace_put_ref(ns))
			break;
		droid_native_pid_namespace_destroy(ns);
		ns = parent;
	}
#endif
}
EXPORT_SYMBOL_GPL(put_pid_ns);

void zap_pid_ns_processes(struct pid_namespace *pid_ns)
{
	int nr;
	int rc;
	struct task_struct *task, *me = current;
	int init_pids = thread_group_leader(me) ? 1 : 2;
	struct pid *pid;

	/* Don't allow any more processes into the pid namespace */
	droid_native_disable_pid_allocation(pid_ns);

	/*
	 * Ignore SIGCHLD causing any terminated children to autoreap.
	 * This speeds up the namespace shutdown, plus see the comment
	 * below.
	 */
	spin_lock_irq(&me->sighand->siglock);
	me->sighand->action[SIGCHLD - 1].sa.sa_handler = SIG_IGN;
	spin_unlock_irq(&me->sighand->siglock);

	/*
	 * The last thread in the cgroup-init thread group is terminating.
	 * Find remaining pid_ts in the namespace, signal and wait for them
	 * to exit.
	 *
	 * Note:  This signals each threads in the namespace - even those that
	 * 	  belong to the same thread group, To avoid this, we would have
	 * 	  to walk the entire tasklist looking a processes in this
	 * 	  namespace, but that could be unnecessarily expensive if the
	 * 	  pid namespace has just a few processes. Or we need to
	 * 	  maintain a tasklist for each pid namespace.
	 *
	 */
	rcu_read_lock();
	read_lock(droid_lkm_ks_tasklist_lock);
	nr = 2;
	idr_for_each_entry_continue(&pid_ns->idr, pid, nr) {
		task = pid_task(pid, PIDTYPE_PID);
		if (task && !__fatal_signal_pending(task))
			droid_native_group_send_sig_info(SIGKILL, SEND_SIG_PRIV, task, PIDTYPE_MAX);
	}
	read_unlock(droid_lkm_ks_tasklist_lock);
	rcu_read_unlock();

	/*
	 * Reap the EXIT_ZOMBIE children we had before we ignored SIGCHLD.
	 * kernel_wait4() will also block until our children traced from the
	 * parent namespace are detached and become EXIT_DEAD.
	 */
	do {
		clear_thread_flag(TIF_SIGPENDING);
		clear_thread_flag(TIF_NOTIFY_SIGNAL);
		rc = droid_native_kernel_wait4(-1, NULL, __WALL, NULL);
	} while (rc != -ECHILD);

	/*
	 * kernel_wait4() misses EXIT_DEAD children, and EXIT_ZOMBIE
	 * process whose parents processes are outside of the pid
	 * namespace.  Such processes are created with setns()+fork().
	 *
	 * If those EXIT_ZOMBIE processes are not reaped by their
	 * parents before their parents exit, they will be reparented
	 * to pid_ns->child_reaper.  Thus pidns->child_reaper needs to
	 * stay valid until they all go away.
	 *
	 * The code relies on the pid_ns->child_reaper ignoring
	 * SIGCHILD to cause those EXIT_ZOMBIE processes to be
	 * autoreaped if reparented.
	 *
	 * Semantically it is also desirable to wait for EXIT_ZOMBIE
	 * processes before allowing the child_reaper to be reaped, as
	 * that gives the invariant that when the init process of a
	 * pid namespace is reaped all of the processes in the pid
	 * namespace are gone.
	 *
	 * Once all of the other tasks are gone from the pid_namespace
	 * free_pid() will awaken this task.
	 */
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (pid_ns->pid_allocated == init_pids)
			break;
		schedule();
	}
	__set_current_state(TASK_RUNNING);

	if (pid_ns->reboot)
		current->signal->group_exit_code = pid_ns->reboot;

	acct_exit_ns(pid_ns);
	return;
}

#ifdef CONFIG_CHECKPOINT_RESTORE
static int pid_ns_ctl_handler(const struct ctl_table *table, int write,
		void *buffer, size_t *lenp, loff_t *ppos)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(current);
	struct ctl_table tmp = *table;
	int ret, next;

	if (write && !checkpoint_restore_ns_capable(pid_ns->user_ns))
		return -EPERM;

	next = idr_get_cursor(&pid_ns->idr) - 1;

	tmp.data = &next;
	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);
	if (!ret && write)
		idr_set_cursor(&pid_ns->idr, next + 1);

	return ret;
}

extern int pid_max;
static struct ctl_table pid_ns_ctl_table[] = {
	{
		.procname = "ns_last_pid",
		.maxlen = sizeof(int),
		.mode = 0666, /* permissions are checked in the handler */
		.proc_handler = pid_ns_ctl_handler,
		.extra1 = SYSCTL_ZERO,
		.extra2 = &pid_max,
	},
};
#endif	/* CONFIG_CHECKPOINT_RESTORE */

int reboot_pid_ns(struct pid_namespace *pid_ns, int cmd)
{
	if (pid_ns == &init_pid_ns)
		return 0;

	switch (cmd) {
	case LINUX_REBOOT_CMD_RESTART2:
	case LINUX_REBOOT_CMD_RESTART:
		pid_ns->reboot = SIGHUP;
		break;

	case LINUX_REBOOT_CMD_POWER_OFF:
	case LINUX_REBOOT_CMD_HALT:
		pid_ns->reboot = SIGINT;
		break;
	default:
		return -EINVAL;
	}

	read_lock(droid_lkm_ks_tasklist_lock);
	send_sig(SIGKILL, pid_ns->child_reaper, 1);
	read_unlock(droid_lkm_ks_tasklist_lock);

	droid_native_do_exit(0);

	/* Not reached */
	return 0;
}

static struct ns_common *pidns_get(struct task_struct *task)
{
	struct pid_namespace *ns;

	rcu_read_lock();
	ns = task_active_pid_ns(task);
	if (ns)
		droid_native_pid_namespace_get(ns);
	rcu_read_unlock();

	return ns ? &ns->ns : NULL;
}

static struct ns_common *pidns_for_children_get(struct task_struct *task)
{
	struct pid_namespace *ns = NULL;

	task_lock(task);
	if (task->nsproxy) {
		ns = task->nsproxy->pid_ns_for_children;
		droid_native_pid_namespace_get(ns);
	}
	task_unlock(task);

	if (ns) {
		read_lock(droid_lkm_ks_tasklist_lock);
		if (!ns->child_reaper) {
			put_pid_ns(ns);
			ns = NULL;
		}
		read_unlock(droid_lkm_ks_tasklist_lock);
	}

	return ns ? &ns->ns : NULL;
}

static void pidns_put(struct ns_common *ns)
{
	put_pid_ns(droid_native_to_pid_ns(ns));
}

static int pidns_install(struct nsset *nsset, struct ns_common *ns)
{
	struct nsproxy *nsproxy = nsset->nsproxy;
	struct pid_namespace *active = task_active_pid_ns(current);
	struct pid_namespace *ancestor, *new = droid_native_to_pid_ns(ns);

	if (!ns_capable(new->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(nsset->cred->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * Only allow entering the current active pid namespace
	 * or a child of the current active pid namespace.
	 *
	 * This is required for fork to return a usable pid value and
	 * this maintains the property that processes and their
	 * children can not escape their current pid namespace.
	 */
	if (new->level < active->level)
		return -EINVAL;

	ancestor = new;
	while (ancestor->level > active->level)
		ancestor = ancestor->parent;
	if (ancestor != active)
		return -EINVAL;

	put_pid_ns(nsproxy->pid_ns_for_children);
	nsproxy->pid_ns_for_children = droid_native_pid_namespace_get(new);
	return 0;
}

static struct ns_common *pidns_get_parent(struct ns_common *ns)
{
	struct pid_namespace *active = task_active_pid_ns(current);
	struct pid_namespace *pid_ns, *p;

	/* See if the parent is in the current namespace */
	pid_ns = p = droid_native_to_pid_ns(ns)->parent;
	for (;;) {
		if (!p)
			return ERR_PTR(-EPERM);
		if (p == active)
			break;
		p = p->parent;
	}

	return &droid_native_pid_namespace_get(pid_ns)->ns;
}

static struct user_namespace *pidns_owner(struct ns_common *ns)
{
	return droid_native_to_pid_ns(ns)->user_ns;
}

const struct proc_ns_operations pidns_operations = {
	.name		= "pid",
	DROID_NATIVE_PIDNS_TYPE
	.get		= pidns_get,
	.put		= pidns_put,
	.install	= pidns_install,
	.owner		= pidns_owner,
	.get_parent	= pidns_get_parent,
};

const struct proc_ns_operations pidns_for_children_operations = {
	.name		= "pid_for_children",
	.real_ns_name	= "pid",
	DROID_NATIVE_PIDNS_TYPE
	.get		= pidns_for_children_get,
	.put		= pidns_put,
	.install	= pidns_install,
	.owner		= pidns_owner,
	.get_parent	= pidns_get_parent,
};

int droid_native_pidns_init(void)
{
	(void)droid_native_pidns_runtime_supported();
	pid_ns_cachep = kmem_cache_create("droid_native_pid_namespace",
					 sizeof(struct pid_namespace), 0,
					 SLAB_ACCOUNT, NULL);
	if (!pid_ns_cachep)
		return -ENOMEM;

	return 0;
}

void droid_native_pidns_exit(void)
{
}

struct pid_namespace *droid_native_create_pid_namespace(
	struct user_namespace *user_ns, struct pid_namespace *parent)
{
	if (droid_native_pidns_runtime_supported())
		return droid_native_real_copy_pid_ns(CLONE_NEWPID, user_ns,
						     parent);
	return create_pid_namespace(user_ns, parent);
}

struct pid_namespace *droid_native_get_pid_ns(struct pid_namespace *ns)
{
	return droid_native_pid_namespace_get(ns);
}

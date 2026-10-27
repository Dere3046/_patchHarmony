// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/user_namespace.h>

#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "misc.h"
#include "misc_layout.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "userns.h"

#define DROID_LKM_USERNS_MAX 1024

static LIST_HEAD(droid_lkm_userns_list);
static DEFINE_SPINLOCK(droid_lkm_userns_lock);
static unsigned int droid_lkm_userns_count;
static bool droid_lkm_userns_module_pinned;

static inline bool droid_lkm_refcount_dec_and_lock_irqsave(
	refcount_t *r, spinlock_t *lock, unsigned long *flags)
{
	if (refcount_dec_not_one(r))
		return false;

	spin_lock_irqsave(lock, *flags);
	if (!refcount_dec_and_test(r)) {
		spin_unlock_irqrestore(lock, *flags);
		return false;
	}
	return true;
}

static void droid_lkm_userns_free_work(struct work_struct *work)
{
	struct droid_lkm_userns *u = container_of(work, struct droid_lkm_userns, work);
	struct droid_lkm_userns *parent = NULL;

	if (u->uns.parent && u->uns.parent != droid_lkm_userns_init_uns)
		parent = droid_lkm_userns_from_ns(u->uns.parent);

	droid_lkm_misc_ks.proc_free_inum(u->uns.ns.inum);
	kfree(u);

	if (parent)
		droid_lkm_userns_put(parent);
}

struct droid_lkm_userns *droid_lkm_userns_get(struct droid_lkm_userns *u)
{
	if (u)
		refcount_inc(&u->count);
	return u;
}

void droid_lkm_userns_put(struct droid_lkm_userns *u)
{
	unsigned long flags;

	if (!u)
		return;

	if (droid_lkm_refcount_dec_and_lock_irqsave(&u->count, &droid_lkm_userns_lock, &flags)) {
		list_del_init(&u->link);
		if (droid_lkm_userns_count > 0)
			droid_lkm_userns_count--;
		spin_unlock_irqrestore(&droid_lkm_userns_lock, flags);

		schedule_work(&u->work);
	}
}

bool droid_lkm_userns_is_ours(const struct user_namespace *uns)
{
	if (!uns || uns == droid_lkm_userns_init_uns)
		return false;

	return uns->ns.ops == droid_lkm_userns_ops();
}

struct droid_lkm_userns *droid_lkm_userns_from_ns(struct user_namespace *uns)
{
	if (!droid_lkm_userns_is_ours(uns))
		return NULL;

	return container_of(uns, struct droid_lkm_userns, uns);
}

struct droid_lkm_userns *droid_lkm_userns_of(struct task_struct *task)
{
	const struct cred *cred;

	if (!task)
		return NULL;

	cred = droid_lkm_misc_task_cred(task);
	if (!cred)
		return NULL;

	return droid_lkm_userns_from_ns(cred->user_ns);
}

/*
 * the creation-time privilege anchor. a namespace created by a process that held
 * CAP_SETUID and CAP_SETGID over the initial namespace may map anything; a nested one
 * only when its parent was privileged and the creator is the host's root. nothing is
 * re-derived later, so a process that drops its capabilities keeps what it created with
 */
static bool droid_lkm_userns_creator_privileged(const struct droid_lkm_userns *parent)
{
	if (!parent)
		return ns_capable(&init_user_ns, CAP_SETUID) &&
		       ns_capable(&init_user_ns, CAP_SETGID);

	return parent->privileged && uid_eq(current_euid(), GLOBAL_ROOT_UID);
}

/*
 * mirrors create_user_ns(): level, parent, owner, group, flags, empty maps, only the
 * work_struct stays zeroed
 */
struct droid_lkm_userns *droid_lkm_userns_create(struct droid_lkm_userns *parent)
{
	struct droid_lkm_userns *u;
	unsigned int inum;
	int ret, i;

	if (parent && parent->level > 32)
		return ERR_PTR(-EINVAL);

	{
		unsigned long flags;
		spin_lock_irqsave(&droid_lkm_userns_lock, flags);
		if (droid_lkm_userns_count >= DROID_LKM_USERNS_MAX) {
			spin_unlock_irqrestore(&droid_lkm_userns_lock, flags);
			return ERR_PTR(-ENOSPC);
		}
		spin_unlock_irqrestore(&droid_lkm_userns_lock, flags);
	}

	u = kzalloc(sizeof(*u), GFP_KERNEL);
	if (!u)
		return ERR_PTR(-ENOMEM);

	ret = droid_lkm_misc_ks.proc_alloc_inum(&inum);
	if (ret) {
		kfree(u);
		return ERR_PTR(ret);
	}

	u->level = parent ? parent->level + 1 : 1;
	u->uns.parent = parent ? &parent->uns : &init_user_ns;
	u->uns.level = u->level;
	u->uns.owner = current_euid();
	u->uns.group = current_egid();
	u->uns.flags = (parent ? parent->uns.flags : USERNS_INIT_FLAGS);
	u->uns.parent_could_setfcap = cap_raised(current_cred()->cap_effective,
						 CAP_SETFCAP);
	u->uns.ns.inum = inum;
	u->uns.ns.ops = droid_lkm_userns_ops();
	refcount_set(droid_lkm_userns_ns_ref(&u->uns.ns), 1);
	u->uns.ucounts = NULL;
	/*
	 * inc_ucount() walks ns->ucount_max[], which zero would turn into ENOSPC for every
	 * copy_utsname(), copy_mnt_ns() and copy_net_ns(), so mirror create_user_ns(). ucounts stays
	 * NULL, nothing is accounted per user
	 */
	for (i = 0; i < UCOUNT_COUNTS; i++)
		u->uns.ucount_max[i] = INT_MAX;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	for (i = 0; i < UCOUNT_RLIMIT_COUNTS; i++)
		u->uns.rlimit_max[i] = LONG_MAX;
#endif
	INIT_LIST_HEAD(&u->link);
	refcount_set(&u->count, 1);
	INIT_WORK(&u->work, droid_lkm_userns_free_work);
	if (parent)
		droid_lkm_userns_get(parent);

	/*
	 * the default map. a privileged creator gets the identity, a confined one gets
	 * container id 0 mapped to its own host id and nothing else; a creator whose own
	 * id is the host's root is refused rather than handed a root mapping
	 */
	u->privileged = droid_lkm_userns_creator_privileged(parent);
	if (u->privileged)
		droid_lkm_userns_map_identity(u);
	else if (!droid_lkm_userns_map_confine(u)) {
		if (parent)
			droid_lkm_userns_put(parent);
		droid_lkm_misc_ks.proc_free_inum(inum);
		kfree(u);
		return ERR_PTR(-EPERM);
	}

	{
		unsigned long flags;
		spin_lock_irqsave(&droid_lkm_userns_lock, flags);
		list_add_tail(&u->link, &droid_lkm_userns_list);
		droid_lkm_userns_count++;
		spin_unlock_irqrestore(&droid_lkm_userns_lock, flags);
	}

	droid_lkm_misc_dbg("userns create %p level=%u inum=%u owner=%u privileged=%u\n", u,
		u->level, inum, __kuid_val(u->uns.owner), u->privileged);

	return u;
}

void droid_lkm_userns_destroy(struct droid_lkm_userns *u)
{
	droid_lkm_userns_put(u);
}

int droid_lkm_userns_init(void)
{
	char detail[32];
	int ret;

	if (IS_ENABLED(CONFIG_USER_NS)) {
		droid_lkm_misc_report("userns", "unsupported", "kernel has CONFIG_USER_NS");
		return -EOPNOTSUPP;
	}

	ret = droid_lkm_userns_nsops_init();
	if (ret) {
		droid_lkm_misc_report("userns", "unsupported", "namespace entry failed");
		return ret;
	}

	ret = droid_lkm_userns_owner_init();
	if (ret) {
		droid_lkm_misc_report("userns", "unsupported", "owner symbols missing");
		droid_lkm_userns_nsops_exit();
		return ret;
	}

	ret = droid_lkm_userns_hooks_init();
	if (ret) {
		droid_lkm_misc_report("userns", "unsupported", "a hook failed");
		droid_lkm_userns_owner_exit();
		droid_lkm_userns_nsops_exit();
		return ret;
	}

	/*
	 * every credential in a fake namespace points into this module, and the proc
	 * inodes carry this module's f_op, but neither holds a reference the kernel
	 * would drop for us: with CONFIG_USER_NS off put_user_ns() is a stub. pin the
	 * module here, before the first object exists, and let rmmod fail while the
	 * feature is up. that is the only lifecycle that cannot leave a stale pointer
	 * behind, and it matches the main module's permanent ns hook pin
	 */
	if (!try_module_get(THIS_MODULE)) {
		droid_lkm_misc_report("userns", "unsupported", "module reference");
		droid_lkm_userns_hooks_exit();
		droid_lkm_userns_owner_exit();
		droid_lkm_userns_nsops_exit();
		return -EBUSY;
	}
	droid_lkm_userns_module_pinned = true;

	droid_lkm_userns_map_selftest();

	ret = droid_lkm_userns_captrace_init();
	if (ret)
		droid_lkm_misc_warn("cap trace unavailable: %d\n", ret);

	/* the map files are an addition to the namespace, a kernel without them keeps the rest */
	ret = droid_lkm_userns_proc_init();
	if (ret)
		droid_lkm_misc_warn("id map files unavailable: %d\n", ret);

	droid_lkm_misc_report("userns", "ready",
			      "unshare clone namespace entry credential swap mount");
	droid_lkm_misc_report("userns cap trace", droid_lkm_userns_captrace_on ?
			      "on" : "off", "captrace=<bool>");
	droid_lkm_misc_info("userns ready\n");
	droid_lkm_userns_selftest_run();

	/* reported after the sample, so the count includes the namespace it left behind */
	scnprintf(detail, sizeof(detail), "%u live", droid_lkm_userns_live());
	droid_lkm_misc_report("userns objects", "ready", detail);

	return 0;
}

unsigned int droid_lkm_userns_live(void)
{
	unsigned long flags;
	unsigned int count;

	spin_lock_irqsave(&droid_lkm_userns_lock, flags);
	count = droid_lkm_userns_count;
	spin_unlock_irqrestore(&droid_lkm_userns_lock, flags);

	return count;
}

void droid_lkm_userns_exit(void)
{
	struct droid_lkm_userns *u, *tmp;
	unsigned long flags;

	droid_lkm_userns_proc_exit();
	droid_lkm_userns_captrace_exit();
	droid_lkm_userns_hooks_exit();
	droid_lkm_userns_owner_exit();

	spin_lock_irqsave(&droid_lkm_userns_lock, flags);
	list_for_each_entry_safe(u, tmp, &droid_lkm_userns_list, link) {
		list_del_init(&u->link);
		spin_unlock_irqrestore(&droid_lkm_userns_lock, flags);

		droid_lkm_misc_ks.proc_free_inum(u->uns.ns.inum);
		kfree(u);

		spin_lock_irqsave(&droid_lkm_userns_lock, flags);
	}
	spin_unlock_irqrestore(&droid_lkm_userns_lock, flags);

	droid_lkm_userns_nsops_exit();

	if (droid_lkm_userns_module_pinned) {
		droid_lkm_userns_module_pinned = false;
		module_put(THIS_MODULE);
	}
}

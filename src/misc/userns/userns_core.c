// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/user_namespace.h>

#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "userns.h"

static LIST_HEAD(droid_lkm_userns_list);
static DEFINE_MUTEX(droid_lkm_userns_lock);

bool droid_lkm_userns_is_ours(const struct user_namespace *uns)
{
	struct droid_lkm_userns *u;
	bool found = false;

	if (!uns)
		return false;

	mutex_lock(&droid_lkm_userns_lock);
	list_for_each_entry(u, &droid_lkm_userns_list, link) {
		if (&u->uns == uns) {
			found = true;
			break;
		}
	}
	mutex_unlock(&droid_lkm_userns_lock);

	return found;
}

struct droid_lkm_userns *droid_lkm_userns_of(struct task_struct *task)
{
	struct user_namespace *uns;

	if (!task)
		return NULL;

	uns = __task_cred(task)->user_ns;
	if (!droid_lkm_userns_is_ours(uns))
		return NULL;

	return container_of(uns, struct droid_lkm_userns, uns);
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
		return NULL;

	u = kzalloc(sizeof(*u), GFP_KERNEL);
	if (!u)
		return NULL;

	ret = droid_lkm_misc_ks.proc_alloc_inum(&inum);
	if (ret) {
		kfree(u);
		return NULL;
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

	/* phase one installs the identity map, the only map a container on this kernel could write */
	droid_lkm_userns_map_identity(u);

	mutex_lock(&droid_lkm_userns_lock);
	list_add_tail(&u->link, &droid_lkm_userns_list);
	mutex_unlock(&droid_lkm_userns_lock);

	droid_lkm_misc_dbg("userns create %p level=%u inum=%u owner=%u\n", u,
		u->level, inum, __kuid_val(u->uns.owner));

	return u;
}

void droid_lkm_userns_destroy(struct droid_lkm_userns *u)
{
	if (!u)
		return;

	mutex_lock(&droid_lkm_userns_lock);
	list_del_init(&u->link);
	mutex_unlock(&droid_lkm_userns_lock);

	droid_lkm_misc_ks.proc_free_inum(u->uns.ns.inum);
	kfree(u);
}

int droid_lkm_userns_init(void)
{
	char detail[32];
	int ret;

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

	droid_lkm_userns_map_selftest();

	ret = droid_lkm_userns_captrace_init();
	if (ret)
		droid_lkm_misc_warn("cap trace unavailable: %d\n", ret);

	/* the map files are an addition to the namespace, a kernel without them keeps the rest */
	ret = droid_lkm_userns_proc_init();
	if (ret)
		droid_lkm_misc_warn("id map files unavailable: %d\n", ret);

	droid_lkm_misc_report("userns", "ready",
			      "unshare clone namespace entry credential swap");
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
	struct droid_lkm_userns *u;
	unsigned int count = 0;

	mutex_lock(&droid_lkm_userns_lock);
	list_for_each_entry(u, &droid_lkm_userns_list, link)
		count++;
	mutex_unlock(&droid_lkm_userns_lock);

	return count;
}

void droid_lkm_userns_exit(void)
{
	struct droid_lkm_userns *u, *tmp;

	droid_lkm_userns_proc_exit();
	droid_lkm_userns_captrace_exit();
	droid_lkm_userns_hooks_exit();
	droid_lkm_userns_owner_exit();

	mutex_lock(&droid_lkm_userns_lock);
	list_for_each_entry_safe(u, tmp, &droid_lkm_userns_list, link) {
		list_del_init(&u->link);
		droid_lkm_misc_ks.proc_free_inum(u->uns.ns.inum);
		kfree(u);
	}
	mutex_unlock(&droid_lkm_userns_lock);

	droid_lkm_userns_nsops_exit();
}

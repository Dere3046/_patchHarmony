// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */
#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/ns_common.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/user_namespace.h>

#include "ds.h"
#include "ds_ksym.h"
#include "native_compat.h"

static struct pid_namespace *(*droid_native_real_copy_pid_ns_fn)(
	droid_native_ns_flags_t flags, struct user_namespace *user_ns,
	struct pid_namespace *old_ns);
static void (*droid_native_real_put_pid_ns_fn)(struct pid_namespace *ns);
static bool droid_native_pidns_runtime_checked;
static bool droid_native_pidns_runtime;

bool droid_native_pidns_runtime_supported(void)
{
	if (!droid_native_pidns_runtime_checked) {
		droid_native_real_copy_pid_ns_fn = (void *)droid_lkm_sym("copy_pid_ns");
		droid_native_real_put_pid_ns_fn = (void *)droid_lkm_sym("put_pid_ns");
		droid_native_pidns_runtime =
			droid_native_real_copy_pid_ns_fn &&
			droid_native_real_put_pid_ns_fn;
		droid_native_pidns_runtime_checked = true;
	}
	return droid_native_pidns_runtime;
}

__nocfi struct pid_namespace *droid_native_real_copy_pid_ns(
	droid_native_ns_flags_t flags, struct user_namespace *user_ns,
	struct pid_namespace *old_ns)
{
	return droid_native_real_copy_pid_ns_fn(flags, user_ns, old_ns);
}

__nocfi void droid_native_real_put_pid_ns(struct pid_namespace *ns)
{
	droid_native_real_put_pid_ns_fn(ns);
}

__nocfi int droid_native_ns_alloc_inum(struct ns_common *ns)
{
	return droid_lkm_ks.proc_alloc_inum(&ns->inum);
}

__nocfi void droid_native_ns_free_inum(struct ns_common *ns)
{
	droid_lkm_ks.proc_free_inum(ns->inum);
}

__nocfi void droid_native_disable_pid_allocation(struct pid_namespace *ns)
{
	droid_lkm_ks.disable_pid_allocation(ns);
}

__nocfi int droid_native_group_send_sig_info(int sig, struct kernel_siginfo *info,
				     struct task_struct *p, enum pid_type type)
{
	return droid_lkm_ks.group_send_sig_info(sig, info, p, type);
}

__nocfi long droid_native_kernel_wait4(pid_t upid, int __user *stat_addr, int options,
			       struct rusage __user *ru)
{
	return droid_lkm_ks.kernel_wait4(upid, stat_addr, options, ru);
}

__nocfi void droid_native_do_exit(long code)
{
	void (*fn)(long code) = (void *)droid_lkm_sym("do_exit");

	if (fn)
		fn(code);
}

__nocfi struct ucounts *droid_native_inc_ucount(struct user_namespace *ns, kuid_t uid,
				       enum ucount_type type)
{
	struct ucounts *(*fn)(struct user_namespace *, kuid_t, enum ucount_type);

	fn = (void *)droid_lkm_sym("inc_ucount");
	return fn ? fn(ns, uid, type) : NULL;
}

__nocfi void droid_native_dec_ucount(struct ucounts *ucounts, enum ucount_type type)
{
	void (*fn)(struct ucounts *, enum ucount_type);

	fn = (void *)droid_lkm_sym("dec_ucount");
	if (fn)
		fn(ucounts, type);
}

__nocfi struct user_namespace *droid_native_get_user_ns(struct user_namespace *ns)
{
	return droid_lkm_get_user_ns(ns);
}

__nocfi void droid_native_put_user_ns(struct user_namespace *ns)
{
	put_user_ns(ns);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
extern const struct proc_ns_operations droid_native_pidns_operations;

__nocfi int droid_native_ns_common_init(struct ns_common *ns)
{
	int (*fn)(struct ns_common *, u32, const struct proc_ns_operations *, int);

	fn = (void *)droid_lkm_sym("__ns_common_init");
	return fn ? fn(ns, CLONE_NEWPID, &droid_native_pidns_operations, 0) :
		    -ENOSYS;
}

__nocfi void droid_native_ns_common_free(struct ns_common *ns)
{
	void (*fn)(struct ns_common *);

	fn = (void *)droid_lkm_sym("__ns_common_free");
	if (fn)
		fn(ns);
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
__nocfi int droid_native_ns_tree_add_raw(struct ns_common *ns)
{
	u64 (*gen_id)(struct ns_common *);
	void (*add_raw)(struct ns_common *, void *);
	void *tree = (void *)droid_lkm_sym("pid_ns_tree");

	gen_id = (void *)droid_lkm_sym("ns_tree_gen_id");
	add_raw = (void *)droid_lkm_sym("__ns_tree_add_raw");
	if (!tree || !gen_id || !add_raw)
		return -ENOSYS;
	gen_id(ns);
	add_raw(ns, tree);
	return 0;
}

__nocfi void droid_native_ns_tree_remove_raw(struct ns_common *ns)
{
	void (*remove_raw)(struct ns_common *, void *);
	void *tree = (void *)droid_lkm_sym("pid_ns_tree");

	remove_raw = (void *)droid_lkm_sym("__ns_tree_remove");
	if (tree && remove_raw)
		remove_raw(ns, tree);
}

__nocfi int droid_native_pidns_sysctl_register_raw(struct pid_namespace *ns)
{
	int (*fn)(struct pid_namespace *);

	fn = (void *)droid_lkm_sym("register_pidns_sysctls");
	return fn ? fn(ns) : -ENOSYS;
}

__nocfi void droid_native_pidns_sysctl_unregister_raw(struct pid_namespace *ns)
{
	void (*fn)(struct pid_namespace *);

	fn = (void *)droid_lkm_sym("unregister_pidns_sysctls");
	if (fn)
		fn(ns);
}
#endif

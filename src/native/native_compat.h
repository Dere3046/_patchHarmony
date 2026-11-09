// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Dere3046
 */
#ifndef DROID_LKM_NATIVE_COMPAT_H
#define DROID_LKM_NATIVE_COMPAT_H

#include <linux/version.h>
#include <linux/kref.h>
#include <linux/refcount.h>
#include <linux/pid_namespace.h>
#include <linux/ns_common.h>
#include <linux/proc_ns.h>
#include <linux/user_namespace.h>

extern const struct proc_ns_operations droid_native_pidns_operations;
int droid_native_ns_alloc_inum(struct ns_common *ns);
void droid_native_ns_free_inum(struct ns_common *ns);
void droid_native_dec_ucount(struct ucounts *ucounts, enum ucount_type type);
void droid_native_put_user_ns(struct user_namespace *ns);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
typedef u64 droid_native_ns_flags_t;
#else
typedef unsigned long droid_native_ns_flags_t;
#endif

bool droid_native_pidns_runtime_supported(void);
struct pid_namespace *droid_native_real_copy_pid_ns(
	droid_native_ns_flags_t flags, struct user_namespace *user_ns,
	struct pid_namespace *old_ns);
void droid_native_real_put_pid_ns(struct pid_namespace *ns);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
#define DROID_NATIVE_PIDNS_TYPE .type = CLONE_NEWPID,
#else
#define DROID_NATIVE_PIDNS_TYPE
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)

void droid_native_pidns_kref_release(struct kref *kref);

static inline void droid_native_pidns_ref_init(struct pid_namespace *ns)
{
	kref_init(&ns->kref);
}

static inline void droid_native_pidns_ref_get(struct pid_namespace *ns)
{
	kref_get(&ns->kref);
}

static inline bool droid_native_pidns_ref_put(struct pid_namespace *ns)
{
	return kref_put(&ns->kref, droid_native_pidns_kref_release);
}

#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)

static inline void droid_native_pidns_ref_init(struct pid_namespace *ns)
{
	refcount_set(&ns->ns.count, 1);
}

static inline void droid_native_pidns_ref_get(struct pid_namespace *ns)
{
	refcount_inc(&ns->ns.count);
}

static inline bool droid_native_pidns_ref_put(struct pid_namespace *ns)
{
	return refcount_dec_and_test(&ns->ns.count);
}

#else

static inline void droid_native_pidns_ref_init(struct pid_namespace *ns)
{
	refcount_set(&ns->ns.__ns_ref, 1);
}

static inline void droid_native_pidns_ref_get(struct pid_namespace *ns)
{
	(void)ns_ref_get(ns);
}

static inline bool droid_native_pidns_ref_put(struct pid_namespace *ns)
{
	return ns_ref_put(ns);
}

#endif

static inline struct pid_namespace *droid_native_to_pid_ns(struct ns_common *ns)
{
	return container_of(ns, struct pid_namespace, ns);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
int droid_native_ns_common_init(struct ns_common *ns);
void droid_native_ns_common_free(struct ns_common *ns);
void droid_native_pidns_destroy_work(struct work_struct *work);
int droid_native_ns_tree_add_raw(struct ns_common *ns);
void droid_native_ns_tree_remove_raw(struct ns_common *ns);
int droid_native_pidns_sysctl_register_raw(struct pid_namespace *ns);
void droid_native_pidns_sysctl_unregister_raw(struct pid_namespace *ns);
#endif

static inline int droid_native_pidns_tree_add(struct pid_namespace *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return droid_native_ns_tree_add_raw(&ns->ns);
#else
	(void)ns;
	return 0;
#endif
}

static inline void droid_native_pidns_tree_remove(struct pid_namespace *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	droid_native_ns_tree_remove_raw(&ns->ns);
#else
	(void)ns;
#endif
}

static inline int droid_native_pidns_sysctl_register(struct pid_namespace *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return droid_native_pidns_sysctl_register_raw(ns);
#else
	(void)ns;
	return 0;
#endif
}

static inline void droid_native_pidns_sysctl_unregister(struct pid_namespace *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	droid_native_pidns_sysctl_unregister_raw(ns);
#else
	(void)ns;
#endif
}

static inline int droid_native_pidns_common_init(struct pid_namespace *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return droid_native_ns_common_init(&ns->ns);
#else
	int err;

	err = droid_native_ns_alloc_inum(&ns->ns);
	if (!err)
		ns->ns.ops = &droid_native_pidns_operations;
	return err;
#endif
}

static inline void droid_native_pidns_common_free(struct pid_namespace *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	droid_native_ns_common_free(&ns->ns);
#else
	droid_native_ns_free_inum(&ns->ns);
#endif
}

static inline void droid_native_pidns_work_init(struct pid_namespace *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	INIT_WORK(&ns->work, droid_native_pidns_destroy_work);
#else
	(void)ns;
#endif
}

static inline void droid_native_pidns_memfd_init(struct pid_namespace *ns,
						 struct pid_namespace *parent)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0) && \
	defined(CONFIG_SYSCTL) && defined(CONFIG_MEMFD_CREATE)
	ns->memfd_noexec_scope = pidns_memfd_noexec_scope(parent);
#else
	(void)ns;
	(void)parent;
#endif
}

#endif

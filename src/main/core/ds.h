// SPDX-License-Identifier: GPL-2.0-only

#include <linux/version.h>
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_H
#define DROID_LKM_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>

#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/cred.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/ns_common.h>
#include <linux/slab.h>

#include "core.h"
#include "hk_patch.h"
#include "ds_compat.h"
#include "ds_hook.h"
#include "ds_ti.h"

static inline pid_t droid_lkm_task_pid(const struct task_struct *task)
{
	return *(pid_t *)((char *)task + droid_lkm_layout_off(DROID_LKM_F_TASK_PID));
}

static inline pid_t droid_lkm_task_tgid(const struct task_struct *task)
{
	return *(pid_t *)((char *)task + droid_lkm_layout_off(DROID_LKM_F_TASK_TGID));
}

static inline const char *droid_lkm_task_comm(const struct task_struct *task)
{
	return (const char *)task + droid_lkm_layout_off(DROID_LKM_F_TASK_COMM);
}

static inline struct nsproxy *droid_lkm_task_nsproxy(const struct task_struct *task)
{
	return *(struct nsproxy **)((char *)task +
				    droid_lkm_layout_off(DROID_LKM_F_TASK_NSPROXY));
}

static inline struct pid *droid_lkm_task_thread_pid(const struct task_struct *task)
{
	return *(struct pid **)((char *)task +
				droid_lkm_layout_off(DROID_LKM_F_TASK_THREAD_PID));
}

static inline struct list_head *droid_lkm_task_tasks(const struct task_struct *task)
{
	return (struct list_head *)((char *)task +
				    droid_lkm_layout_off(DROID_LKM_F_TASK_TASKS));
}

static inline struct task_struct *
droid_lkm_task_from_tasks(struct list_head *node)
{
	return (struct task_struct *)((char *)node -
		droid_lkm_layout_off(DROID_LKM_F_TASK_TASKS));
}

static inline struct task_struct *droid_lkm_next_task(struct task_struct *task)
{
	return droid_lkm_task_from_tasks(droid_lkm_task_tasks(task)->next);
}

static inline struct mm_struct *droid_lkm_task_mm(const struct task_struct *task)
{
	return *(struct mm_struct **)((char *)task +
				       droid_lkm_layout_off(DROID_LKM_F_TASK_MM));
}

static inline struct fs_struct *droid_lkm_task_fs(const struct task_struct *task)
{
	return *(struct fs_struct **)((char *)task +
				      droid_lkm_layout_off(DROID_LKM_F_TASK_FS));
}

static inline struct signal_struct *droid_lkm_task_signal(const struct task_struct *task)
{
	return *(struct signal_struct **)((char *)task +
					   droid_lkm_layout_off(DROID_LKM_F_TASK_SIGNAL));
}

static inline struct sighand_struct *droid_lkm_task_sighand(const struct task_struct *task)
{
	return *(struct sighand_struct **)((char *)task +
					    droid_lkm_layout_off(DROID_LKM_F_TASK_SIGHAND));
}

static inline const struct cred *droid_lkm_task_cred(const struct task_struct *task)
{
	return *(const struct cred **)((char *)task +
					droid_lkm_layout_off(DROID_LKM_F_TASK_CRED));
}

static inline const struct cred *droid_lkm_task_real_cred(const struct task_struct *task)
{
	return *(const struct cred **)((char *)task +
					droid_lkm_layout_off(DROID_LKM_F_TASK_REAL_CRED));
}

static inline unsigned int droid_lkm_task_flags(const struct task_struct *task)
{
	return *(unsigned int *)((char *)task +
				 droid_lkm_layout_off(DROID_LKM_F_TASK_FLAGS));
}

static inline struct ipc_namespace *
droid_lkm_nsproxy_ipc_ns(const struct nsproxy *nsproxy)
{
	return *(struct ipc_namespace **)((char *)nsproxy +
					  droid_lkm_layout_off(DROID_LKM_F_NSPROXY_IPC_NS));
}

static inline void droid_lkm_nsproxy_set_ipc_ns(struct nsproxy *nsproxy,
						struct ipc_namespace *ns)
{
	*(struct ipc_namespace **)((char *)nsproxy +
				   droid_lkm_layout_off(DROID_LKM_F_NSPROXY_IPC_NS)) = ns;
}

static inline struct pid_namespace *
droid_lkm_nsproxy_pid_ns_for_children(const struct nsproxy *nsproxy)
{
	return *(struct pid_namespace **)((char *)nsproxy +
					  droid_lkm_layout_off(DROID_LKM_F_NSPROXY_PID_NS_FOR_CHILDREN));
}

static inline void droid_lkm_nsproxy_set_pid_ns_for_children(
					struct nsproxy *nsproxy,
					struct pid_namespace *ns)
{
	*(struct pid_namespace **)((char *)nsproxy +
				   droid_lkm_layout_off(DROID_LKM_F_NSPROXY_PID_NS_FOR_CHILDREN)) = ns;
}

static inline struct uts_namespace *
droid_lkm_nsproxy_uts_ns(const struct nsproxy *nsproxy)
{
	return *(struct uts_namespace **)((char *)nsproxy +
					  droid_lkm_layout_off(DROID_LKM_F_NSPROXY_UTS_NS));
}

static inline struct mnt_namespace *
droid_lkm_nsproxy_mnt_ns(const struct nsproxy *nsproxy)
{
	return *(struct mnt_namespace **)((char *)nsproxy +
					  droid_lkm_layout_off(DROID_LKM_F_NSPROXY_MNT_NS));
}

static inline struct net *
droid_lkm_nsproxy_net_ns(const struct nsproxy *nsproxy)
{
	return *(struct net **)((char *)nsproxy +
				droid_lkm_layout_off(DROID_LKM_F_NSPROXY_NET_NS));
}

static inline unsigned int droid_lkm_pid_level(const struct pid *pid)
{
	return *(unsigned int *)((char *)pid +
				 droid_lkm_layout_off(DROID_LKM_F_PID_LEVEL));
}

static inline struct upid *droid_lkm_pid_numbers(const struct pid *pid)
{
	return (struct upid *)((char *)pid +
			       droid_lkm_layout_off(DROID_LKM_F_PID_NUMBERS));
}

static inline pid_t droid_lkm_upid_nr(const struct upid *upid)
{
	return *(pid_t *)((char *)upid +
			  droid_lkm_layout_off(DROID_LKM_F_UPID_NR));
}

static inline struct pid_namespace *droid_lkm_upid_ns(const struct upid *upid)
{
	return *(struct pid_namespace **)((char *)upid +
					  droid_lkm_layout_off(DROID_LKM_F_UPID_NS));
}

static inline struct idr *droid_lkm_pidns_idr(struct pid_namespace *ns)
{
	return (struct idr *)((char *)ns +
			      droid_lkm_layout_off(DROID_LKM_F_PID_NS_IDR));
}

static inline unsigned int droid_lkm_pidns_level(const struct pid_namespace *ns)
{
	return *(unsigned int *)((char *)ns +
				 droid_lkm_layout_off(DROID_LKM_F_PID_NS_LEVEL));
}

static inline void droid_lkm_pidns_set_level(struct pid_namespace *ns,
					     unsigned int level)
{
	*(unsigned int *)((char *)ns +
			  droid_lkm_layout_off(DROID_LKM_F_PID_NS_LEVEL)) = level;
}

static inline struct pid_namespace *droid_lkm_pidns_parent(const struct pid_namespace *ns)
{
	return *(struct pid_namespace **)((char *)ns +
					  droid_lkm_layout_off(DROID_LKM_F_PID_NS_PARENT));
}

static inline void droid_lkm_pidns_set_parent(struct pid_namespace *ns,
					      struct pid_namespace *parent)
{
	*(struct pid_namespace **)((char *)ns +
				   droid_lkm_layout_off(DROID_LKM_F_PID_NS_PARENT)) = parent;
}

static inline struct kmem_cache *droid_lkm_pidns_pid_cachep(const struct pid_namespace *ns)
{
	return *(struct kmem_cache **)((char *)ns +
				       droid_lkm_layout_off(DROID_LKM_F_PID_NS_PID_CACHEP));
}

static inline void droid_lkm_pidns_set_pid_cachep(struct pid_namespace *ns,
						  struct kmem_cache *cachep)
{
	*(struct kmem_cache **)((char *)ns +
				droid_lkm_layout_off(DROID_LKM_F_PID_NS_PID_CACHEP)) = cachep;
}

static inline struct task_struct *droid_lkm_pidns_child_reaper(const struct pid_namespace *ns)
{
	return *(struct task_struct **)((char *)ns +
					droid_lkm_layout_off(DROID_LKM_F_PID_NS_CHILD_REAPER));
}

static inline void droid_lkm_pidns_set_child_reaper(struct pid_namespace *ns,
						    struct task_struct *task)
{
	*(struct task_struct **)((char *)ns +
				 droid_lkm_layout_off(DROID_LKM_F_PID_NS_CHILD_REAPER)) = task;
}

static inline struct user_namespace *droid_lkm_pidns_user_ns(const struct pid_namespace *ns)
{
	return *(struct user_namespace **)((char *)ns +
					   droid_lkm_layout_off(DROID_LKM_F_PID_NS_USER_NS));
}

static inline void droid_lkm_pidns_set_user_ns(struct pid_namespace *ns,
					       struct user_namespace *user_ns)
{
	*(struct user_namespace **)((char *)ns +
				    droid_lkm_layout_off(DROID_LKM_F_PID_NS_USER_NS)) = user_ns;
}

static inline void droid_lkm_pidns_set_ucounts(struct pid_namespace *ns,
					       struct ucounts *ucounts)
{
	*(struct ucounts **)((char *)ns +
			     droid_lkm_layout_off(DROID_LKM_F_PID_NS_UCOUNTS)) = ucounts;
}

static inline struct ucounts *droid_lkm_pidns_ucounts(const struct pid_namespace *ns)
{
	return *(struct ucounts **)((char *)ns +
				    droid_lkm_layout_off(DROID_LKM_F_PID_NS_UCOUNTS));
}

static inline unsigned int droid_lkm_pidns_pid_allocated(const struct pid_namespace *ns)
{
	return *(unsigned int *)((char *)ns +
				 droid_lkm_layout_off(DROID_LKM_F_PID_NS_PID_ALLOCATED));
}

static inline void droid_lkm_pidns_set_pid_allocated(struct pid_namespace *ns,
						     unsigned int val)
{
	*(unsigned int *)((char *)ns +
			  droid_lkm_layout_off(DROID_LKM_F_PID_NS_PID_ALLOCATED)) = val;
}

static inline struct ns_common *droid_lkm_pidns_ns(struct pid_namespace *ns)
{
	return (struct ns_common *)((char *)ns +
				    droid_lkm_layout_off(DROID_LKM_F_PID_NS_NS));
}

static inline int droid_lkm_pidns_reboot_value(const struct pid_namespace *ns)
{
	return *(int *)((char *)ns +
			droid_lkm_layout_off(DROID_LKM_F_PID_NS_REBOOT));
}

static inline void droid_lkm_pidns_set_reboot(struct pid_namespace *ns, int val)
{
	*(int *)((char *)ns +
		 droid_lkm_layout_off(DROID_LKM_F_PID_NS_REBOOT)) = val;
}

#define DROID_LKM_TAG "droid_lkm"

/* ipc side, refuses to load when the target kernel has no locked unmap entry */
int droid_lkm_munmap_init(void);

/* reached indirectly so the entry needs a bti landing pad on a real device */
#define DROID_LKM_ENTRY __attribute__((target("branch-protection=bti")))

#define droid_lkm_info(fmt, ...) pr_info("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_warn(fmt, ...) pr_warn("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_err(fmt, ...) pr_err("[" DROID_LKM_TAG "] " fmt, ##__VA_ARGS__)

extern bool droid_lkm_verbose;
#define droid_lkm_dbg(fmt, ...)                                                      \
	do {                                                                   \
		if (droid_lkm_verbose)                                                \
			pr_info("[" DROID_LKM_TAG "/dbg] " fmt, ##__VA_ARGS__);        \
	} while (0)

/*
 * kallsyms bootstrap, module symbols via module_kallsyms fallback
 *
 * the shims in this module are deliberately defined under the kernel's own
 * names, and that fallback searches the module tables too, so a kernel that
 * does not carry the name hands back the shim defined here instead. that is not
 * the kernel symbol: a shim whose body calls the resolved pointer calls itself.
 * the hook wrappers are this module's own symbols by design and carry the
 * module prefix, everything else that lands inside the module is a shim
 */
static inline unsigned long __nocfi droid_lkm_sym(const char *name)
{
	/* the compat layer owns the shim guard, see ds_compat_sym */
	return ds_compat_sym(name);
}

void droid_lkm_reset_task_ns_refs(void);
bool droid_lkm_any_task_ns_refs(void);

void droid_lkm_task_ipc_deferred_run(void);
void droid_lkm_keepalive_pin(void);
void droid_lkm_keepalive_sync(void);
void droid_lkm_keepalive_release(void);

/* the compat layer owns the write cascade, so there is one implementation */
static inline int droid_lkm_patch_write(void *dst, unsigned long val)
{
	return ds_compat_write(dst, val);
}


/*
 * 6.18 moved the namespace type out of proc_ns_operations and into
 * ns_common::ns_type. every place that hands a namespace its operations goes
 * through this, so no call site carries the difference
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define droid_lkm_ns_set_ops(_ns, _ops, _type)                                 \
	do {                                                                   \
		(_ns)->ops = (_ops);                                           \
		(_ns)->ns_type = (_type);                                      \
	} while (0)
#else
#define droid_lkm_ns_set_ops(_ns, _ops, _type)                                 \
	do {                                                                   \
		(_ns)->ops = (_ops);                                           \
	} while (0)
#endif

/*
 * 6.18 hands MODULE_INFO the namespace as a string, before it stringified the
 * token itself, so the call sites name the namespace once and this picks the
 * form the kernel takes
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#define DROID_LKM_STR(_x)	#_x
#define DROID_LKM_GKI_NS(_ns)	ANDROID_GKI_##_ns
#define DROID_LKM_IMPORT_NS(_ns)                                               \
	MODULE_IMPORT_NS(#_ns);                                                \
	MODULE_IMPORT_NS(DROID_LKM_STR(DROID_LKM_GKI_NS(_ns)))
#else
#define DROID_LKM_IMPORT_NS(_ns)	MODULE_IMPORT_NS(_ns)
#endif

/*
 * the kernel declares these itself before 6.18 while the shims here define
 * them on every version, so the prototypes live where both can see them
 */
struct dentry;
struct dentry_operations;
struct dentry *lookup_one_len(const char *name, struct dentry *base, int len);
void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op);

#endif /* DROID_LKM_H */

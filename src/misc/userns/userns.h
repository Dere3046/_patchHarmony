// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_MISC_USERNS_H
#define DROID_LKM_MISC_USERNS_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/uidgid.h>
#include <linux/ns_common.h>
#include <linux/user_namespace.h>
#include <linux/proc_ns.h>
#include <linux/refcount.h>
#include <linux/version.h>

#include <linux/workqueue.h>

struct task_struct;
struct cred;
struct mnt_namespace;

static inline refcount_t *droid_lkm_userns_ns_ref(struct ns_common *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return &ns->__ns_ref;
#else
	return &ns->count;
#endif
}

struct user_namespace *droid_lkm_userns_mntns_owner(struct mnt_namespace *mnt_ns);

/*
 * one fake user namespace. struct user_namespace comes first so the object can be handed to
 * the kernel and found again from a struct user_namespace pointer. the map starts with the
 * namespace's default (identity for a privileged creator, a single 0 -> creator extent for
 * everyone else) and a proc write may replace it once, subject to the creator's anchor
 */
struct droid_lkm_userns {
	struct user_namespace uns;
	struct list_head link;
	refcount_t count;
	struct work_struct work;
	unsigned int level;
	/* the creator held CAP_SETUID and CAP_SETGID over the initial namespace */
	bool privileged;
	/* a map was written through the proc file; the default map is not a write */
	bool uid_written;
	bool gid_written;
};

int droid_lkm_userns_init(void);
void droid_lkm_userns_exit(void);

int droid_lkm_userns_hooks_init(void);
void droid_lkm_userns_hooks_exit(void);

/* the initial user namespace, resolved once and shared by the owner work */
extern struct user_namespace *droid_lkm_userns_init_uns;

int droid_lkm_userns_owner_init(void);
void droid_lkm_userns_owner_exit(void);

extern bool droid_lkm_userns_captrace_on;
int droid_lkm_userns_captrace_init(void);
void droid_lkm_userns_captrace_exit(void);

/* /proc/<pid>/{uid_map,gid_map,setgroups}, added back under the pid directory */
int droid_lkm_userns_proc_init(void);
void droid_lkm_userns_proc_exit(void);

/* layers two and three of the install time self test */
void droid_lkm_userns_selftest_run(void);

struct droid_lkm_userns *droid_lkm_userns_create(struct droid_lkm_userns *parent);
void droid_lkm_userns_destroy(struct droid_lkm_userns *u);
struct droid_lkm_userns *droid_lkm_userns_get(struct droid_lkm_userns *u);
void droid_lkm_userns_put(struct droid_lkm_userns *u);

/*
 * borrowed: the pointer is only valid while the caller holds rcu_read_lock(). every
 * caller that keeps it past that point wants droid_lkm_userns_get_of() instead
 */
struct droid_lkm_userns *droid_lkm_userns_of(struct task_struct *task);

/* the same lookup, with a reference the caller owns and has to put */
struct droid_lkm_userns *droid_lkm_userns_get_of(struct task_struct *task);

struct droid_lkm_userns *droid_lkm_userns_from_ns(struct user_namespace *uns);
unsigned int droid_lkm_userns_live(void);
bool droid_lkm_userns_is_ours(const struct user_namespace *uns);

int droid_lkm_userns_install_current(struct droid_lkm_userns *u);
void droid_lkm_userns_install_cred(struct cred *new, struct droid_lkm_userns *u);

const struct proc_ns_operations *droid_lkm_userns_ops(void);
const struct proc_ns_operations *droid_lkm_userns_ops_for(struct task_struct *task);
int droid_lkm_userns_nsops_init(void);
void droid_lkm_userns_nsops_exit(void);

/*
 * why this module cannot be unloaded while the fake user namespace is up, and the one
 * place that says so. taken by droid_lkm_userns_nsops_init() at the point where the
 * main module starts handing out this module's proc_ns_operations, released by
 * droid_lkm_userns_nsops_exit() on the way back out
 */
void droid_lkm_userns_stay_resident(const char *why);
void droid_lkm_userns_release_resident(void);
bool droid_lkm_userns_is_resident(void);

/* an id no namespace maps, the same value map_id_up() returns */
#define DROID_LKM_USERNS_ID_UNMAPPED ((u32)-1)

u32 droid_lkm_userns_map_down(const struct droid_lkm_userns *u, u32 id, bool is_uid);
u32 droid_lkm_userns_map_up(const struct droid_lkm_userns *u, u32 id, bool is_uid);
u32 droid_lkm_userns_map_parent_down(const struct droid_lkm_userns *u, u32 id,
				     u32 count, bool is_uid);
bool droid_lkm_userns_map_ok(const struct droid_lkm_userns *u, u32 id, bool is_uid);
bool droid_lkm_userns_has_mapping(const struct droid_lkm_userns *u, u32 id,
				  bool is_uid);
void droid_lkm_userns_map_identity(struct droid_lkm_userns *u);
bool droid_lkm_userns_map_confine(struct droid_lkm_userns *u);
void droid_lkm_userns_map_selftest(void);

#endif

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

struct task_struct;
struct cred;

/*
 * ns_common's own reference count. 6.18 renamed the field to __ns_ref; 5.15
 * introduced it as count. a bare ns_common has no counter before 5.15 -- the
 * concrete type carries it there -- but this half is not built below 5.12 (the
 * Makefile gates it on mnt_idmapping.h) and nothing in it builds a plain
 * ns_common without going through one of the two shapes below, so only those
 * two are spelled. this mirrors droid_lkm_pidns_refcount() in ds_pidns.h
 */
static inline refcount_t *droid_lkm_userns_ns_ref(struct ns_common *ns)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	return &ns->__ns_ref;
#else
	return &ns->count;
#endif
}

/*
 * one fake user namespace. struct user_namespace comes first so the object can be handed to
 * the kernel and found again from a struct user_namespace pointer. the map starts empty like a
 * fresh namespace and becomes the identity map when written, an empty map reports 65534
 */
struct droid_lkm_userns {
	struct user_namespace uns;
	struct list_head link;
	unsigned int level;
	bool map_written;
};

int droid_lkm_userns_init(void);
void droid_lkm_userns_exit(void);

int droid_lkm_userns_hooks_init(void);
void droid_lkm_userns_hooks_exit(void);

/* the initial user namespace, resolved once and shared by the owner work */
extern struct user_namespace *droid_lkm_userns_init_uns;

int droid_lkm_userns_owner_init(void);
void droid_lkm_userns_owner_exit(void);

/* the owner of a mount namespace, for callers that hold the pointer but cannot see the type */
struct mnt_namespace;
struct user_namespace *droid_lkm_userns_mntns_owner(struct mnt_namespace *mnt_ns);

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
struct droid_lkm_userns *droid_lkm_userns_of(struct task_struct *task);
unsigned int droid_lkm_userns_live(void);
bool droid_lkm_userns_is_ours(const struct user_namespace *uns);

int droid_lkm_userns_install_current(struct droid_lkm_userns *u);
void droid_lkm_userns_install_cred(struct cred *new, struct droid_lkm_userns *u);

const struct proc_ns_operations *droid_lkm_userns_ops(void);
const struct proc_ns_operations *droid_lkm_userns_ops_for(struct task_struct *task);
int droid_lkm_userns_nsops_init(void);
void droid_lkm_userns_nsops_exit(void);

u32 droid_lkm_userns_map_down(const struct droid_lkm_userns *u, u32 id);
u32 droid_lkm_userns_map_up(const struct droid_lkm_userns *u, u32 id);
bool droid_lkm_userns_map_ok(const struct droid_lkm_userns *u, u32 id);
void droid_lkm_userns_map_identity(struct droid_lkm_userns *u);
void droid_lkm_userns_map_selftest(void);

#endif

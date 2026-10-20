// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_COMPAT_H
#define DROID_LKM_COMPAT_H

/*
 * kernel API drift across the GKI branches this module builds against.
 * a shape that differs in type only is absorbed here so call sites stay
 * identical on every branch, and anything that differs in behaviour is decided
 * at load time in ds_caps.c instead of here.
 */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/msg.h>
#include <linux/ns_common.h>
#include <linux/sysctl.h>

/*
 * ns_common::stashed is an atomic_long_t up to 6.6 and a struct dentry * from
 * 6.12, same size in both, and it is the first member of the struct on every
 * kernel we build for (checked on 5.10, 5.15, 6.1, 6.6 and 6.12). reading and
 * writing the first word therefore serves both layouts, and no call site has to
 * know which kernel it is on.
 */
static inline unsigned long droid_lkm_ns_stashed_raw(const struct ns_common *ns)
{
	return READ_ONCE(*(const unsigned long *)ns);
}

static inline bool droid_lkm_ns_stashed(const struct ns_common *ns)
{
	return droid_lkm_ns_stashed_raw(ns) != 0;
}

static inline void *droid_lkm_ns_stash_ptr(const struct ns_common *ns)
{
	return (void *)droid_lkm_ns_stashed_raw(ns);
}

static inline void droid_lkm_ns_stash_clear(struct ns_common *ns)
{
	WRITE_ONCE(*(unsigned long *)ns, 0);
}

/*
 * struct fd: 6.12 hides the members and provides fd_file(), 6.6 exposes .file
 * directly. the guard tests for the accessor macro.
 */
#ifdef fd_file
#define droid_lkm_fd_file(f)	fd_file(f)
#else
#define droid_lkm_fd_file(f)	((f).file)
#endif

/*
 * msg_msg allocation buckets arrived with CONFIG_SLAB_BUCKETS. without them
 * the kernel macros already reduce to plain kmalloc, keep the same shape so
 * the call sites stay version agnostic.
 */
#ifdef CONFIG_SLAB_BUCKETS
typedef kmem_buckets droid_lkm_msg_bucket_t;

static inline droid_lkm_msg_bucket_t *droid_lkm_msg_bucket_create(void)
{
	return kmem_buckets_create("msg_msg", SLAB_ACCOUNT, sizeof(struct msg_msg),
				   PAGE_SIZE - sizeof(struct msg_msg), NULL);
}

static inline void *droid_lkm_msg_alloc(droid_lkm_msg_bucket_t *buckets,
					size_t size)
{
	return kmem_buckets_alloc(buckets, size, GFP_KERNEL);
}
#else
typedef void droid_lkm_msg_bucket_t;

static inline droid_lkm_msg_bucket_t *droid_lkm_msg_bucket_create(void)
{
	return NULL;
}

static inline void *droid_lkm_msg_alloc(droid_lkm_msg_bucket_t *buckets,
					size_t size)
{
	return kmalloc(size, GFP_KERNEL);
}
#endif

/*
 * 6.6 struct file_operations has no fop_flags field at all, so the line has to
 * be omitted instead of zeroed. the guard tests for the flag macro.
 */
#ifdef FOP_HUGE_PAGES
#define DROID_LKM_SHM_HUGE_FOP	.fop_flags = FOP_HUGE_PAGES,
#else
#define DROID_LKM_SHM_HUGE_FOP
#endif

/*
 * proc_handler takes a const ctl_table from 6.12 on. the kernel builds with
 * -Wcast-function-type-strict, so a handler's parameter has to match the
 * typedef and the choice can only be made at compile time.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define DROID_LKM_CTL_TABLE	const struct ctl_table
#else
#define DROID_LKM_CTL_TABLE	struct ctl_table
#endif

/*
 * inode_permission takes (inode, mask) up to 5.11, a user namespace from 5.12
 * and an id mapping from 6.3. an argument list is not something a runtime
 * capability can select, so the shape is folded here and the shim in
 * ksym_shim.c, the thunk field in ds_ksym.h and every caller spell it through
 * these two macros. the value handed in is droid_lkm_caps.idmap_none, which is
 * nop_mnt_idmap or init_user_ns for the running kernel. ipc_mqueue_compat.h
 * folds the same shape under DROID_LKM_MQ_IDMAP_ARG for its vfs callees.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#define DROID_LKM_IDMAP_PARAM	struct mnt_idmap *idmap,
#define DROID_LKM_IDMAP_PASS	idmap,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#define DROID_LKM_IDMAP_PARAM	struct user_namespace *mnt_userns,
#define DROID_LKM_IDMAP_PASS	mnt_userns,
#else
#define DROID_LKM_IDMAP_PARAM
#define DROID_LKM_IDMAP_PASS
#endif

/*
 * shmem_lock's last argument is a struct user_struct up to 5.10 and a struct
 * ucounts from 5.15 on, the count stays the same. folded here so the shim and
 * the thunk field read like the inode_permission pair above.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#define DROID_LKM_SHMEM_LOCK_PARAM	struct ucounts *ucounts
#define DROID_LKM_SHMEM_LOCK_PASS	ucounts
#else
#define DROID_LKM_SHMEM_LOCK_PARAM	struct user_struct *user
#define DROID_LKM_SHMEM_LOCK_PASS	user
#endif

/*
 * the ucounts helpers take the branch enum, and an enum is a distinct type for
 * the KCFI hash, so spelling it int traps on every call. 6.1 renamed the enum.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
typedef enum rlimit_type droid_lkm_rlimit_type;
#else
typedef enum ucount_type droid_lkm_rlimit_type;
#endif

/*
 * __register_sysctl_table gained the table size in 6.6, and its table argument
 * stays writable even on 6.12 where the rest of the registration API takes
 * const tables. a shape one cast away from the real one fails the KCFI hash
 * before the call runs, so the resolved pointer and the call spell the tail
 * through these two macros and the table argument is never const.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#define DROID_LKM_REG_SYSCTL_PARAM	, size_t table_size
#define DROID_LKM_REG_SYSCTL_PASS	, table_size
#else
#define DROID_LKM_REG_SYSCTL_PARAM
#define DROID_LKM_REG_SYSCTL_PASS
#endif

#endif

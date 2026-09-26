// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_COMPAT_H
#define DROID_LKM_COMPAT_H

/*
 * kernel API drift across the GKI branches this module builds against.
 * every item below is a compile time shape change, so a wrong boundary breaks
 * the build instead of misbehaving at run time. the version boundaries are the
 * ones verified on android15-6.6 (old shape) and android16-6.12 (new shape),
 * with android14-6.1 and android17-6.18 checked for the same split.
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
 * ns_common::stashed packs the nsfs dentry into an atomic_long_t on 6.1 and
 * 6.6, and is a plain struct dentry * on 6.12 and 6.18. the guard treats
 * everything before 6.12 as the old shape.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static inline bool droid_lkm_ns_stashed(const struct ns_common *ns)
{
	return ns->stashed != NULL;
}

static inline void *droid_lkm_ns_stash_ptr(const struct ns_common *ns)
{
	return ns->stashed;
}

static inline void droid_lkm_ns_stash_clear(struct ns_common *ns)
{
	ns->stashed = NULL;
}
#else
static inline bool droid_lkm_ns_stashed(const struct ns_common *ns)
{
	return atomic_long_read(&ns->stashed) != 0;
}

static inline void *droid_lkm_ns_stash_ptr(const struct ns_common *ns)
{
	return (void *)atomic_long_read(&ns->stashed);
}

static inline void droid_lkm_ns_stash_clear(struct ns_common *ns)
{
	atomic_long_set(&ns->stashed, 0);
}
#endif

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
 * 6.6 defines do_vmi_align_munmap without a header prototype, 6.12 declares it
 * in include/linux/mm.h
 */
int do_vmi_align_munmap(struct vma_iterator *vmi, struct vm_area_struct *vma,
			struct mm_struct *mm, unsigned long start,
			unsigned long end, struct list_head *uf, bool unlock);

/*
 * ctl_table is const from 6.12 (6.6 passes a plain pointer), handlers must
 * match the kernel typedef.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define DROID_LKM_CTL_TABLE	const struct ctl_table
#else
#define DROID_LKM_CTL_TABLE	struct ctl_table
#endif

#endif

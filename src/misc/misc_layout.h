// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_MISC_LAYOUT_H
#define DROID_LKM_MISC_LAYOUT_H

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/fs_struct.h>
#include <linux/cred.h>

extern u32 droid_lkm_misc_task_cred_off;
extern u32 droid_lkm_misc_task_real_cred_off;
extern u32 droid_lkm_misc_task_fs_off;
extern u32 droid_lkm_misc_task_nsproxy_off;
extern u32 droid_lkm_misc_mntns_user_ns_off;

static inline const struct cred *droid_lkm_misc_task_cred(const struct task_struct *task)
{
	if (!task)
		return NULL;
	return *(const struct cred **)((char *)task + droid_lkm_misc_task_cred_off);
}

static inline const struct cred *droid_lkm_misc_task_real_cred(const struct task_struct *task)
{
	if (!task)
		return NULL;
	return *(const struct cred **)((char *)task + droid_lkm_misc_task_real_cred_off);
}

static inline struct fs_struct *droid_lkm_misc_task_fs(const struct task_struct *task)
{
	if (!task)
		return NULL;
	return *(struct fs_struct **)((char *)task + droid_lkm_misc_task_fs_off);
}

static inline struct nsproxy *droid_lkm_misc_task_nsproxy(const struct task_struct *task)
{
	if (!task)
		return NULL;
	return *(struct nsproxy **)((char *)task + droid_lkm_misc_task_nsproxy_off);
}

int droid_lkm_misc_layout_init(void);

#endif /* DROID_LKM_MISC_LAYOUT_H */

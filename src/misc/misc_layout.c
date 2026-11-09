// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/fs_struct.h>
#include <linux/cred.h>

#include "misc.h"
#include "misc_layout.h"

/*
 * the same table the other two modules hand to the same engine: BTF first, the
 * anchor symbols when the running kernel carries no BTF, and nothing at all when
 * neither can vouch for an offset. an offset here is dereferenced, not compared,
 * so an unverified one has to switch the feature off rather than fall back to
 * whatever this build happened to see
 *
 * mnt_namespace.user_ns is the one field the anchor does not derive. it is filled
 * from BTF only, and the mount owner work reports itself unsupported without it
 */
enum {
	DROID_LKM_MISC_F_TASK_CRED,
	DROID_LKM_MISC_F_TASK_REAL_CRED,
	DROID_LKM_MISC_F_TASK_FS,
	DROID_LKM_MISC_F_TASK_NSPROXY,
	DROID_LKM_MISC_F_MNTNS_USER_NS,
	DROID_LKM_MISC_F_COUNT
};

static struct ds_compat_field droid_lkm_misc_fields[DROID_LKM_MISC_F_COUNT] = {
	[DROID_LKM_MISC_F_TASK_CRED] = {
		.type = "task_struct", .member = "cred",
		.build = offsetof(struct task_struct, cred),
	},
	[DROID_LKM_MISC_F_TASK_REAL_CRED] = {
		.type = "task_struct", .member = "real_cred",
		.build = offsetof(struct task_struct, real_cred),
	},
	[DROID_LKM_MISC_F_TASK_FS] = {
		.type = "task_struct", .member = "fs",
		.build = offsetof(struct task_struct, fs),
	},
	[DROID_LKM_MISC_F_TASK_NSPROXY] = {
		.type = "task_struct", .member = "nsproxy",
		.build = offsetof(struct task_struct, nsproxy),
	},
	[DROID_LKM_MISC_F_MNTNS_USER_NS] = {
		.type = "mnt_namespace", .member = "user_ns",
	},
};

static struct ds_compat_type droid_lkm_misc_types[] = {
	{ .name = "task_struct", .build = sizeof(struct task_struct) },
};

static struct ds_compat_layout droid_lkm_misc_layout = {
	.module = "droid_lkm_misc",
	.fields = droid_lkm_misc_fields,
	.nfields = ARRAY_SIZE(droid_lkm_misc_fields),
	.types = droid_lkm_misc_types,
	.ntypes = ARRAY_SIZE(droid_lkm_misc_types),
};

/* 0 until the engine vouches for it, and 0 is what every reader treats as unusable */
u32 droid_lkm_misc_task_cred_off;
u32 droid_lkm_misc_task_real_cred_off;
u32 droid_lkm_misc_task_fs_off;
u32 droid_lkm_misc_task_nsproxy_off;
u32 droid_lkm_misc_mntns_user_ns_off;

static bool droid_lkm_misc_layout_trusted_flag;

bool droid_lkm_misc_layout_trusted(void)
{
	return droid_lkm_misc_layout_trusted_flag;
}

/*
 * the fields this module dereferences itself. user_ns is not one of them: the
 * owner work checks it on its own and reports itself unsupported, so losing it
 * costs the mount owner and not the namespace
 */
static const unsigned int droid_lkm_misc_required[] = {
	DROID_LKM_MISC_F_TASK_CRED,
	DROID_LKM_MISC_F_TASK_REAL_CRED,
	DROID_LKM_MISC_F_TASK_FS,
	DROID_LKM_MISC_F_TASK_NSPROXY,
};

void droid_lkm_misc_layout_init(void)
{
	unsigned int i;

	droid_lkm_misc_layout_trusted_flag = false;
	ds_compat_layout_init(&droid_lkm_misc_layout);

	droid_lkm_misc_task_cred_off =
		droid_lkm_misc_fields[DROID_LKM_MISC_F_TASK_CRED].live;
	droid_lkm_misc_task_real_cred_off =
		droid_lkm_misc_fields[DROID_LKM_MISC_F_TASK_REAL_CRED].live;
	droid_lkm_misc_task_fs_off =
		droid_lkm_misc_fields[DROID_LKM_MISC_F_TASK_FS].live;
	droid_lkm_misc_task_nsproxy_off =
		droid_lkm_misc_fields[DROID_LKM_MISC_F_TASK_NSPROXY].live;
	droid_lkm_misc_mntns_user_ns_off =
		droid_lkm_misc_fields[DROID_LKM_MISC_F_MNTNS_USER_NS].live;

	for (i = 0; i < ARRAY_SIZE(droid_lkm_misc_required); i++) {
		if (!ds_compat_layout_verified(
			    droid_lkm_misc_fields[droid_lkm_misc_required[i]].state)) {
			droid_lkm_misc_warn("layout: %s.%s is not verified, namespace features stay off\n",
					    droid_lkm_misc_fields[droid_lkm_misc_required[i]].type,
					    droid_lkm_misc_fields[droid_lkm_misc_required[i]].member);
			return;
		}
	}

	droid_lkm_misc_layout_trusted_flag = true;
}

void droid_lkm_misc_layout_report(void)
{
	ds_compat_layout_report(&droid_lkm_misc_layout);
}

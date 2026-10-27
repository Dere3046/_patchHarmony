// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/bitops.h>

#include "misc.h"
#include "misc_layout.h"
#include "type_info.h"
#include "btf.h"

u32 droid_lkm_misc_task_cred_off = offsetof(struct task_struct, cred);
u32 droid_lkm_misc_task_real_cred_off = offsetof(struct task_struct, real_cred);
u32 droid_lkm_misc_task_fs_off = offsetof(struct task_struct, fs);
u32 droid_lkm_misc_task_nsproxy_off = offsetof(struct task_struct, nsproxy);
u32 droid_lkm_misc_mntns_user_ns_off = 0;

static void droid_lkm_misc_ti_field(u32 type_id, const char *member, u32 *target, const char *name)
{
	u32 bit_off, bit_sz;

	if (ti_member_off(ti_base(), type_id, member, &bit_off, &bit_sz) == 0) {
		u32 byte_off = bit_off / 8;
		if (*target && *target != byte_off) {
			droid_lkm_misc_info("layout: %s offset differs: build=%u live=%u\n",
					    name, *target, byte_off);
		}
		*target = byte_off;
		droid_lkm_misc_dbg("layout: %s resolved to offset %u\n", name, byte_off);
	} else {
		droid_lkm_misc_warn("layout: %s member not found in BTF\n", name);
	}
}

int droid_lkm_misc_layout_init(void)
{
	struct ti_resolver res = { .name_to_addr = droid_lkm_misc_sym };
	u32 task_id, mntns_id;
	int ret;

	ret = ti_init(&res);
	if (ret) {
		droid_lkm_misc_warn("layout: Type_info init failed (%d), falling back to header offsets\n", ret);
		return ret;
	}

	if (!ti_btf_available()) {
		droid_lkm_misc_warn("layout: BTF not available, using header offsets\n");
		return -ENODATA;
	}

	if (ti_type_by_name(ti_base(), "task_struct", BIT(BTF_KIND_STRUCT), &task_id) == 0) {
		droid_lkm_misc_ti_field(task_id, "cred", &droid_lkm_misc_task_cred_off, "task_struct.cred");
		droid_lkm_misc_ti_field(task_id, "real_cred", &droid_lkm_misc_task_real_cred_off, "task_struct.real_cred");
		droid_lkm_misc_ti_field(task_id, "fs", &droid_lkm_misc_task_fs_off, "task_struct.fs");
		droid_lkm_misc_ti_field(task_id, "nsproxy", &droid_lkm_misc_task_nsproxy_off, "task_struct.nsproxy");
	} else {
		droid_lkm_misc_warn("layout: task_struct not found in BTF\n");
	}

	if (ti_type_by_name(ti_base(), "mnt_namespace", BIT(BTF_KIND_STRUCT), &mntns_id) == 0) {
		droid_lkm_misc_ti_field(mntns_id, "user_ns", &droid_lkm_misc_mntns_user_ns_off, "mnt_namespace.user_ns");
	} else {
		droid_lkm_misc_warn("layout: mnt_namespace not found in BTF\n");
	}

	droid_lkm_misc_info("layout: dynamic offsets ready (task_cred=%u, mntns_user_ns=%u)\n",
			    droid_lkm_misc_task_cred_off, droid_lkm_misc_mntns_user_ns_off);
	return 0;
}

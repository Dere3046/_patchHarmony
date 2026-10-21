// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046 / LKM contributors
 *
 * PTY Leakage Shield:
 * When container daemons (such as Droidspaces) allocate console PTYs from the host
 * mount namespace, slave devices (e.g. /dev/pts/2) appear in the host /dev/pts
 * with owner uid=0 (root) and SELinux label u:object_r:devpts:s0.
 *
 * On Android, normal apps (untrusted_app, UID >= 10000) scanning /dev/pts flag
 * any root-owned PTY as an immediate indicator of a running root container/daemon.
 *
 * This shield intercepts vfs_getattr(). For ordinary apps (UID >= 10000) outside the
 * container namespace, any root-owned PTY slave node (DEVPTS_SUPER_MAGIC, major 136)
 * has its reported stat->uid smoothly spoofed to AID_SHELL (2000).
 *
 * Legitimate container processes and host root processes retain the real uid=0 view.
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/stat.h>
#include <linux/dcache.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/major.h>
#include <linux/uidgid.h>
#include <uapi/linux/magic.h>

#include "misc.h"
#include "misc_proc.h"
#include "misc_scope.h"
#include "hk_inline.h"
#include "kcfi_safe.h"
#include "pts_shield.h"

#define AID_SHELL_UID 2000
#define AID_APP_START_UID 10000

#ifndef DEVPTS_SUPER_MAGIC
#define DEVPTS_SUPER_MAGIC 0x1cd1
#endif

#ifndef UNIX98_PTY_SLAVE_MAJOR
#define UNIX98_PTY_SLAVE_MAJOR 136
#endif

static struct hk_inline droid_lkm_pts_shield_getattr_hook;
static int (*droid_lkm_pts_shield_getattr_orig)(const struct path *path,
						struct kstat *stat,
						u32 request_mask,
						unsigned int query_flags);

__nocfi noinline int droid_lkm_pts_shield_getattr_wrap(const struct path *path,
						       struct kstat *stat,
						       u32 request_mask,
						       unsigned int query_flags)
{
	int ret;

	if (!droid_lkm_pts_shield_getattr_orig)
		return -EAGAIN;

	ret = droid_lkm_pts_shield_getattr_orig(path, stat, request_mask, query_flags);
	if (ret)
		return ret;

	/*
	 * Fast path: only inspect when the caller is an ordinary unprivileged app
	 * (AID_APP_START <= UID). Host system services, init, adbd and root tasks
	 * skip this check immediately.
	 */
	if (unlikely(__kuid_val(current_uid()) >= AID_APP_START_UID)) {
		if (stat && S_ISCHR(stat->mode) &&
		    __kuid_val(stat->uid) == 0 &&
		    MAJOR(stat->rdev) == UNIX98_PTY_SLAVE_MAJOR &&
		    path && path->dentry && path->dentry->d_sb &&
		    path->dentry->d_sb->s_magic == DEVPTS_SUPER_MAGIC &&
		    !droid_lkm_misc_is_container_task(current)) {
			stat->uid = KUIDT_INIT(AID_SHELL_UID);
		}
	}

	return 0;
}

int droid_lkm_misc_pts_shield_init(void)
{
	int ret;

	ret = hk_inline_hook(&droid_lkm_pts_shield_getattr_hook, "vfs_getattr",
			     "droid_lkm_pts_shield_getattr_wrap");
	if (ret) {
		droid_lkm_misc_warn("pts_shield: hook vfs_getattr failed: %d\n", ret);
		droid_lkm_misc_report("pts shield", "unsupported", "vfs_getattr hook failed");
		return ret;
	}
	droid_lkm_pts_shield_getattr_orig = (void *)droid_lkm_pts_shield_getattr_hook.orig;

	droid_lkm_misc_report("pts shield", "ready",
			      "spoof root PTY to AID_SHELL for ordinary apps");
	droid_lkm_misc_info("pts shield ready (root PTY spoof to AID_SHELL)\n");

	return 0;
}

void droid_lkm_misc_pts_shield_exit(void)
{
	if (droid_lkm_pts_shield_getattr_orig) {
		hk_inline_unhook(&droid_lkm_pts_shield_getattr_hook);
		droid_lkm_pts_shield_getattr_orig = NULL;
	}
}

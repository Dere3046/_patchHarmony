// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_CAPS_H
#define DROID_LKM_CAPS_H

#include <linux/types.h>

#define DROID_LKM_VERSION(_major, _minor, _patch) \
	((_major) * 65536 + (_minor) * 256 + (_patch))

/*
 * who serves a feature on the running kernel. the build target only fixes
 * shapes, so presence is answered here at load time: a kdir that carries
 * CONFIG_POSIX_MQUEUE=y says nothing about the kernel the module lands on, and
 * device trees turn the same configs off. deciding presence at build time is
 * what left the 6.1 device with neither the kernel's mqueue nor ours.
 */
enum droid_lkm_owner {
	DROID_LKM_ABSENT = 0,	/* nobody serves it, the call keeps the kernel's answer */
	DROID_LKM_KERNEL,	/* the running kernel serves it */
	DROID_LKM_MODULE,	/* the module serves it */
};

struct droid_lkm_feature {
	enum droid_lkm_owner owner;
	const char *reason;
};

/*
 * what the running kernel offers, decided once at load time before any hook is
 * installed. call sites ask these questions instead of testing a kernel
 * version, so one source serves every branch we ship without a conditional.
 */
struct droid_lkm_caps {
	unsigned int version;		/* utsname release, parsed */
	bool perm_takes_idmap;		/* inode_permission's first argument */
	void *idmap_none;		/* nop_mnt_idmap, or init_user_ns */
	bool mmap_takes_vm_flags;	/* do_mmap carries vm_flags */
	bool has_ns_count;		/* ns_common::count, absent on 5.10 */
	bool ctl_takes_table;		/* ctl_table_root::set_ownership has the table */
	struct droid_lkm_feature ipc_ns;
	struct droid_lkm_feature posix_mqueue;
	struct droid_lkm_feature sysvipc;
	struct droid_lkm_feature compat32;
};

extern struct droid_lkm_caps droid_lkm_caps;

const char *droid_lkm_owner_name(enum droid_lkm_owner owner);

/* fills the struct, and refuses to load on a kernel this build cannot serve */
int droid_lkm_caps_init(void);
void droid_lkm_caps_report(void);
void droid_lkm_caps_report_features(void);

#endif

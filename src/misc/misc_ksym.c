// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include "misc.h"
#include "misc_ksym.h"

struct droid_lkm_misc_ksym droid_lkm_misc_ks = { };

static bool droid_lkm_misc_thunk_missing;

static unsigned long __nocfi droid_lkm_misc_resolve(const char *name)
{
	return droid_lkm_misc_sym(name);
}

#define DROID_LKM_MISC_THUNK(_field, _name, _type)                                    \
	do {                                                                          \
		droid_lkm_misc_ks._field = (_type)droid_lkm_misc_resolve(_name);       \
		if (!droid_lkm_misc_ks._field) {                                      \
			droid_lkm_misc_warn("thunk missing: %s\n", _name);             \
			droid_lkm_misc_thunk_missing = true;                          \
		}                                                                     \
	} while (0)

#define DROID_LKM_MISC_THUNK_OPT(_field, _name, _type)                                \
	do {                                                                          \
		droid_lkm_misc_ks._field = (_type)droid_lkm_misc_resolve(_name);       \
		if (!droid_lkm_misc_ks._field)                                        \
			droid_lkm_misc_warn("optional thunk missing: %s\n", _name);    \
	} while (0)

int droid_lkm_misc_ksym_init(void)
{
	DROID_LKM_MISC_THUNK_OPT(xt_register_matches, "xt_register_matches",
		typeof(droid_lkm_misc_ks.xt_register_matches));
	DROID_LKM_MISC_THUNK_OPT(xt_unregister_matches, "xt_unregister_matches",
		typeof(droid_lkm_misc_ks.xt_unregister_matches));
	DROID_LKM_MISC_THUNK_OPT(inet_dev_addr_type, "inet_dev_addr_type",
		typeof(droid_lkm_misc_ks.inet_dev_addr_type));
	DROID_LKM_MISC_THUNK_OPT(ipv6_chk_addr, "ipv6_chk_addr",
		typeof(droid_lkm_misc_ks.ipv6_chk_addr));
	DROID_LKM_MISC_THUNK_OPT(__ipv6_addr_type, "__ipv6_addr_type",
		typeof(droid_lkm_misc_ks.__ipv6_addr_type));
	DROID_LKM_MISC_THUNK_OPT(__nf_ip6_route, "__nf_ip6_route",
		typeof(droid_lkm_misc_ks.__nf_ip6_route));

	DROID_LKM_MISC_THUNK(proc_alloc_inum, "proc_alloc_inum",
		typeof(droid_lkm_misc_ks.proc_alloc_inum));
	DROID_LKM_MISC_THUNK(proc_free_inum, "proc_free_inum",
		typeof(droid_lkm_misc_ks.proc_free_inum));
	DROID_LKM_MISC_THUNK(prepare_creds, "prepare_creds",
		typeof(droid_lkm_misc_ks.prepare_creds));
	DROID_LKM_MISC_THUNK(commit_creds, "commit_creds",
		typeof(droid_lkm_misc_ks.commit_creds));
	DROID_LKM_MISC_THUNK_OPT(key_put, "key_put", typeof(droid_lkm_misc_ks.key_put));
	DROID_LKM_MISC_THUNK(create_new_namespaces, "create_new_namespaces",
		typeof(droid_lkm_misc_ks.create_new_namespaces));

	droid_lkm_misc_ks.init_task = (typeof(droid_lkm_misc_ks.init_task))
		droid_lkm_misc_resolve("init_task");
	if (!droid_lkm_misc_ks.init_task) {
		droid_lkm_misc_warn("thunk missing: init_task\n");
		droid_lkm_misc_thunk_missing = true;
	}

	droid_lkm_misc_ks.nop_mnt_idmap = (typeof(droid_lkm_misc_ks.nop_mnt_idmap))
		droid_lkm_misc_resolve("nop_mnt_idmap");
	if (!droid_lkm_misc_ks.nop_mnt_idmap)
		droid_lkm_misc_warn("optional thunk missing: nop_mnt_idmap\n");

	/* exported by droid_lkm.ko, absent only means the ns kind cannot be offered */
	droid_lkm_misc_ks.ns_kind_register =
		(typeof(droid_lkm_misc_ks.ns_kind_register))
			droid_lkm_misc_resolve("droid_lkm_ns_kind_register");
	droid_lkm_misc_ks.ns_kind_unregister =
		(typeof(droid_lkm_misc_ks.ns_kind_unregister))
			droid_lkm_misc_resolve("droid_lkm_ns_kind_unregister");
	if (!droid_lkm_misc_ks.ns_kind_register ||
	    !droid_lkm_misc_ks.ns_kind_unregister)
		droid_lkm_misc_warn("droid_lkm not loaded, ns kind registration skipped\n");

	/* optional: without it the ns kind is registered without a reference on its owner */
	droid_lkm_misc_ks.module_address = (typeof(droid_lkm_misc_ks.module_address))
		droid_lkm_misc_resolve("__module_address");
	if (!droid_lkm_misc_ks.module_address)
		droid_lkm_misc_warn("optional thunk missing: __module_address\n");

	/* capability trace only, a kernel without it loses the trace and nothing else */
	droid_lkm_misc_ks.cap_capable = (typeof(droid_lkm_misc_ks.cap_capable))
		droid_lkm_misc_resolve("cap_capable");
	droid_lkm_misc_ks.security_capable = (typeof(droid_lkm_misc_ks.security_capable))
		droid_lkm_misc_resolve("security_capable");

	if (droid_lkm_misc_thunk_missing)
		return -ENOENT;

	return 0;
}

/*
 * probe list for symbols later stages need, verbose builds only, so a device side bisect
 * can tell a missing symbol from a failed hook
 * stage 2a user namespace, 2b none yet, 2c devtmpfs
 */
static const char *const droid_lkm_misc_probe_syms[] = {
	"ksys_unshare",
	"copy_creds",
	"capable_wrt_inode_uidgid",
	"copy_mnt_ns",
	"mntns_operations",
	"fs_context_for_mount",
	"cap_capable",
	"init_user_ns",
	"proc_tgid_base_lookup",
	"proc_tid_base_lookup",
	"proc_pid_make_inode",
	"name_to_int",
	"find_task_by_pid_ns",
	"pid_dentry_operations",
	"d_splice_alias",
	"d_set_d_op",
	/* the registration helper the pid dirs use; inlined away on this kernel */
	"proc_pid_make_base_inode",
	"cgroup_can_fork",
	"cgroup_cancel_fork",
	"cgroup_post_fork",
	"devcgroup_check_permission",
	"device_add",
	"device_del",
	"ramfs_init_fs_context",
	"ramfs_kill_sb",
	"init_special_inode",
};

void droid_lkm_misc_ksym_probe(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(droid_lkm_misc_probe_syms); i++)
		droid_lkm_misc_dbg("sym %-32s = 0x%lx\n", droid_lkm_misc_probe_syms[i],
			droid_lkm_misc_resolve(droid_lkm_misc_probe_syms[i]));
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "core.h"
#include "hk.h"
#include "hk_patch.h"
#include "ds.h"
#include "ds_caps.h"
#include "ds_ti.h"
#include "ds_ipcns.h"
#include "ds_ksym.h"
#include "ds_pidns.h"
#include "ds_proc.h"
#include "ds_slot.h"
#include "ds_status.h"
#include "ds_phase.h"
#include "ipc_mqueue_compat.h"

bool droid_lkm_verbose;

module_param_named(verbose, droid_lkm_verbose, bool, 0444);
MODULE_PARM_DESC(verbose, "log every hook entry (on-device crash bisect)");

static bool droid_lkm_mqueue_fs_up;

static const char *const droid_lkm_probe_syms[] = {
	"do_exit",
	"kernel_clone",
	"task_active_pid_ns",
	"find_ge_pid",
	"pid_task",
	"proc_alloc_inum",
	"proc_free_inum",
	"disable_pid_allocation",
	"group_send_sig_info",
	"kernel_wait4",
	"proc_ns_dir_inode_operations",
	"proc_pid_make_inode",
	"pid_dentry_operations",
	"do_mmap",
	"shmem_file_setup",
	"__arm64_sys_unshare",
	"__arm64_sys_clone",
	"__arm64_sys_clone3",
	"__arm64_sys_setns",
	"__arm64_sys_reboot",
	"sys_call_table",
	"create_new_namespaces",
	"copy_namespaces",
	"copy_process",
	"unshare_nsproxy_namespaces",
	"check_unshare_flags",
	"switch_task_namespaces",
};

static void droid_lkm_probe_symbols(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(droid_lkm_probe_syms); i++)
		droid_lkm_dbg("sym %-30s = 0x%lx\n", droid_lkm_probe_syms[i],
			droid_lkm_sym(droid_lkm_probe_syms[i]));
}

static void droid_lkm_mqueue_fs_teardown(void)
{
	if (droid_lkm_mqueue_fs_up) {
		droid_lkm_mqueue_fs_exit();
		droid_lkm_mqueue_fs_up = false;
	}
}

static unsigned long __nocfi droid_lkm_hk_resolve(const char *name)
{
	return droid_lkm_sym(name);
}

/*
 * the write path is chosen once, here. the slot path stores through an alias this
 * module maps itself, and MediaTek kernel protection trips on that store on this
 * device, which is what took it down, so the kernel's own primitive is the path
 * every write takes. the slot_on parameter brings the slot path back for a
 * kernel that allows it
 */
static struct hk_cfg droid_lkm_hk_cfg = {
	.resolve = droid_lkm_hk_resolve,
	.write = hk_write_kernel,
};

static int __init droid_lkm_init(void)
{
	int ret;

	find_kallsyms_base();
	if (!klnum_val || !kallrecon_klp) {
		droid_lkm_err("kallsyms recovery failed\n");
		return -ENODATA;
	}
	droid_lkm_info("loaded, klnum=%u\n", klnum_val);

	droid_lkm_info("params: verbose=%d skip_sysvipc=%d skip_do_exit=%d no_fake_ns=%d\n",
		droid_lkm_verbose, droid_lkm_slot_skip_sysvipc(), droid_lkm_pidns_skip_do_exit(),
		droid_lkm_slot_no_fake_ns());
	droid_lkm_probe_symbols();

	ret = droid_lkm_ksym_init();
	if (ret)
		return ret;

	droid_lkm_layout_init();

	ret = droid_lkm_caps_init();
	if (ret)
		return ret;
	droid_lkm_caps_report();

	ret = droid_lkm_munmap_init();
	if (ret)
		return ret;

	ret = hk_init(&droid_lkm_hk_cfg);
	if (ret) {
		droid_lkm_err("hk_init failed: %d\n", ret);
		return ret;
	}
	/*
	 * the policy has to reach the engine before the first hook: leaving this
	 * out keeps droid_lkm_inline_hooks_on at its zero initialised value and
	 * silently disables every inline hook, the nsproxy one included
	 */
	droid_lkm_hook_policy_apply();

	/*
	 * the wall goes up before anything new runs. everything above is the
	 * code the previous build already ran on this device, so a fault from
	 * here on is the part being debugged, and it has to be readable before
	 * the device goes down with it
	 */
	droid_lkm_guard_arm();

	/*
	 * the OPPO vendor fixup wants the engine, so it goes first. it used to
	 * be a ko that refused to load when its hook would not install; in here
	 * that refusal would take ipcns and the userns half down with a vendor
	 * quirk, so it reports and the load goes on
	 */
	droid_lkm_compat_phase_init();

	ret = droid_lkm_pidns_init();
	if (ret)
		goto err_compat;

	ret = droid_lkm_ipcns_init();
	if (ret)
		goto err_pidns;



	if (droid_lkm_mqueue_fs_init()) {
		droid_lkm_info("POSIX mqueue stays off: %s\n",
			       droid_lkm_caps.posix_mqueue.reason);
	} else {
		droid_lkm_mqueue_fs_up = true;
	}
	/* the mqueue owner is settled above, the rest were settled at caps_init */
	droid_lkm_caps_report_features();

	ret = droid_lkm_slot_init();
	if (ret)
		goto err_ipcns;

	ret = droid_lkm_proc_init();
	if (ret)
		goto err_slot;

	ret = droid_lkm_status_init();
	if (ret)
		droid_lkm_warn("NSpid emulation disabled: %d\n", ret);

	/*
	 * devtmpfs and the fake user namespace, and the last install site: the
	 * namespace kind the fake user namespace registers hangs off the /proc
	 * work droid_lkm_proc_init() just did
	 */
	droid_lkm_misc_phase_init();

	/* every install site has run by now, so this is the load's hook verdict */
	droid_lkm_hook_report();
	droid_lkm_layout_report();

	// the legacy container path is the state the device crashed in
	if (!droid_lkm_hook_critical_ok() && !droid_lkm_degrade_enabled()) {
		droid_lkm_err("a container critical hook did not install, refusing to load: pass degrade=1 to run without it\n");
		ret = -EOPNOTSUPP;
		goto err_slot;
	}

	droid_lkm_info("ready\n");
	return 0;

err_slot:
	droid_lkm_misc_phase_exit();
	droid_lkm_proc_exit();
	droid_lkm_slot_exit();
err_ipcns:
	droid_lkm_ipcns_exit();

	droid_lkm_mqueue_fs_teardown();
err_pidns:
	droid_lkm_pidns_exit();
err_compat:
	droid_lkm_compat_phase_exit();
	hk_exit();
	return ret;
}

static void __exit droid_lkm_exit(void)
{


	if (droid_lkm_pidns_busy() || droid_lkm_ipcns_busy())
		droid_lkm_warn("live container state at unload; those objects will be leaked\n");

	droid_lkm_status_exit();
	/* the namespace kinds it registered have to go before the /proc trees do */
	droid_lkm_misc_phase_exit();
	droid_lkm_proc_exit();
	droid_lkm_slot_exit();

	droid_lkm_task_ipc_deferred_run();
	droid_lkm_ipcns_exit();

	droid_lkm_mqueue_fs_teardown();
	droid_lkm_pidns_exit();
	droid_lkm_keepalive_release();
	droid_lkm_compat_phase_exit();
	droid_lkm_guard_disarm();
	hk_exit();
	droid_lkm_info("unloaded\n");
}

module_init(droid_lkm_init);
module_exit(droid_lkm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Droidspaces kernel feature shim for stock GKI: namespace emulation, devtmpfs, a fake user namespace and the OPPO vendor fixups");
MODULE_AUTHOR("dere3046");
MODULE_VERSION("0.2.0");

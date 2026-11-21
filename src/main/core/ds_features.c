// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "ds.h"
#include "ds_caps.h"
#include "ds_features.h"
#include "ds_hook.h"
#include "ds_ipcns.h"
#include "ds_pidns.h"
#include "ds_proc.h"
#include "ds_slot.h"
#include "ds_status.h"
#include "ds_ti.h"
#include "ipc_mqueue_compat.h"

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *droid_lkm_features_dir;

static void droid_lkm_feature_line(struct seq_file *m, const char *name,
				   const struct droid_lkm_feature *f)
{
	seq_printf(m, "%-20s %-7s %s\n", name, droid_lkm_owner_name(f->owner),
		   f->reason ? f->reason : "");
}

static int droid_lkm_features_hook(void *ctx, const struct ds_hook *h)
{
	struct seq_file *m = ctx;

	seq_printf(m, "%-20s %-7s %s\n", h->name, ds_hook_route_name(h->route),
		   h->live ? "installed" : "refused");
	return 0;
}

static int droid_lkm_features_show(struct seq_file *m, void *v)
{
	const struct droid_lkm_caps *caps = &droid_lkm_caps;
	const char *nspid_mode;
	int ret;

	seq_puts(m, "feature              owner   detail\n");
	droid_lkm_feature_line(m, "ipc_ns", &caps->ipc_ns);
	droid_lkm_feature_line(m, "posix_mqueue", &caps->posix_mqueue);
	droid_lkm_feature_line(m, "sysvipc", &caps->sysvipc);
	droid_lkm_feature_line(m, "compat32", &caps->compat32);
	seq_printf(m, "\nlayout trusted: %s\n",
		   droid_lkm_layout_trusted() ? "yes" : "no");
	droid_lkm_layout_show(m);
	seq_puts(m, "\nhook                 route   state\n");
	ret = ds_hook_enum(droid_lkm_features_hook, m);
	if (ret)
		return ret;

	if (!droid_lkm_status_ready())
		nspid_mode = "off";
	else if (droid_lkm_status_can_insert_pos())
		nspid_mode = "inline";
	else
		nspid_mode = "append";

	seq_puts(m, "\nruntime              state   detail\n");
	seq_printf(m, "%-20s %-7s\n", "module_ready",
		   droid_lkm_is_ready() ? "yes" : "no");
	seq_printf(m, "%-20s %-7s\n", "critical_hooks",
		   ds_hook_critical_ok() ? "ok" : "degraded");
	seq_printf(m, "%-20s %-7s\n", "degrade",
		   ds_hook_degrade_enabled() ? "yes" : "no");
	seq_printf(m, "%-20s %-7s\n", "gate",
		   droid_lkm_slot_gate_enabled() ? "gated" : "global");
	seq_printf(m, "%-20s %-7s\n", "hook_route_policy",
		   ds_hook_mode_name());
	seq_printf(m, "%-20s %-7s impl=%s backends=%s nsproxy=%d\n", "pidns",
		   droid_lkm_pidns_ready() ? "ready" : "off",
		   droid_lkm_pidns_impl_name(),
		   droid_lkm_pidns_backends(),
		   droid_lkm_pidns_nsproxy_enabled() ? 1 : 0);
	seq_printf(m, "%-20s %-7s\n", "ipcns",
		   droid_lkm_ipcns_ready() ? "ready" : "off");
	seq_printf(m, "%-20s %-7s shim=%s\n", "mqueue_fs",
		   droid_lkm_mqueue_fs_is_up() ? "up" : "off",
		   droid_lkm_mqueue_ready() ? "yes" : "no");
	seq_printf(m, "%-20s %-7s\n", "proc_ns",
		   droid_lkm_proc_ready() ? "ready" : "off");
	seq_printf(m, "%-20s %-7s insert=%s\n", "nspid_status",
		   droid_lkm_status_ready() ? "ready" : "off", nspid_mode);
	seq_printf(m, "%-20s %-7s clone=%s ipc64=%d/%d ipc32=%d/%d\n",
		   "syscall_slots",
		   droid_lkm_slot_ready() ? "ready" : "off",
		   droid_lkm_slot_clone_ready() ? "yes" : "no",
		   droid_lkm_slot_ipc_count(), droid_lkm_slot_ipc_total(),
		   droid_lkm_slot_ipc32_count(), droid_lkm_slot_ipc_total());
	return 0;
}

static int droid_lkm_features_open(struct inode *inode, struct file *file)
{
	return single_open(file, droid_lkm_features_show, NULL);
}

static const struct proc_ops droid_lkm_features_pops = {
	.proc_open = droid_lkm_features_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int droid_lkm_features_init(void)
{
	droid_lkm_features_dir = proc_mkdir("droid_lkm", NULL);
	if (!droid_lkm_features_dir)
		return -ENOMEM;
	if (!proc_create("features", 0444, droid_lkm_features_dir,
			 &droid_lkm_features_pops)) {
		remove_proc_entry("droid_lkm", NULL);
		droid_lkm_features_dir = NULL;
		return -ENOMEM;
	}
	return 0;
}

void droid_lkm_features_exit(void)
{
	if (!droid_lkm_features_dir)
		return;
	remove_proc_entry("features", droid_lkm_features_dir);
	remove_proc_entry("droid_lkm", NULL);
	droid_lkm_features_dir = NULL;
}
#else
int droid_lkm_features_init(void)
{
	return 0;
}

void droid_lkm_features_exit(void)
{
}
#endif

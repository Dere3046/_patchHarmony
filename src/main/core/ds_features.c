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

	seq_puts(m, "feature              owner   detail\n");
	droid_lkm_feature_line(m, "ipc_ns", &caps->ipc_ns);
	droid_lkm_feature_line(m, "posix_mqueue", &caps->posix_mqueue);
	droid_lkm_feature_line(m, "sysvipc", &caps->sysvipc);
	droid_lkm_feature_line(m, "compat32", &caps->compat32);
	seq_puts(m, "\nhook                 route   state\n");
	return ds_hook_enum(droid_lkm_features_hook, m);
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

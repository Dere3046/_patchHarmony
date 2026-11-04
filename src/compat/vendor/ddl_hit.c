// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Dere3046
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/threads.h>
#include <linux/types.h>

#include "core.h"
#include "ddl_hit.h"
#include "vendor_syms.h"
#include "hk_redir.h"

struct dlc_ddl_entry {
	char comm[TASK_COMM_LEN];
	u64 hit;
};

static struct hk_redir dlc_ddl_hook;

static void __nocfi noinline dlc_ddl_update_wrap(struct task_struct *p)
{
	struct dlc_ddl_entry *entry;
	void *base = dlc_vendor_syms.ddl_sdata;

	if (dlc_ddl_hook.how == HK_REDIR_KPROBE)
		hk_redir_leave(&dlc_ddl_hook);

	if (!p || !base || p->pid <= 0 || p->pid >= PID_MAX_DEFAULT)
		return;

	entry = (struct dlc_ddl_entry *)base + p->pid;
	if (!entry->comm[0] ||
	    strncmp(p->comm, entry->comm, strlen(p->comm))) {
		memset(entry, 0, sizeof(*entry));
		strscpy(entry->comm, p->comm, TASK_COMM_LEN);
	}
	entry->hit++;
}

int dlc_ddl_hit_init(void)
{
	struct hk_inline_probe probe;
	int ret;

	if (!dlc_vendor_syms.ddl_sdata || !dlc_vendor_syms.ddl_update)
		return 0;

	if (!hk_inline_probe("update_ddl_hit_history", &probe) &&
	    probe.state != HK_INLINE_PLAIN)
		pr_warn("[droid_lkm_vendor] ddl: entry state %d (%s), text route skipped\n",
			probe.state, probe.reason ? probe.reason : "?");
	else {
		ret = hk_redir_install(&dlc_ddl_hook, "update_ddl_hit_history",
				       "dlc_ddl_update_wrap", HK_REDIR_TEXT);
		if (!ret) {
			pr_info("[droid_lkm_vendor] ddl: update_ddl_hit_history on the text route\n");
			return 0;
		}
		pr_warn("[droid_lkm_vendor] ddl: text route refused %d, trying kprobe\n",
			ret);
	}

	ret = hk_redir_install_at(&dlc_ddl_hook,
				  (unsigned long)dlc_vendor_syms.ddl_update,
				  (unsigned long)dlc_ddl_update_wrap,
				  HK_REDIR_KPROBE);
	if (ret) {
		pr_warn("[droid_lkm_vendor] ddl: no route took update_ddl_hit_history (%d)\n",
			ret);
		return 0;
	}
	pr_info("[droid_lkm_vendor] ddl: update_ddl_hit_history on the kprobe route\n");
	return 0;
}

void dlc_ddl_hit_exit(void)
{
	if (!dlc_ddl_hook.live)
		return;

	hk_redir_remove(&dlc_ddl_hook);
	dlc_ddl_hook.live = false;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include "core.h"
#include "selftest.h"

static struct task_struct *(*dlc_probe_find_task_by_vpid)(pid_t nr);
static struct proc_dir_entry *dlc_probe_pde;

static int dlc_probe_vnr(struct task_struct *task)
{
	struct pid *pid = task->thread_pid;

	if (!pid || pid->level < 0)
		return -1;
	return pid->numbers[pid->level].nr;
}

static ssize_t dlc_probe_read(struct file *file, char __user *ubuf, size_t len,
			      loff_t *ppos)
{
	struct task_struct *task;
	char out[128];

	if (*ppos)
		return 0;
	if (!dlc_probe_find_task_by_vpid)
		return -ENODATA;

	task = dlc_probe_find_task_by_vpid(0x7ffffff0);
	snprintf(out, sizeof(out), "task=%px comm=%s pid=%d tgid=%d vnr=%d\n",
		 task, task ? task->comm : "(null)",
		 task ? (int)task->pid : -1, task ? (int)task->tgid : -1,
		 task ? dlc_probe_vnr(task) : -1);
	return simple_read_from_buffer(ubuf, len, ppos, out, strlen(out));
}

static const struct proc_ops dlc_probe_pops = {
	.proc_read = dlc_probe_read,
};

int dlc_selftest_init(void)
{
	struct proc_dir_entry *dir;

	if (!kallrecon_klp)
		return -ENODATA;
	dlc_probe_find_task_by_vpid =
		(typeof(dlc_probe_find_task_by_vpid))kallrecon_klp("find_task_by_vpid");
	if (!dlc_probe_find_task_by_vpid)
		return -ENOENT;

	dir = proc_mkdir("droid_lkm_compat", NULL);
	if (!dir)
		return -ENOMEM;
	dlc_probe_pde = proc_create("selftest", 0444, dir, &dlc_probe_pops);
	return dlc_probe_pde ? 0 : -ENOMEM;
}

void dlc_selftest_exit(void)
{
	if (dlc_probe_pde) {
		proc_remove(dlc_probe_pde);
		dlc_probe_pde = NULL;
	}
}

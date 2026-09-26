// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "ds.h"
#include "ds_ksym.h"
#include "ds_status.h"
#include "hk_inline.h"

static void (*droid_lkm_seq_puts_fn)(struct seq_file *m, const char *s);
static int (*droid_lkm_seq_write_fn)(struct seq_file *m, const void *p,
				     size_t size);
static void (*droid_lkm_seq_putc_fn)(struct seq_file *m, char c);
static void (*droid_lkm_seq_dec_fn)(struct seq_file *m, const char *delimiter,
				    unsigned long long num);

static pid_t (*droid_lkm_task_nr_ns_fn)(struct task_struct *task,
					enum pid_type type,
					struct pid_namespace *ns);

static struct hk_inline droid_lkm_status_hook;
static bool droid_lkm_status_hooked;
static int (*droid_lkm_status_orig)(struct seq_file *m, struct pid_namespace *ns,
				    struct pid *pid, struct task_struct *task);

static void droid_lkm_put_str(struct seq_file *m, const char *s)
{
	if (droid_lkm_seq_puts_fn)
		droid_lkm_seq_puts_fn(m, s);
	else
		droid_lkm_seq_write_fn(m, s, strlen(s));
}

static void droid_lkm_ns_line(struct seq_file *m, const char *tag,
			      struct task_struct *task, struct pid_namespace *ns,
			      struct pid *pid, enum pid_type type)
{
	int g;

	droid_lkm_put_str(m, tag);
	for (g = (int)ns->level; g <= (int)pid->level; g++)
		droid_lkm_seq_dec_fn(
			m, "\t",
			(unsigned long long)droid_lkm_task_nr_ns_fn(
				task, type, pid->numbers[g].ns));
}

__nocfi noinline int droid_lkm_status_wrap(struct seq_file *m,
					   struct pid_namespace *ns,
					   struct pid *pid,
					   struct task_struct *task)
{
	int ret;

	ret = droid_lkm_status_orig(m, ns, pid, task);
	if (ret || !task || !pid)
		return ret;

	/*
	 * upstream prints this block inside task_state() between Groups: and
	 * Kthread:. an inline hook can only append after the original function
	 * returns, so the block goes at the end of /proc/<pid>/status instead.
	 * the bytes of the block itself match fs/proc/array.c, and the values
	 * come from the same __task_pid_nr_ns() the kernel uses.
	 */
	droid_lkm_ns_line(m, "NStgid:", task, ns, pid, PIDTYPE_TGID);
	droid_lkm_ns_line(m, "\nNSpid:", task, ns, pid, PIDTYPE_PID);
	droid_lkm_ns_line(m, "\nNSpgid:", task, ns, pid, PIDTYPE_PGID);
	droid_lkm_ns_line(m, "\nNSsid:", task, ns, pid, PIDTYPE_SID);
	droid_lkm_seq_putc_fn(m, '\n');
	return ret;
}

// gki kernel has CONFIG_PID_NS off so proc_pid_status emits no NS lines
int droid_lkm_status_init(void)
{
	int ret;

	droid_lkm_seq_puts_fn = (void *)droid_lkm_sym("__seq_puts");
	droid_lkm_seq_write_fn = (void *)droid_lkm_sym("seq_write");
	droid_lkm_seq_putc_fn = (void *)droid_lkm_sym("seq_putc");
	droid_lkm_seq_dec_fn = (void *)droid_lkm_sym("seq_put_decimal_ull");
	droid_lkm_task_nr_ns_fn = (void *)droid_lkm_sym("__task_pid_nr_ns");

	if ((!droid_lkm_seq_puts_fn && !droid_lkm_seq_write_fn) ||
	    !droid_lkm_seq_putc_fn || !droid_lkm_seq_dec_fn ||
	    !droid_lkm_task_nr_ns_fn) {
		droid_lkm_warn("NSpid emulation skipped, helpers missing (puts=%p write=%p putc=%p dec=%p nr_ns=%p)\n",
			       droid_lkm_seq_puts_fn, droid_lkm_seq_write_fn,
			       droid_lkm_seq_putc_fn, droid_lkm_seq_dec_fn,
			       droid_lkm_task_nr_ns_fn);
		return -ENODATA;
	}

	ret = hk_inline_hook(&droid_lkm_status_hook, "proc_pid_status",
			     "droid_lkm_status_wrap");
	if (ret) {
		droid_lkm_warn("NSpid emulation skipped, proc_pid_status hook failed (%d)\n",
			       ret);
		return ret;
	}

	droid_lkm_status_orig =
		(typeof(droid_lkm_status_orig))droid_lkm_status_hook.orig;
	droid_lkm_status_hooked = true;
	droid_lkm_info("NSpid emulation: proc_pid_status 0x%lx wrapped (orig=0x%lx)\n",
		       droid_lkm_status_hook.addr, droid_lkm_status_hook.orig);
	return 0;
}

void droid_lkm_status_exit(void)
{
	if (droid_lkm_status_hooked) {
		hk_inline_unhook(&droid_lkm_status_hook);
		droid_lkm_status_hooked = false;
	}
}

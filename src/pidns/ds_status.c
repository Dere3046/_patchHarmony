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
#include "ds_ti.h"
#include "hk_inline.h"

// block inserted where task_state prints it
// task_state is inlined so a hook cannot reach inside
static bool droid_lkm_status_enable = true;
module_param_named(nspid, droid_lkm_status_enable, bool, 0444);
MODULE_PARM_DESC(nspid, "emulate the NStgid/NSpid/NSpgid/NSsid lines the kernel omits, on by default");

static bool droid_lkm_status_append;
module_param_named(nspid_append, droid_lkm_status_append, bool, 0444);
MODULE_PARM_DESC(nspid_append,
	"append the NS block at the end of the file instead of the position the kernel prints it in");

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

// insert stays off until init finds the seq_file offsets
static bool droid_lkm_status_can_insert;
static u32 droid_lkm_sf_buf, droid_lkm_sf_count, droid_lkm_sf_size;
static u32 droid_lkm_pid_level, droid_lkm_pid_numbers;
static u32 droid_lkm_upid_ns, droid_lkm_upid_stride;

static __nocfi noinline void droid_lkm_put_str(struct seq_file *m, const char *s)
{
	if (droid_lkm_seq_puts_fn)
		droid_lkm_seq_puts_fn(m, s);
	else
		droid_lkm_seq_write_fn(m, s, strlen(s));
}

static struct pid_namespace *droid_lkm_ns_at(struct pid *pid, int level)
{
	char *base = (char *)pid + droid_lkm_pid_numbers;

	return *(struct pid_namespace **)(base + level * droid_lkm_upid_stride +
					  droid_lkm_upid_ns);
}

static int droid_lkm_pid_level_of(struct pid *pid)
{
	return *(int *)((char *)pid + droid_lkm_pid_level);
}

static int droid_lkm_ns_level_of(struct pid_namespace *ns)
{
	return *(int *)((char *)ns + droid_lkm_layout_off(DROID_LKM_F_PID_NS_LEVEL));
}

// same bytes as array.c
// the trailing newline belongs to the caller
static __nocfi noinline int droid_lkm_ns_block(char *buf, size_t size,
					 struct task_struct *task,
			      struct pid_namespace *ns, struct pid *pid)
{
	static const struct {
		const char *tag;
		enum pid_type type;
	} lines[] = {
		{ "NStgid:", PIDTYPE_TGID },
		{ "NSpid:", PIDTYPE_PID },
		{ "NSpgid:", PIDTYPE_PGID },
		{ "NSsid:", PIDTYPE_SID },
	};
	int from = droid_lkm_ns_level_of(ns);
	int to = droid_lkm_pid_level_of(pid);
	int len = 0;
	unsigned int i;
	int g;

	for (i = 0; i < ARRAY_SIZE(lines); i++) {
		len += scnprintf(buf + len, size - len, "\n%s", lines[i].tag);
		for (g = from; g <= to; g++) {
			pid_t nr = droid_lkm_task_nr_ns_fn(task, lines[i].type,
							   droid_lkm_ns_at(pid, g));

			len += scnprintf(buf + len, size - len, "\t%d", nr);
		}
		if (len >= size)
			return -ENOSPC;
	}

	return len;
}

// append fallback
static __nocfi noinline void droid_lkm_ns_lines(struct seq_file *m,
					       struct task_struct *task,
			       struct pid_namespace *ns, struct pid *pid,
			       enum pid_type type)
{
	int from = droid_lkm_ns_level_of(ns);
	int to = droid_lkm_pid_level_of(pid);
	int g;

	for (g = from; g <= to; g++)
		droid_lkm_seq_dec_fn(m, "\t",
				     (unsigned long long)droid_lkm_task_nr_ns_fn(
					     task, type, droid_lkm_ns_at(pid, g)));
}

static void droid_lkm_status_append_block(struct seq_file *m,
					  struct task_struct *task,
					  struct pid_namespace *ns,
					  struct pid *pid)
{
	droid_lkm_put_str(m, "\nNStgid:");
	droid_lkm_ns_lines(m, task, ns, pid, PIDTYPE_TGID);
	droid_lkm_put_str(m, "\nNSpid:");
	droid_lkm_ns_lines(m, task, ns, pid, PIDTYPE_PID);
	droid_lkm_put_str(m, "\nNSpgid:");
	droid_lkm_ns_lines(m, task, ns, pid, PIDTYPE_PGID);
	droid_lkm_put_str(m, "\nNSsid:");
	droid_lkm_ns_lines(m, task, ns, pid, PIDTYPE_SID);
	droid_lkm_seq_putc_fn(m, '\n');
}

// insert before the newline that ends the Groups line
static bool droid_lkm_status_at(const char *buf, unsigned long count,
				unsigned long *at)
{
	unsigned long i;

	for (i = 0; i + 8 < count; i++) {
		if (buf[i] != '\n' || memcmp(buf + i + 1, "Groups:", 7))
			continue;
		for (i++; i < count; i++) {
			if (buf[i] == '\n') {
				*at = i;
				return true;
			}
		}
		break;
	}

	return false;
}

static bool droid_lkm_status_insert(struct seq_file *m, struct task_struct *task,
				    struct pid_namespace *ns, struct pid *pid)
{
	char block[512];
	char *buf;
	unsigned long count, size, at;
	int len;

	len = droid_lkm_ns_block(block, sizeof(block), task, ns, pid);
	if (len <= 0)
		return false;

	buf = *(char **)((char *)m + droid_lkm_sf_buf);
	count = *(unsigned long *)((char *)m + droid_lkm_sf_count);
	size = *(unsigned long *)((char *)m + droid_lkm_sf_size);

	if (!droid_lkm_status_at(buf, count, &at) || count + len > size)
		return false;

	memmove(buf + at + len, buf + at, count - at);
	memcpy(buf + at, block, len);
	*(unsigned long *)((char *)m + droid_lkm_sf_count) = count + len;

	return true;
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

	if (droid_lkm_status_can_insert &&
	    droid_lkm_status_insert(m, task, ns, pid))
		return ret;

	droid_lkm_status_append_block(m, task, ns, pid);
	return ret;
}

// gki kernel has CONFIG_PID_NS off so proc_pid_status emits no NS lines
int droid_lkm_status_init(void)
{
	int ret;

	if (!droid_lkm_status_enable) {
		droid_lkm_info("NSpid emulation off: nspid=0\n");
		return 0;
	}

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

	// struct pid offsets are needed by both paths
	// a zero offset here reads pid->count as a level
	if (!droid_lkm_layout_ok(DROID_LKM_F_PID_LEVEL) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_PID_NUMBERS) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_UPID_NS) ||
	    !droid_lkm_layout_ok(DROID_LKM_F_PID_NS_LEVEL) ||
	    !droid_lkm_layout_type_ok(DROID_LKM_T_UPID)) {
		droid_lkm_warn("NSpid emulation skipped, BTF does not describe struct pid\n");
		return -ENODATA;
	}

	droid_lkm_pid_level = droid_lkm_layout_off(DROID_LKM_F_PID_LEVEL);
	droid_lkm_pid_numbers = droid_lkm_layout_off(DROID_LKM_F_PID_NUMBERS);
	droid_lkm_upid_ns = droid_lkm_layout_off(DROID_LKM_F_UPID_NS);
	droid_lkm_upid_stride = droid_lkm_layout_type_size(DROID_LKM_T_UPID);

	if (droid_lkm_layout_ok(DROID_LKM_F_SEQ_FILE_BUF) &&
	    droid_lkm_layout_ok(DROID_LKM_F_SEQ_FILE_COUNT) &&
	    droid_lkm_layout_ok(DROID_LKM_F_SEQ_FILE_SIZE)) {
		droid_lkm_sf_buf = droid_lkm_layout_off(DROID_LKM_F_SEQ_FILE_BUF);
		droid_lkm_sf_count = droid_lkm_layout_off(DROID_LKM_F_SEQ_FILE_COUNT);
		droid_lkm_sf_size = droid_lkm_layout_off(DROID_LKM_F_SEQ_FILE_SIZE);
		droid_lkm_status_can_insert = !droid_lkm_status_append;
	}

	ret = droid_lkm_hook_install(&droid_lkm_status_hook, "proc_pid_status",
				     "droid_lkm_status_wrap");
	if (ret) {
		droid_lkm_warn("NSpid emulation skipped, proc_pid_status hook failed (%d)\n",
			       ret);
		return ret;
	}

	droid_lkm_status_orig =
		(typeof(droid_lkm_status_orig))droid_lkm_status_hook.orig;
	droid_lkm_status_hooked = true;

	if (droid_lkm_status_can_insert)
		droid_lkm_info("NSpid emulation: block goes where the kernel prints it, after Groups: (nspid_append=0)\n");
	else if (droid_lkm_status_append)
		droid_lkm_warn("NSpid emulation: block is appended at the end of the file (nspid_append=1)\n");
	else
		droid_lkm_warn("NSpid emulation: block is appended at the end, the seq_file offsets are not in BTF\n");
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

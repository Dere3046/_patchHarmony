// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/nsproxy.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/proc_ns.h>
#include <linux/user_namespace.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "ds_compat.h"
#include "anchor.h"

/* task_struct is the widest window the scans touch */
#define DS_ANCHOR_TASK_WIN	4096
#define DS_ANCHOR_NS_WIN	128
#define DS_ANCHOR_PID_WIN	128
#define DS_ANCHOR_PIDNS_WIN	256

static unsigned int ds_anchor_fault;
module_param_named(layout_fault, ds_anchor_fault, uint, 0444);
MODULE_PARM_DESC(layout_fault,
		 "refuse every anchor, a test hook that must leave the features off");

static int ds_anchor_shift;
module_param_named(layout_shift, ds_anchor_shift, int, 0444);
MODULE_PARM_DESC(layout_shift,
		 "shift the anchor symbols, a test hook that must leave the layout refused");

static unsigned long ds_anchor_sym(const char *name, long shift)
{
	unsigned long addr = ds_compat_sym(name);

	return addr ? addr + shift : 0;
}

static bool ds_anchor_rd64(unsigned long addr, unsigned long *v)
{
	return !copy_from_kernel_nofault(v, (void *)addr, sizeof(*v));
}

static bool ds_anchor_rd32(unsigned long addr, u32 *v)
{
	return !copy_from_kernel_nofault(v, (void *)addr, sizeof(*v));
}

/* the anchor result is used directly as the live offset */
static bool ds_anchor_set(struct ds_compat_layout *l, const char *type,
			  const char *member, u32 off)
{
	unsigned int i;

	for (i = 0; i < l->nfields; i++) {
		if (!strcmp(l->fields[i].type, type) &&
		    !strcmp(l->fields[i].member, member)) {
			l->fields[i].live = off;
			l->fields[i].state = DS_COMPAT_VERIFIED;
			return true;
		}
	}
	return false;
}

static void ds_anchor_type(struct ds_compat_layout *l, const char *name,
			   u32 size)
{
	unsigned int i;

	for (i = 0; i < l->ntypes; i++) {
		if (!strcmp(l->types[i].name, name)) {
			l->types[i].live = size;
			l->types[i].state = DS_COMPAT_VERIFIED;
			return;
		}
	}
}

/* module owned structs keep the build layout */
static void ds_anchor_keep_build(struct ds_compat_layout *l, const char *type)
{
	unsigned int i;

	for (i = 0; i < l->nfields; i++) {
		if (!strcmp(l->fields[i].type, type)) {
			l->fields[i].live = l->fields[i].build;
			l->fields[i].state = DS_COMPAT_BUILD;
		}
	}
	for (i = 0; i < l->ntypes; i++) {
		if (!strcmp(l->types[i].name, type) && l->types[i].build) {
			l->types[i].live = l->types[i].build;
			l->types[i].state = DS_COMPAT_BUILD;
		}
	}
}

static bool ds_anchor_find_ptr(unsigned long base, unsigned long win,
			       unsigned long want, u32 *off)
{
	unsigned long v;
	u32 i;
	bool found = false;
	u32 at = 0;

	for (i = 0; i + 8 <= win; i += 8) {
		if (!ds_anchor_rd64(base + i, &v) || v != want)
			continue;
		if (found && at != i)
			return false;
		found = true;
		at = i;
	}
	if (!found)
		return false;
	*off = at;
	return true;
}

static bool ds_anchor_find_u32(unsigned long base, unsigned long win,
			       u32 want, u32 *off)
{
	u32 v;
	u32 i;
	bool found = false;
	u32 at = 0;

	for (i = 0; i + 4 <= win; i += 4) {
		if (!ds_anchor_rd32(base + i, &v) || v != want)
			continue;
		if (found && at != i)
			return false;
		found = true;
		at = i;
	}
	if (!found)
		return false;
	*off = at;
	return true;
}

static __nocfi noinline char *ds_anchor_call_get_comm(char *to, size_t len,
						 struct task_struct *task)
{
	unsigned long fn = ds_compat_sym("__get_task_comm");

	if (!fn)
		return NULL;
	return ((char *(*)(char *, size_t, struct task_struct *))fn)(to, len,
								    task);
}

static __nocfi noinline pid_t ds_anchor_call_pid_nr(struct task_struct *task,
						    enum pid_type type,
						    unsigned long ns)
{
	unsigned long fn = ds_compat_sym("__task_pid_nr_ns");

	if (!fn || !ns)
		return -1;
	return ((pid_t (*)(struct task_struct *, enum pid_type,
			   struct pid_namespace *))fn)(task, type,
						       (void *)ns);
}

static bool ds_anchor_task(struct ds_compat_layout *l, unsigned long init_task,
			   unsigned long init_nsproxy, unsigned long init_cred,
			   unsigned long init_struct_pid,
			   unsigned long init_signals, unsigned long init_sighand,
			   unsigned long init_fs,
			   u32 *task_pid_off, u32 *task_comm_off)
{
	struct ti_boot_args args;
	struct ti_task_offs offs;
	unsigned long v, init_pid_ns;
	char comm[16];
	u32 off_ns = 0, off_tp = 0, off_sig = 0, off_sighand = 0, off_fs = 0;
	u32 off_flags = 0;
	bool ok = true;

	if (ds_anchor_fault)
		return false;

	memset(&args, 0, sizeof(args));
	memset(&offs, 0, sizeof(offs));
	init_pid_ns = ds_compat_sym("init_pid_ns");
	if (!init_pid_ns || !ds_anchor_call_get_comm((char *)args.comm,
						     sizeof(args.comm),
						     current))
		return false;
	args.pid = ds_anchor_call_pid_nr(current, PIDTYPE_PID, init_pid_ns);
	args.tgid = ds_anchor_call_pid_nr(current, PIDTYPE_TGID, init_pid_ns);
	if (args.pid < 0 || args.tgid < 0)
		return false;
	/* another task as the cross check, init itself when insmod runs elsewhere */
	args.ref_pid = args.pid == 1 ? 2 : 1;
	args.ref_tgid = args.ref_pid;
	ti_bootstrap_task(&args, &offs);

	if (!offs.off_comm || !offs.off_pid)
		return false;
	*task_pid_off = offs.off_pid;
	*task_comm_off = offs.off_comm;
	if (!ds_anchor_rd64(init_task + offs.off_comm, &v))
		return false;
	memcpy(comm, &v, 8);
	if (memcmp(comm, "swapper", 7))
		return false;
	if (!ds_anchor_rd64(init_task + offs.off_pid, &v) || v)
		return false;
	if (!ds_anchor_rd64(init_task + offs.off_real_cred, &v) || v != init_cred)
		return false;
	if (!ds_anchor_rd64(init_task + offs.off_cred, &v) || v != init_cred)
		return false;

	ds_anchor_find_ptr(init_task, DS_ANCHOR_TASK_WIN, init_nsproxy, &off_ns);
	ds_anchor_find_ptr(init_task, DS_ANCHOR_TASK_WIN, init_struct_pid, &off_tp);

	ok &= ds_anchor_set(l, "task_struct", "comm", offs.off_comm);
	ok &= ds_anchor_set(l, "task_struct", "pid", offs.off_pid);
	ok &= ds_anchor_set(l, "task_struct", "tgid", offs.off_pid + 4);
	ok &= ds_anchor_set(l, "task_struct", "real_cred", offs.off_real_cred);
	ok &= ds_anchor_set(l, "task_struct", "cred", offs.off_cred);
	if (off_ns)
		ok &= ds_anchor_set(l, "task_struct", "nsproxy", off_ns);
	if (off_tp)
		ok &= ds_anchor_set(l, "task_struct", "thread_pid", off_tp);
	if (offs.off_tasks)
		ok &= ds_anchor_set(l, "task_struct", "tasks", offs.off_tasks);

	/* ti_bootstrap_task already validated the mm candidate with TASK_SIZE */
	if (offs.off_mm)
		ok &= ds_anchor_set(l, "task_struct", "mm", offs.off_mm);
	if (init_fs &&
	    ds_anchor_find_ptr(init_task, DS_ANCHOR_TASK_WIN, init_fs, &off_fs))
		ok &= ds_anchor_set(l, "task_struct", "fs", off_fs);

	if (init_signals &&
	    ds_anchor_find_ptr(init_task, DS_ANCHOR_TASK_WIN, init_signals, &off_sig))
		ok &= ds_anchor_set(l, "task_struct", "signal", off_sig);
	if (init_sighand &&
	    ds_anchor_find_ptr(init_task, DS_ANCHOR_TASK_WIN, init_sighand, &off_sighand))
		ok &= ds_anchor_set(l, "task_struct", "sighand", off_sighand);

	if (ds_anchor_find_u32(init_task, DS_ANCHOR_TASK_WIN,
			       PF_KTHREAD | PF_IDLE, &off_flags))
		ok &= ds_anchor_set(l, "task_struct", "flags", off_flags);

	return ok;
}

static bool ds_anchor_nsproxy(struct ds_compat_layout *l, unsigned long init_nsproxy,
			      unsigned long init_uts, unsigned long init_net,
			      unsigned long init_pid_ns)
{
	u32 off_uts = 0, off_net = 0, off_pid = 0;
	unsigned long v;
	u32 off_ipc, off_mnt;
	bool ok = true;

	if (ds_anchor_fault)
		return false;

	/*
	 * uts, ipc, mnt, pid and net are the first five pointers, the config
	 * gated namespaces come after net, so the three known ones pin the block
	 */
	if (!ds_anchor_find_ptr(init_nsproxy, DS_ANCHOR_NS_WIN, init_uts, &off_uts) ||
	    !ds_anchor_find_ptr(init_nsproxy, DS_ANCHOR_NS_WIN, init_net, &off_net) ||
	    !ds_anchor_find_ptr(init_nsproxy, DS_ANCHOR_NS_WIN, init_pid_ns, &off_pid))
		return false;
	off_ipc = off_uts + 8;
	off_mnt = off_uts + 16;
	if (off_pid != off_uts + 24 || off_net != off_uts + 32)
		return false;
	if (!ds_anchor_rd64(init_nsproxy + off_mnt, &v) || !v)
		return false;

	ok &= ds_anchor_set(l, "nsproxy", "uts_ns", off_uts);
	ok &= ds_anchor_set(l, "nsproxy", "ipc_ns", off_ipc);
	ok &= ds_anchor_set(l, "nsproxy", "mnt_ns", off_mnt);
	ok &= ds_anchor_set(l, "nsproxy", "pid_ns_for_children", off_uts + 24);
	ok &= ds_anchor_set(l, "nsproxy", "net_ns", off_uts + 32);
	return ok;
}

static bool ds_anchor_find_u32_pair(unsigned long base, unsigned long win,
				    u32 first, u32 second, u32 *off)
{
	u32 a, b;
	u32 i;
	bool found = false;
	u32 at = 0;

	for (i = 0; i + 8 <= win; i += 4) {
		if (!ds_anchor_rd32(base + i, &a) ||
		    !ds_anchor_rd32(base + i + 4, &b))
			continue;
		if (a != first || b != second)
			continue;
		if (found && at != i)
			return false;
		found = true;
		at = i;
	}
	if (!found)
		return false;
	*off = at + 4;
	return true;
}

static bool ds_anchor_pid(struct ds_compat_layout *l, unsigned long init_struct_pid,
			  unsigned long init_pid_ns)
{
	u32 off_ns = 0, off_nr, off_level;
	unsigned int i;
	u32 v;
	bool ok = true;

	if (ds_anchor_fault)
		return false;

	if (!ds_anchor_find_ptr(init_struct_pid, DS_ANCHOR_PID_WIN, init_pid_ns, &off_ns))
		return false;
	if (off_ns < 8)
		return false;
	off_nr = off_ns - 8;
	if (!ds_anchor_rd32(init_struct_pid + off_nr, &v) || v)
		return false;

	/* init_struct_pid carries numbers[0], nr then the namespace pointer */
	ok &= ds_anchor_set(l, "upid", "nr", 0);
	ok &= ds_anchor_set(l, "upid", "ns", 8);
	ok &= ds_anchor_set(l, "pid", "numbers", off_nr);
	for (i = 0; i < l->ntypes; i++) {
		u32 size;

		if (strcmp(l->types[i].name, "upid"))
			continue;
		size = sizeof(struct upid);
		ds_anchor_type(l, "upid", size);
	}
	/* init_struct_pid.count is 1 and level is 0 right after it */
	if (ds_anchor_find_u32_pair(init_struct_pid, DS_ANCHOR_PID_WIN,
				    1, 0, &off_level))
		ok &= ds_anchor_set(l, "pid", "level", off_level);
	return ok;
}

static bool ds_anchor_find_task(unsigned long base, unsigned long win,
				u32 pid_off, u32 comm_off, pid_t pid, u32 *off)
{
	unsigned long cand, cv;
	u32 p, tgid;
	char comm[16];
	u32 i;
	bool found = false;
	u32 at = 0;

	for (i = 0; i + 8 <= win; i += 8) {
		if (!ds_anchor_rd64(base + i, &cand) || !cand)
			continue;
		if (!ds_anchor_rd32(cand + pid_off, &p) ||
		    !ds_anchor_rd32(cand + pid_off + 4, &tgid) ||
		    p != (u32)pid || tgid != (u32)pid)
			continue;
		if (!ds_anchor_rd64(cand + comm_off, &cv))
			continue;
		memcpy(comm, &cv, 8);
		if (!comm[0] || !memchr(comm, 0, sizeof(comm)))
			continue;
		if (found && at != i)
			return false;
		found = true;
		at = i;
	}
	if (!found)
		return false;
	*off = at;
	return true;
}

static bool ds_anchor_pidns(struct ds_compat_layout *l, unsigned long init_pid_ns,
			    unsigned long init_user_ns, u32 task_pid_off,
			    u32 task_comm_off)
{
	u32 off_user = 0, off_inum = 0;
	u32 off_ns, off_level, off_parent, off_alloc, off_ucounts, off_reboot;
	u32 off_idr = 8, off_cachep, off_reaper = 0;
	unsigned long v;
	u32 u;
	bool ok = true;

	if (ds_anchor_fault)
		return false;

	if (!init_user_ns || !init_pid_ns || !task_pid_off)
		return false;

	if (!ds_anchor_find_ptr(init_pid_ns, DS_ANCHOR_PIDNS_WIN, init_user_ns,
				&off_user)) {
		return false;
	}

	/*
	 * init_pid_ns starts with child_reaper pointing at init_task, but the
	 * first pid 1 fork replaces it with that task. identify the slot by
	 * asking the task's own pid field, which itself was cross checked
	 */
	if (!ds_anchor_find_task(init_pid_ns, DS_ANCHOR_PIDNS_WIN, task_pid_off,
				 task_comm_off, 1, &off_reaper)) {
		return false;
	}

	/* init_pid_ns.ns.inum is PROC_PID_INIT_INO */
	if (!ds_anchor_find_u32(init_pid_ns, DS_ANCHOR_PIDNS_WIN,
				PROC_PID_INIT_INO, &off_inum)) {
		return false;
	}
	if (off_inum < offsetof(struct ns_common, inum))
		return false;
	off_ns = off_inum - offsetof(struct ns_common, inum);

	/* child_reaper, pid_cachep, level then parent */
	off_cachep = off_reaper + 8;
	off_level = off_reaper + 16;
	off_parent = off_level + 8;
	off_alloc = off_reaper - 8;
	off_ucounts = off_user + 8;
	off_reboot = off_user + 16;

	if (!ds_anchor_rd32(init_pid_ns + off_level, &u) || u) {
		return false;
	}
	if (!ds_anchor_rd64(init_pid_ns + off_parent, &v) || v) {
		return false;
	}
	if (!ds_anchor_rd32(init_pid_ns + off_alloc, &u) ||
	    (u & PIDNS_ADDING) != PIDNS_ADDING) {
		return false;
	}
	if (!ds_anchor_rd64(init_pid_ns + off_ucounts, &v) || v) {
		return false;
	}
	if (!ds_anchor_rd32(init_pid_ns + off_reboot, &u) || u) {
		return false;
	}

	ok &= ds_anchor_set(l, "pid_namespace", "idr", off_idr);
	ok &= ds_anchor_set(l, "pid_namespace", "user_ns", off_user);
	ok &= ds_anchor_set(l, "pid_namespace", "child_reaper", off_reaper);
	ok &= ds_anchor_set(l, "pid_namespace", "pid_cachep", off_cachep);
	ok &= ds_anchor_set(l, "pid_namespace", "ns", off_ns);
	ok &= ds_anchor_set(l, "pid_namespace", "level", off_level);
	ok &= ds_anchor_set(l, "pid_namespace", "parent", off_parent);
	ok &= ds_anchor_set(l, "pid_namespace", "pid_allocated", off_alloc);
	ok &= ds_anchor_set(l, "pid_namespace", "ucounts", off_ucounts);
	ok &= ds_anchor_set(l, "pid_namespace", "reboot", off_reboot);
	ds_anchor_type(l, "pid_namespace",
		       off_ns + sizeof(struct ns_common));
	return ok;
}

bool ds_compat_layout_anchor(struct ds_compat_layout *l)
{
	unsigned long init_task, init_nsproxy, init_cred, init_struct_pid;
	unsigned long init_pid_ns, init_uts, init_net, init_user_ns;
	unsigned long init_signals, init_sighand, init_fs;
	bool task, nsproxy, pid, pidns;
	u32 task_pid_off = 0, task_comm_off = 0;
	unsigned int i;
	long shift = ds_anchor_shift;

	if (!l || !l->fields || !l->types)
		return false;

	/* a nonzero shift makes every symbol point into the wrong object */
	init_task = ds_anchor_sym("init_task", shift);
	init_nsproxy = ds_anchor_sym("init_nsproxy", shift);
	init_cred = ds_anchor_sym("init_cred", shift);
	init_struct_pid = ds_anchor_sym("init_struct_pid", shift);
	init_pid_ns = ds_anchor_sym("init_pid_ns", shift);
	init_uts = ds_anchor_sym("init_uts_ns", shift);
	init_net = ds_anchor_sym("init_net", shift);
	init_user_ns = ds_anchor_sym("init_user_ns", shift);
	init_signals = ds_anchor_sym("init_signals", shift);
	init_sighand = ds_anchor_sym("init_sighand", shift);
	init_fs = ds_anchor_sym("init_fs", shift);

	ds_anchor_keep_build(l, "ipc_namespace");
	ds_anchor_keep_build(l, "seq_file");

	task = init_task && init_nsproxy && init_cred && init_struct_pid &&
	       ds_anchor_task(l, init_task, init_nsproxy, init_cred,
			      init_struct_pid, init_signals, init_sighand,
			      init_fs, &task_pid_off, &task_comm_off);
	nsproxy = init_nsproxy && init_uts && init_net && init_pid_ns &&
		  ds_anchor_nsproxy(l, init_nsproxy, init_uts, init_net, init_pid_ns);
	pid = init_struct_pid && init_pid_ns &&
	      ds_anchor_pid(l, init_struct_pid, init_pid_ns);
	pidns = ds_anchor_pidns(l, init_pid_ns, init_user_ns, task_pid_off,
				task_comm_off);

	for (i = 0; i < l->nfields; i++) {
		if (l->fields[i].state == DS_COMPAT_ANCHORED ||
		    l->fields[i].state == DS_COMPAT_VERIFIED ||
		    l->fields[i].state == DS_COMPAT_BUILD)
			continue;
		l->fields[i].live = 0;
		l->fields[i].state = DS_COMPAT_UNVERIFIED;
	}

	pr_info("[%s] layout: no BTF, anchors task=%d nsproxy=%d pid=%d pidns=%d, module owned structs keep the build layout\n",
		l->module, task, nsproxy, pid, pidns);
	return task || nsproxy || pid || pidns;
}

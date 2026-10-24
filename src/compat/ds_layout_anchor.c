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

static bool ds_anchor_build_off(struct ds_compat_layout *l, const char *type,
				const char *member, u32 *off)
{
	unsigned int i;

	for (i = 0; i < l->nfields; i++) {
		if (!strcmp(l->fields[i].type, type) &&
		    !strcmp(l->fields[i].member, member)) {
			*off = l->fields[i].build;
			return true;
		}
	}
	return false;
}

/* an anchored offset is only usable while it agrees with the build layout */
static bool ds_anchor_set(struct ds_compat_layout *l, const char *type,
			  const char *member, u32 off)
{
	u32 build;
	unsigned int i;

	if (!ds_anchor_build_off(l, type, member, &build))
		return false;

	for (i = 0; i < l->nfields; i++) {
		if (!strcmp(l->fields[i].type, type) &&
		    !strcmp(l->fields[i].member, member)) {
			if (build != off) {
				l->fields[i].live = 0;
				l->fields[i].state = DS_COMPAT_DIFFERS;
				pr_warn("[%s] anchor %s.%s at %u, the build uses %u\n",
					l->module, type, member, off, build);
				return false;
			}
			l->fields[i].live = off;
			l->fields[i].state = DS_COMPAT_ANCHORED;
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
			if (l->types[i].build && l->types[i].build != size) {
				l->types[i].live = 0;
				l->types[i].state = DS_COMPAT_DIFFERS;
				return;
			}
			l->types[i].live = size;
			l->types[i].state = DS_COMPAT_ANCHORED;
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

static bool ds_anchor_task(struct ds_compat_layout *l, unsigned long init_task,
			   unsigned long init_nsproxy, unsigned long init_cred,
			   unsigned long init_struct_pid,
			   unsigned long init_signals, unsigned long init_sighand)
{
	struct ti_boot_args args;
	struct ti_task_offs offs;
	unsigned long v;
	char comm[16];
	u32 off_ns = 0, off_tp = 0, off_sig = 0, off_sighand = 0, off_flags = 0;
	u32 build_comm, build_pid, build_cred, build_mm, build_flags;
	bool ok = true;

	if (ds_anchor_fault)
		return false;

	memset(&args, 0, sizeof(args));
	memset(&offs, 0, sizeof(offs));
	memcpy((void *)args.comm, current->comm, sizeof(args.comm));
	args.pid = current->pid;
	args.tgid = current->tgid;
	/* another task as the cross check, init itself when insmod runs elsewhere */
	args.ref_pid = current->pid == 1 ? 2 : 1;
	args.ref_tgid = args.ref_pid;
	ti_bootstrap_task(&args, &offs);

	if (!offs.off_comm || !offs.off_pid)
		return false;
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

	if (!ds_anchor_build_off(l, "task_struct", "comm", &build_comm) ||
	    !ds_anchor_build_off(l, "task_struct", "pid", &build_pid) ||
	    !ds_anchor_build_off(l, "task_struct", "cred", &build_cred) ||
	    !ds_anchor_build_off(l, "task_struct", "mm", &build_mm))
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

	/*
	 * the scan runs on current, so its mm slot is the one holding the
	 * compiler's current->mm. a kernel thread has none and the field stays
	 * refused, which is the right answer for a task without an mm
	 */
	{
		unsigned long mm = (unsigned long)current->mm;

		if (offs.off_mm && mm &&
		    ds_anchor_rd64((unsigned long)current + offs.off_mm, &v) &&
		    v == mm)
			ok &= ds_anchor_set(l, "task_struct", "mm", offs.off_mm);
	}

	if (init_signals &&
	    ds_anchor_find_ptr(init_task, DS_ANCHOR_TASK_WIN, init_signals, &off_sig))
		ok &= ds_anchor_set(l, "task_struct", "signal", off_sig);
	if (init_sighand &&
	    ds_anchor_find_ptr(init_task, DS_ANCHOR_TASK_WIN, init_sighand, &off_sighand))
		ok &= ds_anchor_set(l, "task_struct", "sighand", off_sighand);

	if (ds_anchor_build_off(l, "task_struct", "flags", &build_flags) &&
	    ds_anchor_find_u32(init_task, DS_ANCHOR_TASK_WIN,
			       PF_KTHREAD | PF_IDLE, &off_flags) &&
	    off_flags == build_flags)
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
		size = l->types[i].build ? l->types[i].build : 16;
		ds_anchor_type(l, "upid", size);
	}
	off_level = 0;
	for (i = 0; i < l->nfields; i++) {
		if (!strcmp(l->fields[i].type, "pid") &&
		    !strcmp(l->fields[i].member, "level"))
			off_level = l->fields[i].build;
	}
	if (off_level && off_level < off_nr &&
	    ds_anchor_rd32(init_struct_pid + off_level, &v) && !v)
		ok &= ds_anchor_set(l, "pid", "level", off_level);
	return ok;
}

static bool ds_anchor_pidns(struct ds_compat_layout *l, unsigned long init_pid_ns,
			    unsigned long init_user_ns)
{
	u32 off_user = 0, off_inum = 0;
	u32 off_ns, off_level, off_parent, off_alloc, off_ucounts;
	u32 build_user, build_reaper, build_ns, build_level;
	u32 build_parent, build_alloc, build_ucounts;
	u32 build_task_pid, build_task_comm;
	unsigned long reaper, v;
	u32 u;
	bool ok = true;

	if (ds_anchor_fault)
		return false;

	if (!init_user_ns || !init_pid_ns)
		return false;

	if (!ds_anchor_build_off(l, "pid_namespace", "user_ns", &build_user) ||
	    !ds_anchor_build_off(l, "pid_namespace", "child_reaper", &build_reaper) ||
	    !ds_anchor_build_off(l, "pid_namespace", "ns", &build_ns) ||
	    !ds_anchor_build_off(l, "pid_namespace", "level", &build_level) ||
	    !ds_anchor_build_off(l, "pid_namespace", "parent", &build_parent) ||
	    !ds_anchor_build_off(l, "pid_namespace", "pid_allocated", &build_alloc) ||
	    !ds_anchor_build_off(l, "pid_namespace", "ucounts", &build_ucounts) ||
	    !ds_anchor_build_off(l, "task_struct", "pid", &build_task_pid) ||
	    !ds_anchor_build_off(l, "task_struct", "comm", &build_task_comm))
		return false;

	if (!ds_anchor_find_ptr(init_pid_ns, DS_ANCHOR_PIDNS_WIN, init_user_ns,
				&off_user))
		return false;

	/*
	 * init_pid_ns starts with child_reaper pointing at init_task, but the
	 * first pid 1 fork replaces it with that task. the slot still has to
	 * name a task, so the value is checked as one instead of as init_task
	 */
	if (!ds_anchor_rd64(init_pid_ns + build_reaper, &reaper) || !reaper)
		return false;
	if (!ds_anchor_rd32(reaper + build_task_pid, &u) || u != 1)
		return false;
	if (!ds_anchor_rd64(reaper + build_task_comm, &v))
		return false;
	if (!memchr(&v, 0, sizeof(v))) {
		if (!ds_anchor_rd64(reaper + build_task_comm + 8, &v))
			return false;
		if (!memchr(&v, 0, sizeof(v)))
			return false;
	}

	/* init_pid_ns.ns.inum is PROC_PID_INIT_INO */
	if (!ds_anchor_find_u32(init_pid_ns, DS_ANCHOR_PIDNS_WIN,
				PROC_PID_INIT_INO, &off_inum))
		return false;
	if (off_inum < offsetof(struct ns_common, inum))
		return false;
	off_ns = off_inum - offsetof(struct ns_common, inum);

	/* child_reaper, pid_cachep, level then parent, the config flags do not move them */
	off_level = build_reaper + 16;
	off_parent = off_level + 8;
	off_alloc = build_reaper - 8;
	off_ucounts = off_user + 8;
	if (off_level != build_level || off_parent != build_parent ||
	    off_alloc != build_alloc || off_ucounts != build_ucounts)
		return false;
	if (off_ns != build_ns)
		return false;

	if (!ds_anchor_rd32(init_pid_ns + off_level, &u) || u)
		return false;
	if (!ds_anchor_rd64(init_pid_ns + off_parent, &v) || v)
		return false;
	if (!ds_anchor_rd32(init_pid_ns + off_alloc, &u) ||
	    (u & PIDNS_ADDING) != PIDNS_ADDING)
		return false;
	/* init_pid_ns has no ucounts and reboot starts at zero */
	if (!ds_anchor_rd64(init_pid_ns + off_ucounts, &v) || v)
		return false;
	if (!ds_anchor_rd32(init_pid_ns + off_user + 16, &u) || u)
		return false;

	ok &= ds_anchor_set(l, "pid_namespace", "user_ns", off_user);
	ok &= ds_anchor_set(l, "pid_namespace", "child_reaper", build_reaper);
	ok &= ds_anchor_set(l, "pid_namespace", "ns", off_ns);
	ok &= ds_anchor_set(l, "pid_namespace", "level", off_level);
	ok &= ds_anchor_set(l, "pid_namespace", "parent", off_parent);
	ok &= ds_anchor_set(l, "pid_namespace", "pid_allocated", off_alloc);
	ok &= ds_anchor_set(l, "pid_namespace", "ucounts", off_ucounts);
	return ok;
}

bool ds_compat_layout_anchor(struct ds_compat_layout *l)
{
	unsigned long init_task, init_nsproxy, init_cred, init_struct_pid;
	unsigned long init_pid_ns, init_uts, init_net, init_user_ns;
	unsigned long init_signals, init_sighand;
	bool task, nsproxy, pid, pidns;
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

	ds_anchor_keep_build(l, "ipc_namespace");
	ds_anchor_keep_build(l, "seq_file");
	ds_anchor_keep_build(l, "pid_namespace");

	task = init_task && init_nsproxy && init_cred && init_struct_pid &&
	       ds_anchor_task(l, init_task, init_nsproxy, init_cred,
			      init_struct_pid, init_signals, init_sighand);
	nsproxy = init_nsproxy && init_uts && init_net && init_pid_ns &&
		  ds_anchor_nsproxy(l, init_nsproxy, init_uts, init_net, init_pid_ns);
	pid = init_struct_pid && init_pid_ns &&
	      ds_anchor_pid(l, init_struct_pid, init_pid_ns);
	pidns = ds_anchor_pidns(l, init_pid_ns, init_user_ns);

	for (i = 0; i < l->nfields; i++) {
		if (l->fields[i].state == DS_COMPAT_ANCHORED ||
		    l->fields[i].state == DS_COMPAT_BUILD)
			continue;
		l->fields[i].live = 0;
		l->fields[i].state = DS_COMPAT_UNVERIFIED;
	}

	pr_info("[%s] layout: no BTF, anchors task=%d nsproxy=%d pid=%d pidns=%d, module owned structs keep the build layout\n",
		l->module, task, nsproxy, pid, pidns);
	return task || nsproxy || pid || pidns;
}

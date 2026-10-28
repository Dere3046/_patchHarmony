// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/refcount.h>
#include <linux/string.h>
#include <linux/preempt.h>
#include <linux/threads.h>
#include <linux/compiler.h>
#include <linux/mm.h>

#include "core.h"
#include "ds_compat.h"
#include "ds_hook.h"
#include "ghost.h"
#include "vendor_syms.h"
#include "hk_inline.h"

/* 6.18 builds with -Wmissing-prototypes */
__nocfi noinline struct task_struct *dlc_ftbv_wrap(pid_t vnr);

#define DLC_GHOST_TASK_COMM	"ghost-task-sentinel"
#define DLC_GHOST_LOG_MAX	3

static bool dlc_ghost_enable = true;
module_param_named(ghost, dlc_ghost_enable, bool, 0444);
MODULE_PARM_DESC(ghost, "hand out a ghost task when find_task_by_vpid misses, default 1");

static char *dlc_ghost_match = "oplus_";
module_param_named(ghost_match, dlc_ghost_match, charp, 0444);
MODULE_PARM_DESC(ghost_match, "caller module name prefix, * matches all, default oplus_");

static typeof(&find_task_by_vpid) dlc_ftbv_orig;
static typeof(&__module_address) dlc_module_address;
static struct ds_hook dlc_ftbv_hook;
static struct task_struct dlc_ghost_task;
static bool dlc_ghost_ready;
static unsigned int dlc_ghost_hits;

/* Fast caller cache for hot-path interrupt context (e.g. oplus_bsp_midas) */
#define DLC_GHOST_CACHE_SZ 32
static unsigned long dlc_ghost_cached_pages[DLC_GHOST_CACHE_SZ];
static unsigned int dlc_ghost_cache_idx;

static bool dlc_ghost_cache_hit(unsigned long ip)
{
	unsigned long page = ip & PAGE_MASK;
	int i;

	if (unlikely(!page))
		return false;

	for (i = 0; i < DLC_GHOST_CACHE_SZ; i++) {
		if (READ_ONCE(dlc_ghost_cached_pages[i]) == page)
			return true;
	}
	return false;
}

static void dlc_ghost_cache_add(unsigned long ip)
{
	unsigned long page = ip & PAGE_MASK;
	unsigned int idx;

	if (unlikely(!page))
		return;

	if (dlc_ghost_cache_hit(ip))
		return;

	idx = dlc_ghost_cache_idx++ % DLC_GHOST_CACHE_SZ;
	WRITE_ONCE(dlc_ghost_cached_pages[idx], page);
}

static bool dlc_ghost_match_module(const char *name)
{
	size_t n;

	if (!dlc_ghost_match || !*dlc_ghost_match)
		return false;
	if (dlc_ghost_match[0] == '*' && !dlc_ghost_match[1])
		return true;
	n = strlen(dlc_ghost_match);
	return strncmp(name, dlc_ghost_match, n) == 0;
}

// _RET_IP_ must be read before calling orig
__nocfi noinline struct task_struct *dlc_ftbv_wrap(pid_t vnr)
{
	unsigned long ret_ip = (unsigned long)_RET_IP_;
	typeof(&find_task_by_vpid) orig;
	struct task_struct *task = NULL;
	struct module *mod;
	bool match = false;

	orig = READ_ONCE(dlc_ftbv_orig);
	if (likely(orig))
		task = orig(vnr);

	/* Always clean up hook guard state, even in the race-window where orig is NULL */
	ds_hook_leave(&dlc_ftbv_hook);

	if (likely(task))
		return task;

	/* If ghost fixups are disabled or task_struct template is not ready */
	if (!READ_ONCE(dlc_ghost_ready))
		return task;
	smp_rmb();

	/* Fast path: check page cache without scanning kernel module tree */
	if (dlc_ghost_cache_hit(ret_ip)) {
		match = true;
	} else if (dlc_module_address) {
		preempt_disable();
		mod = dlc_module_address(ret_ip);
		if (mod)
			match = dlc_ghost_match_module(mod->name);
		preempt_enable();
		if (match)
			dlc_ghost_cache_add(ret_ip);
	}

	if (match) {
		dlc_ghost_hits++;
		if (dlc_ghost_hits <= DLC_GHOST_LOG_MAX)
			pr_info("[droid_lkm_vendor] ghost: pid=%d caller=%pS -> %s (orig=%s)\n",
				(int)vnr, (void *)ret_ip, DLC_GHOST_TASK_COMM,
				orig ? "miss" : "race-guard");
		return &dlc_ghost_task;
	}

	return task;
}

enum {
	DLC_GHOST_F_PID,
	DLC_GHOST_F_TGID,
	DLC_GHOST_F_COMM,
	DLC_GHOST_F_USAGE,
	DLC_GHOST_F_TASKS,
	DLC_GHOST_F_CHILDREN,
	DLC_GHOST_F_SIBLING,
	DLC_GHOST_F_THREAD_NODE,
	DLC_GHOST_F_PTRACED,
	DLC_GHOST_F_PTRACE_ENTRY,
	DLC_GHOST_F_COUNT
};

static struct ds_compat_field dlc_ghost_fields[DLC_GHOST_F_COUNT] = {
	[DLC_GHOST_F_PID] = { .type = "task_struct", .member = "pid" },
	[DLC_GHOST_F_TGID] = { .type = "task_struct", .member = "tgid" },
	[DLC_GHOST_F_COMM] = { .type = "task_struct", .member = "comm" },
	[DLC_GHOST_F_USAGE] = { .type = "task_struct", .member = "usage" },
	[DLC_GHOST_F_TASKS] = { .type = "task_struct", .member = "tasks" },
	[DLC_GHOST_F_CHILDREN] = { .type = "task_struct", .member = "children" },
	[DLC_GHOST_F_SIBLING] = { .type = "task_struct", .member = "sibling" },
	[DLC_GHOST_F_THREAD_NODE] = { .type = "task_struct", .member = "thread_node" },
	[DLC_GHOST_F_PTRACED] = { .type = "task_struct", .member = "ptraced" },
	[DLC_GHOST_F_PTRACE_ENTRY] = { .type = "task_struct", .member = "ptrace_entry" },
};

static struct ds_compat_type dlc_ghost_types[] = {
	{ .name = "task_struct" },
};

static struct ds_compat_layout dlc_ghost_layout_table = {
	.module = "droid_lkm_vendor",
	.fields = dlc_ghost_fields,
	.nfields = ARRAY_SIZE(dlc_ghost_fields),
	.types = dlc_ghost_types,
	.ntypes = ARRAY_SIZE(dlc_ghost_types),
};

static u32 dlc_ghost_size;

// offsets come from the running kernel BTF
// a build offset would write pid or comm into whatever the kernel keeps there
static int dlc_ghost_layout(void)
{
	if (!ds_compat_layout_init(&dlc_ghost_layout_table))
		return -ENODATA;

	dlc_ghost_size = dlc_ghost_types[0].live;
	return 0;
}

static void *dlc_ghost_at(unsigned int field)
{
	return (char *)&dlc_ghost_task + dlc_ghost_fields[field].live;
}

// ghost is a shallow copy of init_task, list heads re-init to self
static int dlc_ghost_build(void)
{
	unsigned long init_task_addr = dlc_vendor_syms.init_task;
	unsigned int copy, i;
	int ret;

	if (!init_task_addr)
		return -ENOENT;

	ret = dlc_ghost_layout();
	if (ret) {
		pr_err("[droid_lkm_vendor] ghost: no task_struct offsets for the running kernel (%d)\n",
		       ret);
		return ret;
	}

	// the running kernel's task_struct may be smaller than this build's
	copy = sizeof(dlc_ghost_task);
	if (dlc_ghost_size < copy)
		copy = dlc_ghost_size;
	memcpy(&dlc_ghost_task, (void *)init_task_addr, copy);

	for (i = DLC_GHOST_F_TASKS; i <= DLC_GHOST_F_PTRACE_ENTRY; i++)
		INIT_LIST_HEAD(dlc_ghost_at(i));

	strscpy(dlc_ghost_at(DLC_GHOST_F_COMM), DLC_GHOST_TASK_COMM, TASK_COMM_LEN);
	// vendors index PID_MAX_DEFAULT sized arrays by p->pid
	// init_task.thread_pid is init_struct_pid nr 0 so the vnr helpers agree
	*(pid_t *)dlc_ghost_at(DLC_GHOST_F_PID) = 0;
	*(pid_t *)dlc_ghost_at(DLC_GHOST_F_TGID) = 0;
	refcount_set((refcount_t *)dlc_ghost_at(DLC_GHOST_F_USAGE), 1000);
	smp_wmb();
	WRITE_ONCE(dlc_ghost_ready, true);
	return 0;
}

// vendor tables are PID_MAX_DEFAULT entries indexed by task->pid
// 0 caps only when such a module is loaded
// -1 never  N caps to N
static int dlc_pid_max_cap;
module_param_named(pid_max_cap, dlc_pid_max_cap, int, 0444);
MODULE_PARM_DESC(pid_max_cap,
	"0 caps pid_max only when a pid indexed vendor module is loaded, -1 never, N caps to N");

static const char *const dlc_pid_indexed_modules[] = {
	"oplus_bsp_sched_assist",
	"oplus_bsp_task_sched",
	"oplus_bsp_schedinfo",
	"oplus_bsp_schedtune",
	"oplus_bsp_sched_ext",
	"oplus_bsp_uxmem_opt",
};

/* sysfs is the one place every branch agrees on, so ask it through kern_path */
static bool dlc_module_loaded(const char *name)
{
	int (*kern_path_fn)(const char *, unsigned int, struct path *);
	void (*path_put_fn)(const struct path *);
	char path[64];
	struct path p;

	kern_path_fn = dlc_vendor_syms.kern_path;
	path_put_fn = dlc_vendor_syms.path_put;
	if (!kern_path_fn || !path_put_fn)
		return false;

	snprintf(path, sizeof(path), "/sys/module/%s", name);
	if (kern_path_fn(path, 0, &p))
		return false;
	path_put_fn(&p);
	return true;
}

static void dlc_ghost_audit_pid_max(void)
{
	int *pid_max_addr = dlc_vendor_syms.pid_max;
	int value, cap = dlc_pid_max_cap;
	int i;

	if (!pid_max_addr || cap < 0)
		return;

	value = READ_ONCE(*pid_max_addr);

	/* an explicit cap is enforced as given, whatever the default happens to be */
	if (cap > 0) {
		if (value <= cap)
			return;
		WRITE_ONCE(*pid_max_addr, cap);
		pr_warn("[droid_lkm_vendor] pid_max %d -> %d by module parameter\n",
			value, cap);
		return;
	}

	if (value <= PID_MAX_DEFAULT)
		return;

	{
		for (i = 0; i < ARRAY_SIZE(dlc_pid_indexed_modules); i++) {
			if (dlc_module_loaded(dlc_pid_indexed_modules[i])) {
				pr_warn("[droid_lkm_vendor] pid_max=%d with %s loaded: pid indexed arrays are sized for %d\n",
					value, dlc_pid_indexed_modules[i],
					PID_MAX_DEFAULT);
				break;
			}
		}
		if (i == ARRAY_SIZE(dlc_pid_indexed_modules))
			return;
		cap = PID_MAX_DEFAULT;
	}

	if (value <= cap)
		return;

	WRITE_ONCE(*pid_max_addr, cap);
	pr_warn("[droid_lkm_vendor] pid_max %d -> %d, pids wrap inside the range vendor arrays cover\n",
		value, cap);
}

static void dlc_ghost_prewarm_addr(unsigned long addr)
{
	int i;

	if (!addr)
		return;
	for (i = -2; i <= 2; i++)
		dlc_ghost_cache_add(addr + i * PAGE_SIZE);
}

static void dlc_ghost_prewarm(void)
{
	unsigned long sym_addr;

	sym_addr = ds_compat_sym("update_or_create_entry_locked");
	if (sym_addr) {
		dlc_ghost_prewarm_addr(sym_addr);
		pr_info("[droid_lkm_vendor] ghost: pre-warmed for update_or_create_entry_locked (0x%lx)\n",
			sym_addr);
	}

	sym_addr = ds_compat_sym("midas_record_task_times");
	if (sym_addr) {
		dlc_ghost_prewarm_addr(sym_addr);
		pr_info("[droid_lkm_vendor] ghost: pre-warmed for midas_record_task_times (0x%lx)\n",
			sym_addr);
	}
}

void dlc_ghost_exit(void)
{
	ds_hook_remove(&dlc_ftbv_hook);
	dlc_ghost_ready = false;
}

int dlc_ghost_init(void)
{
	int ret;

	if (!dlc_ghost_enable)
		return 0;

	ret = dlc_ghost_build();
	if (ret) {
		pr_err("[droid_lkm_vendor] ghost: init_task not found\n");
		return ret;
	}
	dlc_ghost_audit_pid_max();

	dlc_ghost_prewarm();

	dlc_module_address = (typeof(dlc_module_address))dlc_vendor_syms.module_address;
	if (!dlc_module_address) {
		pr_err("[droid_lkm_vendor] ghost: __module_address not found\n");
		dlc_ghost_ready = false;
		return -ENOENT;
	}

	/*
	 * the engine patches the end of a branch chain when the entry is a thunk,
	 * and on a +lto kernel find_task_by_vpid is one. the trampoline of that
	 * build loops back into the patched entry, so a call recurses until the
	 * kernel stack is gone. judge the entry first and refuse a shape the
	 * engine cannot drive, the feature degrades instead of panicking
	 */
	{
		struct hk_inline_probe probe;

		memset(&probe, 0, sizeof(probe));
		if (!hk_inline_probe("find_task_by_vpid", &probe) &&
		    probe.state != HK_INLINE_PLAIN) {
			pr_err("[droid_lkm_vendor] ghost: find_task_by_vpid entry state %d (%s) target 0x%lx, inline hook refused\n",
			       probe.state, probe.reason ? probe.reason : "?",
			       probe.target);
			dlc_ghost_ready = false;
			return -EOPNOTSUPP;
		}
	}

	ret = ds_hook_install(&dlc_ftbv_hook, "find_task_by_vpid", "dlc_ftbv_wrap");
	if (ret) {
		pr_err("[droid_lkm_vendor] ghost: hook find_task_by_vpid failed %d\n", ret);
		dlc_ghost_ready = false;
		return ret;
	}
	smp_wmb();
	WRITE_ONCE(dlc_ftbv_orig, (typeof(dlc_ftbv_orig))dlc_ftbv_hook.orig);
	pr_info("[droid_lkm_vendor] ghost: find_task_by_vpid hooked, match=%s\n",
		dlc_ghost_match ? dlc_ghost_match : "");
	return 0;
}

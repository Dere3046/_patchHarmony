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

#include <linux/threads.h>

#include "core.h"
#include "ghost.h"
#include "hk_inline.h"
#include "type_info.h"
#include <linux/btf.h>

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
extern bool dlc_inline_hooks_on;

/*
 * inline hooks rewrite the entry of a live kernel function and the device owner
 * suspects the hypervisor refuses that store on MTK, so they are off by default
 * while everything else stays on. inline_hook=1 brings them back, a refused hook
 * is not fatal, the ghost feature just stays unavailable
 */
static inline int dlc_inline_hook(struct hk_inline *h, const char *sym,
				  const char *wrap)
{
	if (!dlc_inline_hooks_on) {
		pr_warn_once("[droid_lkm_compat] inline hooks are off (inline_hook=0), %s not hooked\n", sym);
		return -EOPNOTSUPP;
	}
	return hk_inline_hook(h, sym, wrap);
}

static struct hk_inline dlc_ftbv_hook;
static struct task_struct dlc_ghost_task;
static bool dlc_ghost_ready;
static unsigned int dlc_ghost_hits;

static unsigned long dlc_ghost_sym(const char *name)
{
	if (!kallrecon_klp)
		return 0;
	return kallrecon_klp(name);
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
	struct task_struct *task;
	struct module *mod;
	bool match = false;

	if (unlikely(!dlc_ftbv_orig))
		return NULL;

	task = dlc_ftbv_orig(vnr);
	if (likely(task))
		return task;

	if (!dlc_ghost_ready)
		return task;

	if (dlc_module_address) {
		preempt_disable();
		mod = dlc_module_address(ret_ip);
		if (mod)
			match = dlc_ghost_match_module(mod->name);
		preempt_enable();
		if (!match)
			return task;
		dlc_ghost_hits++;
		if (dlc_ghost_hits <= DLC_GHOST_LOG_MAX)
			pr_info("[droid_lkm_compat] ghost: pid=%d caller=%pS -> %s\n",
				(int)vnr, (void *)ret_ip, DLC_GHOST_TASK_COMM);
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

static const char *const dlc_ghost_fields[DLC_GHOST_F_COUNT] = {
	[DLC_GHOST_F_PID] = "pid",
	[DLC_GHOST_F_TGID] = "tgid",
	[DLC_GHOST_F_COMM] = "comm",
	[DLC_GHOST_F_USAGE] = "usage",
	[DLC_GHOST_F_TASKS] = "tasks",
	[DLC_GHOST_F_CHILDREN] = "children",
	[DLC_GHOST_F_SIBLING] = "sibling",
	[DLC_GHOST_F_THREAD_NODE] = "thread_node",
	[DLC_GHOST_F_PTRACED] = "ptraced",
	[DLC_GHOST_F_PTRACE_ENTRY] = "ptrace_entry",
};

static u32 dlc_ghost_offs[DLC_GHOST_F_COUNT];
static u32 dlc_ghost_size;

// offsets come from the running kernel BTF
// a build offset would write pid or comm into whatever the kernel keeps there
static int dlc_ghost_layout(void)
{
	struct ti_resolver res = { .name_to_addr = dlc_ghost_sym };
	u32 id, bit, sz;
	int i;

	if (ti_init(&res) || !ti_btf_available())
		return -ENODATA;
	if (ti_type_by_name(ti_base(), "task_struct", BIT(BTF_KIND_STRUCT), &id))
		return -ENOENT;

	for (i = 0; i < DLC_GHOST_F_COUNT; i++) {
		if (ti_member_off(ti_base(), id, dlc_ghost_fields[i], &bit, &sz))
			return -ENOENT;
		dlc_ghost_offs[i] = bit / 8;
	}

	dlc_ghost_size = ti_type_size(ti_base(), id);
	if (!dlc_ghost_size)
		return -ENOENT;

	return 0;
}

static void *dlc_ghost_at(unsigned int field)
{
	return (char *)&dlc_ghost_task + dlc_ghost_offs[field];
}


// ghost is a shallow copy of init_task, list heads re-init to self
static int dlc_ghost_build(void)
{
	unsigned long init_task_addr = dlc_ghost_sym("init_task");
	unsigned int copy, i;
	int ret;

	if (!init_task_addr)
		return -ENOENT;

	ret = dlc_ghost_layout();
	if (ret) {
		pr_err("[droid_lkm_compat] ghost: no task_struct offsets for the running kernel (%d)\n",
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
	dlc_ghost_ready = true;
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

	kern_path_fn = (void *)dlc_ghost_sym("kern_path");
	path_put_fn = (void *)dlc_ghost_sym("path_put");
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
	int *pid_max_addr = (int *)dlc_ghost_sym("pid_max");
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
		pr_warn("[droid_lkm_compat] pid_max %d -> %d by module parameter\n",
			value, cap);
		return;
	}

	if (value <= PID_MAX_DEFAULT)
		return;

	{
		for (i = 0; i < ARRAY_SIZE(dlc_pid_indexed_modules); i++) {
			if (dlc_module_loaded(dlc_pid_indexed_modules[i])) {
				pr_warn("[droid_lkm_compat] pid_max=%d with %s loaded: pid indexed arrays are sized for %d\n",
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
	pr_warn("[droid_lkm_compat] pid_max %d -> %d, pids wrap inside the range vendor arrays cover\n",
		value, cap);
}

void dlc_ghost_exit(void)
{
	if (dlc_ftbv_hook.orig)
		hk_inline_unhook(&dlc_ftbv_hook);
	dlc_ghost_ready = false;
}

int dlc_ghost_init(void)
{
	int ret;

	if (!dlc_ghost_enable)
		return 0;

	ret = dlc_ghost_build();
	if (ret) {
		pr_err("[droid_lkm_compat] ghost: init_task not found\n");
		return ret;
	}
	dlc_ghost_audit_pid_max();


	dlc_module_address = (typeof(dlc_module_address))dlc_ghost_sym("__module_address");
	if (!dlc_module_address) {
		pr_err("[droid_lkm_compat] ghost: __module_address not found\n");
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
			pr_err("[droid_lkm_compat] ghost: find_task_by_vpid entry state %d (%s) target 0x%lx, inline hook refused\n",
			       probe.state, probe.reason ? probe.reason : "?",
			       probe.target);
			dlc_ghost_ready = false;
			return -EOPNOTSUPP;
		}
	}

	ret = dlc_inline_hook(&dlc_ftbv_hook, "find_task_by_vpid", "dlc_ftbv_wrap");
	if (ret) {
		pr_err("[droid_lkm_compat] ghost: hook find_task_by_vpid failed %d\n", ret);
		dlc_ghost_ready = false;
		return ret;
	}
	dlc_ftbv_orig = (typeof(dlc_ftbv_orig))dlc_ftbv_hook.orig;
	pr_info("[droid_lkm_compat] ghost: find_task_by_vpid hooked, match=%s\n",
		dlc_ghost_match ? dlc_ghost_match : "");
	return 0;
}

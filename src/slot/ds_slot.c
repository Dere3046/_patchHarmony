// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/nsproxy.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pid_namespace.h>
#include <linux/pid_namespace.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>
#include <linux/uaccess.h>
#include <linux/capability.h>
#include <linux/user_namespace.h>

#include "ds.h"
#include "ds_ksym.h"
#include "ds_ipcns.h"
#include "ds_pidns.h"
#include "ds_slot.h"
#include "ds_ipc_compat.h"
#include "ipc_mqueue_compat.h"
#include "sc.h"
#include "hk_patch.h"
#include "hk_inline.h"

typedef long (*droid_lkm_syscall_fn)(const struct pt_regs *regs);

asmlinkage long __arm64_sys_mq_open(const struct pt_regs *regs);
asmlinkage long __arm64_sys_mq_unlink(const struct pt_regs *regs);
asmlinkage long __arm64_sys_mq_timedsend(const struct pt_regs *regs);
asmlinkage long __arm64_sys_mq_timedreceive(const struct pt_regs *regs);
asmlinkage long __arm64_sys_mq_notify(const struct pt_regs *regs);
asmlinkage long __arm64_sys_mq_getsetattr(const struct pt_regs *regs);

asmlinkage long __arm64_sys_msgget(const struct pt_regs *regs);
asmlinkage long __arm64_sys_msgctl(const struct pt_regs *regs);
asmlinkage long __arm64_sys_msgrcv(const struct pt_regs *regs);
asmlinkage long __arm64_sys_msgsnd(const struct pt_regs *regs);
asmlinkage long __arm64_sys_semget(const struct pt_regs *regs);
asmlinkage long __arm64_sys_semctl(const struct pt_regs *regs);
asmlinkage long __arm64_sys_semtimedop(const struct pt_regs *regs);
asmlinkage long __arm64_sys_semop(const struct pt_regs *regs);
asmlinkage long __arm64_sys_shmget(const struct pt_regs *regs);
asmlinkage long __arm64_sys_shmctl(const struct pt_regs *regs);
asmlinkage long __arm64_sys_shmat(const struct pt_regs *regs);
asmlinkage long __arm64_sys_shmdt(const struct pt_regs *regs);

static bool droid_lkm_world_task(struct task_struct *t)
{
	struct nsproxy *nsp;

	if (!t)
		return false;
	if (droid_lkm_pidns_is_ours(task_active_pid_ns(t)))
		return true;

	nsp = t->nsproxy;
	if (!nsp)
		return false;
	if (droid_lkm_pidns_is_ours(nsp->pid_ns_for_children))
		return true;


	if (droid_lkm_ipcns_is_ours(nsp->ipc_ns) && !droid_lkm_ipcns_is_host(nsp->ipc_ns))
		return true;
	return false;
}

static bool droid_lkm_is_droidspaces(void)
{
	const char *exe = NULL;

	if ((current->flags & PF_KTHREAD) || !current->mm)
		return false;
	if (strncmp(current->comm, "droidspaces", 11) != 0 &&
	    strcmp(current->comm, "[ds-monitor]") != 0)
		return false;

	if (current->mm->exe_file)
		exe = current->mm->exe_file->f_path.dentry->d_name.name;
	return exe && strstr(exe, "droidspaces") != NULL;
}

static bool droid_lkm_gate_enabled;
module_param_named(gate, droid_lkm_gate_enabled, bool, 0444);
MODULE_PARM_DESC(gate,
	"1 = only serve the droidspaces world (host keeps stock EINVAL/ENOSYS); 0 = full/complete (default)");

static bool droid_lkm_gate_allow(void)
{
	if (!droid_lkm_gate_enabled)
		return true;
	return droid_lkm_world_task(current) || droid_lkm_is_droidspaces();
}

#define DROID_LKM_IPC_ORIG_BASE 180
#define DROID_LKM_IPC_ORIG_MAX 24
static droid_lkm_syscall_fn droid_lkm_ipc_orig[DROID_LKM_IPC_ORIG_MAX];

static long droid_lkm_ipc_orig_call(int nr, const struct pt_regs *regs)
{
	int idx = nr - DROID_LKM_IPC_ORIG_BASE;

	if (idx < 0 || idx >= DROID_LKM_IPC_ORIG_MAX || !droid_lkm_ipc_orig[idx])
		return -ENOSYS;
	return droid_lkm_ipc_orig[idx](regs);
}

#define DROID_LKM_IPC_THUNK(name, nr)                                          \
	static long droid_lkm_thunk_##name(const struct pt_regs *regs)         \
	{                                                               \
		if (droid_lkm_gate_allow())                                    \
			return __arm64_sys_##name(regs);                \
		return droid_lkm_ipc_orig_call(nr, regs);                      \
	}

DROID_LKM_IPC_THUNK(mq_open, __NR_mq_open)
DROID_LKM_IPC_THUNK(mq_unlink, __NR_mq_unlink)
DROID_LKM_IPC_THUNK(mq_timedsend, __NR_mq_timedsend)
DROID_LKM_IPC_THUNK(mq_timedreceive, __NR_mq_timedreceive)
DROID_LKM_IPC_THUNK(mq_notify, __NR_mq_notify)
DROID_LKM_IPC_THUNK(mq_getsetattr, __NR_mq_getsetattr)

DROID_LKM_IPC_THUNK(msgget, __NR_msgget)
DROID_LKM_IPC_THUNK(msgctl, __NR_msgctl)
DROID_LKM_IPC_THUNK(msgrcv, __NR_msgrcv)
DROID_LKM_IPC_THUNK(msgsnd, __NR_msgsnd)
DROID_LKM_IPC_THUNK(semget, __NR_semget)
DROID_LKM_IPC_THUNK(semctl, __NR_semctl)
DROID_LKM_IPC_THUNK(semtimedop, __NR_semtimedop)
DROID_LKM_IPC_THUNK(semop, __NR_semop)
DROID_LKM_IPC_THUNK(shmget, __NR_shmget)
DROID_LKM_IPC_THUNK(shmctl, __NR_shmctl)
DROID_LKM_IPC_THUNK(shmat, __NR_shmat)
DROID_LKM_IPC_THUNK(shmdt, __NR_shmdt)

static const struct droid_lkm_ipc_slot {
	int nr;
	droid_lkm_syscall_fn fn;
	const char *name;
	bool needs_mqueue;
} droid_lkm_ipc_slots[] = {
	{ __NR_mq_open, droid_lkm_thunk_mq_open,  "mq_open" , 1 },
	{ __NR_mq_unlink, droid_lkm_thunk_mq_unlink,  "mq_unlink" , 1 },
	{ __NR_mq_timedsend, droid_lkm_thunk_mq_timedsend,  "mq_timedsend" , 1 },
	{ __NR_mq_timedreceive, droid_lkm_thunk_mq_timedreceive,
	  "mq_timedreceive", 1 },
	{ __NR_mq_notify, droid_lkm_thunk_mq_notify,  "mq_notify" , 1 },
	{ __NR_mq_getsetattr, droid_lkm_thunk_mq_getsetattr,  "mq_getsetattr" , 1 },
	{ __NR_msgget, droid_lkm_thunk_msgget, "msgget" },
	{ __NR_msgctl, droid_lkm_thunk_msgctl, "msgctl" },
	{ __NR_msgrcv, droid_lkm_thunk_msgrcv, "msgrcv" },
	{ __NR_msgsnd, droid_lkm_thunk_msgsnd, "msgsnd" },
	{ __NR_semget, droid_lkm_thunk_semget, "semget" },
	{ __NR_semctl, droid_lkm_thunk_semctl, "semctl" },
	{ __NR_semtimedop, droid_lkm_thunk_semtimedop, "semtimedop" },
	{ __NR_semop, droid_lkm_thunk_semop, "semop" },
	{ __NR_shmget, droid_lkm_thunk_shmget, "shmget" },
	{ __NR_shmctl, droid_lkm_thunk_shmctl, "shmctl" },
	{ __NR_shmat, droid_lkm_thunk_shmat, "shmat" },
	{ __NR_shmdt, droid_lkm_thunk_shmdt, "shmdt" },
};

static unsigned long droid_lkm_ipc_patched[ARRAY_SIZE(droid_lkm_ipc_slots)];

static bool droid_lkm_skip_sysvipc;
static bool droid_lkm_no_fake_ns;

#define DROID_LKM_SLOT_SAVE_MAX 32
static unsigned long *droid_lkm_sys_call_table;
static int droid_lkm_slot_save_nr[DROID_LKM_SLOT_SAVE_MAX];
static unsigned long droid_lkm_slot_save_orig[DROID_LKM_SLOT_SAVE_MAX];
static int droid_lkm_slot_save_cnt;

static int droid_lkm_slot_patch(int nr, unsigned long fn,
				unsigned long *orig_out)
{
	int i;

	if (!droid_lkm_sys_call_table || nr < 0)
		return -EINVAL;
	if (droid_lkm_slot_save_cnt >= DROID_LKM_SLOT_SAVE_MAX)
		return -ENOSPC;
	for (i = 0; i < droid_lkm_slot_save_cnt; i++)
		if (droid_lkm_slot_save_nr[i] == nr)
			return -EEXIST;

	droid_lkm_slot_save_nr[droid_lkm_slot_save_cnt] = nr;
	droid_lkm_slot_save_orig[droid_lkm_slot_save_cnt] =
		droid_lkm_sys_call_table[nr];
	if (orig_out)
		*orig_out = droid_lkm_sys_call_table[nr];
	droid_lkm_slot_save_cnt++;

	return hk_patch_write(&droid_lkm_sys_call_table[nr], fn);
}

static void droid_lkm_slot_unpatch(int nr)
{
	int i;

	if (!droid_lkm_sys_call_table)
		return;
	for (i = 0; i < droid_lkm_slot_save_cnt; i++) {
		if (droid_lkm_slot_save_nr[i] != nr)
			continue;
		(void)hk_patch_write(&droid_lkm_sys_call_table[nr],
				     droid_lkm_slot_save_orig[i]);
		droid_lkm_slot_save_nr[i] =
			droid_lkm_slot_save_nr[--droid_lkm_slot_save_cnt];
		droid_lkm_slot_save_orig[i] =
			droid_lkm_slot_save_orig[droid_lkm_slot_save_cnt];
		return;
	}
}

static int droid_lkm_slot_patch_ipc(void)
{
	unsigned long orig;
	int patched = 0, i;

	if (droid_lkm_skip_sysvipc) {
		droid_lkm_info("skip_sysvipc=1, leave kernel entries alone\n");
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(droid_lkm_ipc_slots); i++) {
		char name[64] = "?";


		if (droid_lkm_ipc_slots[i].needs_mqueue && !droid_lkm_mqueue_ready()) {
			droid_lkm_warn("skip %s: mqueue not ready\n",
				       droid_lkm_ipc_slots[i].name);
			continue;
		}
		unsigned long cur = sc_entry(droid_lkm_ipc_slots[i].nr);

		if (sym_name_at(cur, name, sizeof(name)) < 0)
			snprintf(name, sizeof(name), "0x%lx", cur);

		if (droid_lkm_slot_patch(droid_lkm_ipc_slots[i].nr,
			     (unsigned long)droid_lkm_ipc_slots[i].fn, &orig)) {
			droid_lkm_warn("cannot patch %s\n", droid_lkm_ipc_slots[i].name);
			continue;
		}
		{
			int idx = droid_lkm_ipc_slots[i].nr - DROID_LKM_IPC_ORIG_BASE;

			if (idx >= 0 && idx < DROID_LKM_IPC_ORIG_MAX)
				droid_lkm_ipc_orig[idx] = (droid_lkm_syscall_fn)orig;
		}
		droid_lkm_ipc_patched[i] = 1;
		patched++;
		droid_lkm_dbg("ipc slot %d (%s) 0x%lx[%s] -> ours\n",
			droid_lkm_ipc_slots[i].nr, droid_lkm_ipc_slots[i].name, cur, name);
	}

	droid_lkm_info("ipc syscalls wired: %d/%zu\n", patched,
		ARRAY_SIZE(droid_lkm_ipc_slots));
	return patched ? 0 : -ENODATA;
}

#define DROID_LKM_NS_FLAGS                                                            \
	(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET |            \
	 CLONE_NEWCGROUP | CLONE_NEWTIME)

module_param_named(skip_sysvipc, droid_lkm_skip_sysvipc, bool, 0444);
MODULE_PARM_DESC(skip_sysvipc,
	"do not take over the 18 ipc syscall slots (6 POSIX mqueue + 12 SysV)");

module_param_named(no_fake_ns, droid_lkm_no_fake_ns, bool, 0444);
MODULE_PARM_DESC(no_fake_ns, "diagnostic: fake-success unshare() without attaching our ns");

static droid_lkm_syscall_fn droid_lkm_orig_unshare;
static droid_lkm_syscall_fn droid_lkm_orig_reboot;
static droid_lkm_syscall_fn droid_lkm_orig_clone;
static droid_lkm_syscall_fn droid_lkm_orig_clone3;

static unsigned long __nocfi droid_lkm_sc_resolve(const char *name)
{
	return droid_lkm_sym(name);
}

static int droid_lkm_slot_install_pidns(void)
{
	struct pid_namespace *ns;

	ns = droid_lkm_pidns_create_for_current();
	if (IS_ERR(ns)) {
		droid_lkm_err("cannot create pidns: %ld\n", PTR_ERR(ns));
		return PTR_ERR(ns);
	}

	if (!current->nsproxy) {
		droid_lkm_err("no nsproxy after unshare\n");
		droid_lkm_pidns_put(ns);
		return -EINVAL;
	}

	current->nsproxy->pid_ns_for_children = ns;
	droid_lkm_info("pidns %p attached to %s[%d]\n", ns, current->comm,
		current->pid);
	return 0;
}

static int droid_lkm_slot_install_ipcns(void)
{
	struct ipc_namespace *ns;

	ns = droid_lkm_ipcns_create();
	if (IS_ERR(ns)) {
		droid_lkm_err("cannot create ipcns: %ld\n", PTR_ERR(ns));
		return PTR_ERR(ns);
	}

	if (!current->nsproxy) {
		droid_lkm_err("no nsproxy after unshare\n");
		droid_lkm_ipcns_put(ns);
		return -EINVAL;
	}

	current->nsproxy->ipc_ns = ns;
	droid_lkm_info("ipcns %p attached to %s[%d]\n", ns, current->comm,
		current->pid);
	return 0;
}

#define DROID_LKM_NS_ALL                                                              \
	(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET |            \
	 CLONE_NEWCGROUP | CLONE_NEWTIME | CLONE_NEWPID | CLONE_NEWUSER)

static struct nsproxy *(*droid_lkm_cnn_orig)(unsigned long flags,
				      struct task_struct *tsk,
				      struct user_namespace *user_ns,
				      struct fs_struct *fs);
static struct hk_inline droid_lkm_cnn_hook;
static bool droid_lkm_cnn_hooked;
static int (*droid_lkm_check_unshare_flags_fn)(unsigned long flags);

__nocfi noinline struct nsproxy *droid_lkm_cnn_wrap(unsigned long flags,
					     struct task_struct *tsk,
					     struct user_namespace *user_ns,
					     struct fs_struct *fs)
{
	bool want_pid = flags & CLONE_NEWPID;
	bool want_ipc = flags & CLONE_NEWIPC;
	struct pid_namespace *new_pid = NULL;
	struct ipc_namespace *new_ipc = NULL;
	struct nsproxy *nsp;

	if (!want_pid && !want_ipc)
		return droid_lkm_cnn_orig(flags, tsk, user_ns, fs);


	if (!droid_lkm_gate_allow())
		return droid_lkm_cnn_orig(flags, tsk, user_ns, fs);

	if (want_pid) {
		struct pid_namespace *old;

		if (!tsk->nsproxy)
			return ERR_PTR(-EINVAL);
		old = tsk->nsproxy->pid_ns_for_children;

		if (task_active_pid_ns(current) != old)
			return ERR_PTR(-EINVAL);
		new_pid = droid_lkm_pidns_create(old);
		if (IS_ERR(new_pid)) {
			droid_lkm_warn("nsproxy: pidns create failed: %ld\n",
				PTR_ERR(new_pid));
			return (struct nsproxy *)new_pid;
		}
	}

	if (want_ipc) {
		new_ipc = droid_lkm_ipcns_create();
		if (IS_ERR(new_ipc)) {
			droid_lkm_warn("nsproxy: ipcns create failed: %ld\n",
				PTR_ERR(new_ipc));
			droid_lkm_pidns_put(new_pid);
			return (struct nsproxy *)new_ipc;
		}
	}

	nsp = droid_lkm_cnn_orig(flags & ~(CLONE_NEWPID | CLONE_NEWIPC), tsk, user_ns,
			  fs);
	if (IS_ERR(nsp)) {
		droid_lkm_warn("nsproxy: kernel create failed: %ld\n", PTR_ERR(nsp));
		droid_lkm_pidns_put(new_pid);
		droid_lkm_ipcns_put(new_ipc);
		return nsp;
	}

	if (want_pid)
		nsp->pid_ns_for_children = new_pid;
	if (want_ipc)
		nsp->ipc_ns = new_ipc;

	droid_lkm_dbg("nsproxy: flags=0x%lx pid=%d ipc=%d -> %p\n", flags,
	       (int)want_pid, (int)want_ipc, nsp);
	return nsp;
}

static int (*droid_lkm_cn_orig)(unsigned long flags, struct task_struct *tsk);
static struct hk_inline droid_lkm_cn_hook;
static bool droid_lkm_cn_hooked;

__nocfi noinline int droid_lkm_cn_wrap(unsigned long flags, struct task_struct *tsk)
{
	bool want_pid = flags & CLONE_NEWPID;
	bool want_ipc = flags & CLONE_NEWIPC;
	unsigned long inner = flags & ~(CLONE_NEWPID | CLONE_NEWIPC);
	struct pid_namespace *new_pid = NULL;
	struct ipc_namespace *new_ipc = NULL;
	struct nsproxy *priv;
	int ret;

	if (!want_pid && !want_ipc)
		return droid_lkm_cn_orig(flags, tsk);


	if (!droid_lkm_ks.switch_task_namespaces)
		return droid_lkm_cn_orig(flags, tsk);

	if (!droid_lkm_gate_allow())
		return droid_lkm_cn_orig(flags, tsk);


	if ((flags & (CLONE_NEWIPC | CLONE_SYSVSEM)) ==
	    (CLONE_NEWIPC | CLONE_SYSVSEM))
		return droid_lkm_cn_orig(flags, tsk);

	if (want_pid) {
		struct pid_namespace *old;

		if (!tsk->nsproxy)
			return -EINVAL;
		old = tsk->nsproxy->pid_ns_for_children;

		if (task_active_pid_ns(current) != old)
			return -EINVAL;
		new_pid = droid_lkm_pidns_create(old);
		if (IS_ERR(new_pid))
			return PTR_ERR(new_pid);
	}

	if (want_ipc) {
		new_ipc = droid_lkm_ipcns_create();
		if (IS_ERR(new_ipc)) {
			droid_lkm_pidns_put(new_pid);
			return PTR_ERR(new_ipc);
		}
	}

	ret = droid_lkm_cn_orig(inner, tsk);
	if (ret) {
		droid_lkm_pidns_put(new_pid);
		droid_lkm_ipcns_put(new_ipc);
		return ret;
	}


	if (!(inner & DROID_LKM_NS_FLAGS)) {
		priv = droid_lkm_cnn_orig(0, tsk, current_user_ns(), tsk->fs);
		if (IS_ERR(priv)) {
			droid_lkm_pidns_put(new_pid);
			droid_lkm_ipcns_put(new_ipc);
			return PTR_ERR(priv);
		}
		droid_lkm_ks.switch_task_namespaces(tsk, priv);
	}

	if (want_pid)
		tsk->nsproxy->pid_ns_for_children = new_pid;
	if (want_ipc)
		tsk->nsproxy->ipc_ns = new_ipc;

	droid_lkm_dbg("clone ns: flags=0x%lx pid=%d ipc=%d -> %p\n", flags,
	       (int)want_pid, (int)want_ipc, tsk->nsproxy);
	return 0;
}

static bool droid_lkm_unshare_precheck(unsigned long flags)
{
	unsigned long f = flags;

	if (f & CLONE_NEWUSER)
		f |= CLONE_THREAD | CLONE_FS;
	if (f & CLONE_VM)
		f |= CLONE_SIGHAND;
	if (f & CLONE_SIGHAND)
		f |= CLONE_THREAD;
	if (f & CLONE_NEWNS)
		f |= CLONE_FS;

	if (droid_lkm_check_unshare_flags_fn && droid_lkm_check_unshare_flags_fn(f))
		return false;


	if ((flags & DROID_LKM_NS_ALL) &&
	    !ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return false;

	if ((flags & CLONE_NEWPID) && current->nsproxy &&
	    task_active_pid_ns(current) !=
		    current->nsproxy->pid_ns_for_children)
		return false;

	return true;
}

static long droid_lkm_unshare_legacy(const struct pt_regs *regs)
{
	struct pt_regs tmp = *regs;
	unsigned long flags = tmp.regs[0];
	bool want_pid = flags & CLONE_NEWPID;
	bool want_ipc = flags & CLONE_NEWIPC;
	long ret;

	tmp.regs[0] = flags & ~(CLONE_NEWPID | CLONE_NEWIPC);
	if (!(tmp.regs[0] & DROID_LKM_NS_FLAGS))
		tmp.regs[0] |= CLONE_NEWUTS;

	ret = droid_lkm_orig_unshare(&tmp);
	if (ret)
		return ret;

	if (flags & (CLONE_NEWIPC | CLONE_SYSVSEM))
		droid_lkm_exit_sem(current);
	if (want_ipc)
		droid_lkm_exit_shm(current);

	if (want_ipc) {
		ret = droid_lkm_slot_install_ipcns();
		if (ret)
			return ret;
	}
	if (want_pid)
		ret = droid_lkm_slot_install_pidns();

	droid_lkm_keepalive_pin();
	return ret;
}

static long droid_lkm_sys_unshare(const struct pt_regs *regs)
{
	unsigned long flags = regs->regs[0];
	long ret;


	if (!(flags & (CLONE_NEWPID | CLONE_NEWIPC | CLONE_SYSVSEM)))
		return droid_lkm_orig_unshare(regs);

	if (!droid_lkm_gate_allow()) {
		droid_lkm_dbg("unshare: %s[%d] flags=0x%lx NOT ours -> kernel\n",
		       current->comm, current->pid, flags);
		return droid_lkm_orig_unshare(regs);
	}

	if (droid_lkm_no_fake_ns) {
		droid_lkm_warn("no_fake_ns=1: %s[%d] unshare flags=0x%lx -> fake success (nothing attached)\n",
			current->comm, current->pid, flags);
		return 0;
	}

	droid_lkm_dbg("unshare: %s[%d] flags=0x%lx pid=%d ipc=%d sysvsem=%d wrap=%d\n",
	       current->comm, current->pid, flags, !!(flags & CLONE_NEWPID),
	       !!(flags & CLONE_NEWIPC), !!(flags & CLONE_SYSVSEM),
	       (int)droid_lkm_cnn_hooked);

	if (!droid_lkm_cnn_hooked)
		return droid_lkm_unshare_legacy(regs);



	if (!droid_lkm_unshare_precheck(flags))
		return droid_lkm_orig_unshare(regs);

	if (flags & (CLONE_NEWIPC | CLONE_SYSVSEM))
		droid_lkm_exit_sem(current);
	if (flags & CLONE_NEWIPC)
		droid_lkm_exit_shm(current);


	ret = droid_lkm_orig_unshare(regs);


	if (!ret)
		droid_lkm_keepalive_pin();
	return ret;
}

bool droid_lkm_slot_skip_sysvipc(void) { return droid_lkm_skip_sysvipc; }
bool droid_lkm_slot_no_fake_ns(void) { return droid_lkm_no_fake_ns; }

static long droid_lkm_sys_reboot(const struct pt_regs *regs)
{
	int magic1 = (int)regs->regs[0];
	int magic2 = (int)regs->regs[1];
	unsigned int cmd = (unsigned int)regs->regs[2];
	struct pid_namespace *ns = task_active_pid_ns(current);
	long ret;

	if (!droid_lkm_pidns_is_ours(ns))
		return droid_lkm_orig_reboot(regs);



	if (magic1 != LINUX_REBOOT_MAGIC1 ||
	    (magic2 != LINUX_REBOOT_MAGIC2 && magic2 != LINUX_REBOOT_MAGIC2A &&
	     magic2 != LINUX_REBOOT_MAGIC2B && magic2 != LINUX_REBOOT_MAGIC2C))
		return droid_lkm_orig_reboot(regs);

	ret = droid_lkm_pidns_reboot(ns, cmd);
	if (ret)
		return ret;


	send_sig(SIGKILL, current, 1);
	return 0;
}

static int droid_lkm_nsproxy_hook_install(void)
{
	int ret;

	droid_lkm_check_unshare_flags_fn =
		(int (*)(unsigned long))droid_lkm_sym("check_unshare_flags");

	ret = hk_inline_hook(&droid_lkm_cnn_hook, "create_new_namespaces",
			     "droid_lkm_cnn_wrap");
	if (ret) {
		droid_lkm_warn("nsproxy hook unavailable (%d): clone/clone3 CLONE_NEWPID|NEWIPC stay EINVAL, unshare uses the legacy path\n",
			ret);
		return ret;
	}

	droid_lkm_cnn_orig = (typeof(droid_lkm_cnn_orig))droid_lkm_cnn_hook.orig;
	droid_lkm_cnn_hooked = true;
	droid_lkm_info("nsproxy hook: create_new_namespaces 0x%lx wrapped (orig=0x%lx check_unshare_flags=0x%lx)\n",
		droid_lkm_cnn_hook.addr, droid_lkm_cnn_hook.orig,
		(unsigned long)droid_lkm_check_unshare_flags_fn);
	return 0;
}

struct droid_lkm_clone_args3 {
	unsigned long long flags, pidfd, child_tid, parent_tid;
	unsigned long long exit_signal, stack, stack_size, tls;
	unsigned long long set_tid, set_tid_size, cgroup;
};

struct droid_lkm_clone_ctx {
	struct nsproxy *priv;
	unsigned long save_pid, save_ipc;
};

__nocfi noinline int droid_lkm_clone_ctx_enter(unsigned long flags,
				      struct droid_lkm_clone_ctx *ctx)
{
	bool want_pid = flags & CLONE_NEWPID;
	bool want_ipc = flags & CLONE_NEWIPC;
	struct pid_namespace *new_pid = NULL;
	struct ipc_namespace *new_ipc = NULL;

	ctx->priv = NULL;

	if (want_pid) {
		struct pid_namespace *old;

		if (!current->nsproxy)
			return -EINVAL;
		old = current->nsproxy->pid_ns_for_children;

		if (task_active_pid_ns(current) != old)
			return -EINVAL;
		new_pid = droid_lkm_pidns_create(old);
		if (IS_ERR(new_pid))
			return PTR_ERR(new_pid);
	}

	if (want_ipc) {
		new_ipc = droid_lkm_ipcns_create();
		if (IS_ERR(new_ipc)) {
			droid_lkm_pidns_put(new_pid);
			return PTR_ERR(new_ipc);
		}
	}

	ctx->priv = droid_lkm_cnn_orig(0, current, current_user_ns(), current->fs);
	if (IS_ERR(ctx->priv)) {
		droid_lkm_pidns_put(new_pid);
		droid_lkm_ipcns_put(new_ipc);
		return PTR_ERR(ctx->priv);
	}

	droid_lkm_ks.switch_task_namespaces(current, ctx->priv);
	ctx->save_pid = (unsigned long)ctx->priv->pid_ns_for_children;
	ctx->save_ipc = (unsigned long)ctx->priv->ipc_ns;
	if (want_pid)
		ctx->priv->pid_ns_for_children = new_pid;
	if (want_ipc)
		ctx->priv->ipc_ns = new_ipc;
	return 0;
}

__nocfi noinline void droid_lkm_clone_ctx_leave(struct droid_lkm_clone_ctx *ctx)
{
	struct nsproxy *priv2;

	if (!ctx->priv)
		return;

	priv2 = droid_lkm_cnn_orig(0, current, current_user_ns(), current->fs);
	if (IS_ERR(priv2)) {
		current->nsproxy->pid_ns_for_children =
			(struct pid_namespace *)ctx->save_pid;
		current->nsproxy->ipc_ns = (struct ipc_namespace *)ctx->save_ipc;
		return;
	}

	priv2->pid_ns_for_children = (struct pid_namespace *)ctx->save_pid;
	priv2->ipc_ns = (struct ipc_namespace *)ctx->save_ipc;
	droid_lkm_ks.switch_task_namespaces(current, priv2);
}

static int droid_lkm_clone_ns_check(unsigned long flags)
{
	if ((flags & CLONE_THREAD) && (flags & (CLONE_NEWUSER | CLONE_NEWPID)))
		return -EINVAL;
	if ((flags & (CLONE_NEWIPC | CLONE_SYSVSEM)) ==
	    (CLONE_NEWIPC | CLONE_SYSVSEM))
		return -EINVAL;
	return 0;
}

static bool droid_lkm_clone_ns_ready(unsigned long flags)
{
	return (flags & (CLONE_NEWPID | CLONE_NEWIPC)) && droid_lkm_cnn_hooked &&
	       droid_lkm_ks.switch_task_namespaces && droid_lkm_gate_allow();
}

static long droid_lkm_sys_clone(const struct pt_regs *regs)
{
	struct droid_lkm_clone_ctx ctx;
	struct pt_regs tmp;
	unsigned long flags = regs->regs[0];
	int ret;

	if (!droid_lkm_clone_ns_ready(flags))
		return droid_lkm_orig_clone(regs);
	ret = droid_lkm_clone_ns_check(flags);
	if (ret)
		return ret;

	ret = droid_lkm_clone_ctx_enter(flags, &ctx);
	if (ret)
		return ret;

	tmp = *regs;
	tmp.regs[0] = flags & ~(CLONE_NEWPID | CLONE_NEWIPC);
	ret = droid_lkm_orig_clone(&tmp);

	droid_lkm_clone_ctx_leave(&ctx);
	return ret;
}

static long droid_lkm_sys_clone3(const struct pt_regs *regs)
{
	void __user *uargs = (void __user *)regs->regs[0];
	size_t usize = (size_t)regs->regs[1];
	struct droid_lkm_clone_args3 ua, ua2;
	struct droid_lkm_clone_ctx ctx;
	unsigned long flags;
	size_t n;
	int ret;

	if (!uargs || usize < sizeof(unsigned long long))
		return droid_lkm_orig_clone3(regs);

	n = usize < sizeof(ua) ? usize : sizeof(ua);
	memset(&ua, 0, sizeof(ua));
	if (copy_from_user(&ua, uargs, n))
		return droid_lkm_orig_clone3(regs);

	flags = ua.flags;
	if (!droid_lkm_clone_ns_ready(flags))
		return droid_lkm_orig_clone3(regs);
	ret = droid_lkm_clone_ns_check(flags);
	if (ret)
		return ret;

	ret = droid_lkm_clone_ctx_enter(flags, &ctx);
	if (ret)
		return ret;

	ua2 = ua;
	ua2.flags = flags & ~(CLONE_NEWPID | CLONE_NEWIPC);
	if (copy_to_user(uargs, &ua2, n)) {
		droid_lkm_clone_ctx_leave(&ctx);
		return -EFAULT;
	}

	ret = droid_lkm_orig_clone3(regs);
	(void)copy_to_user(uargs, &ua, n);

	droid_lkm_clone_ctx_leave(&ctx);
	return ret;
}

static int droid_lkm_clone_slot_install(void)
{
	int ret;

	ret = droid_lkm_slot_patch(__NR_clone, (unsigned long)droid_lkm_sys_clone,
		       (unsigned long *)&droid_lkm_orig_clone);
	if (ret) {
		droid_lkm_warn("cannot patch clone slot: %d\n", ret);
		return ret;
	}

	ret = droid_lkm_slot_patch(__NR_clone3, (unsigned long)droid_lkm_sys_clone3,
		       (unsigned long *)&droid_lkm_orig_clone3);
	if (ret) {
		droid_lkm_warn("cannot patch clone3 slot: %d\n", ret);
		droid_lkm_slot_unpatch(__NR_clone);
		return ret;
	}

	droid_lkm_info("clone slots patched: clone=%d clone3=%d\n", __NR_clone,
		__NR_clone3);
	return 0;
}

static int droid_lkm_copy_namespaces_hook_install(void)
{
	int ret = hk_inline_hook(&droid_lkm_cn_hook, "copy_namespaces", "droid_lkm_cn_wrap");

	if (ret) {
		droid_lkm_warn("copy_namespaces hook unavailable (%d)\n", ret);
		return ret;
	}

	droid_lkm_cn_orig = (typeof(droid_lkm_cn_orig))droid_lkm_cn_hook.orig;
	droid_lkm_cn_hooked = true;
	droid_lkm_info("ns hook: copy_namespaces 0x%lx wrapped (orig=0x%lx)\n",
		droid_lkm_cn_hook.addr, droid_lkm_cn_hook.orig);
	return 0;
}

static struct sc_cfg droid_lkm_sc_cfg;
static const struct sc_layout droid_lkm_sc_layout = {
	.resolve = droid_lkm_sc_resolve,
};

int droid_lkm_slot_init(void)
{
	int ret;

	droid_lkm_sys_call_table =
		(unsigned long *)droid_lkm_sym("sys_call_table");

	memset(&droid_lkm_sc_cfg, 0, sizeof(droid_lkm_sc_cfg));
	droid_lkm_sc_cfg.layout = &droid_lkm_sc_layout;
	droid_lkm_sc_cfg.no_patch = true;
	strscpy(droid_lkm_sc_cfg.key, "droid_lkm", sizeof(droid_lkm_sc_cfg.key));

	ret = sc_init(&droid_lkm_sc_cfg);
	if (ret) {
		droid_lkm_err("sc_init failed: %d\n", ret);
		return ret;
	}

	ret = droid_lkm_slot_patch(__NR_unshare, (unsigned long)droid_lkm_sys_unshare,
		       (unsigned long *)&droid_lkm_orig_unshare);
	if (ret) {
		droid_lkm_err("cannot patch unshare slot: %d\n", ret);
		goto err_sc;
	}

	ret = droid_lkm_slot_patch(__NR_reboot, (unsigned long)droid_lkm_sys_reboot,
		       (unsigned long *)&droid_lkm_orig_reboot);
	if (ret) {
		droid_lkm_err("cannot patch reboot slot: %d\n", ret);
		droid_lkm_slot_unpatch(__NR_unshare);
		goto err_sc;
	}

	ret = droid_lkm_slot_patch_ipc();
	if (ret)
		droid_lkm_warn("sysvipc entries not wired: %d\n", ret);

	droid_lkm_nsproxy_hook_install();
	droid_lkm_copy_namespaces_hook_install();
	droid_lkm_clone_slot_install();

	droid_lkm_info("syscall slots patched: unshare=%d reboot=%d\n", __NR_unshare,
		__NR_reboot);
	return 0;

err_sc:
	sc_exit();
	return ret;
}

void droid_lkm_slot_exit(void)
{
	int i;

	if (droid_lkm_cnn_hooked) {
		hk_inline_unhook(&droid_lkm_cnn_hook);
		droid_lkm_cnn_hooked = false;
	}
	if (droid_lkm_cn_hooked) {
		hk_inline_unhook(&droid_lkm_cn_hook);
		droid_lkm_cn_hooked = false;
	}

	for (i = 0; i < ARRAY_SIZE(droid_lkm_ipc_slots); i++) {
		if (droid_lkm_ipc_patched[i]) {
			droid_lkm_slot_unpatch(droid_lkm_ipc_slots[i].nr);
			droid_lkm_ipc_patched[i] = 0;
		}
	}

	if (droid_lkm_orig_clone3)
		droid_lkm_slot_unpatch(__NR_clone3);
	if (droid_lkm_orig_clone)
		droid_lkm_slot_unpatch(__NR_clone);
	if (droid_lkm_orig_reboot)
		droid_lkm_slot_unpatch(__NR_reboot);
	if (droid_lkm_orig_unshare)
		droid_lkm_slot_unpatch(__NR_unshare);
	sc_exit();
}

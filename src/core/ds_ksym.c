// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/sched/signal.h>
#include <linux/nsproxy.h>

#include "ds.h"
#include "ds_ksym.h"

struct droid_lkm_ksym droid_lkm_ks = { };
int *droid_lkm_ks_sysctl_overcommit_memory;
rwlock_t *droid_lkm_ks_tasklist_lock;
const struct proc_ns_operations *droid_lkm_ks_ipcns_operations;

/*
 * the count is reported in one line, then one line per symbol naming the helper
 * and what stops working without it. an unnamed count means the next device
 * report cannot say which helper is the one that takes a feature down
 */
#define DROID_LKM_MISSING_MAX 40
struct droid_lkm_missing {
	const char *name;
	const char *lost;
};
static struct droid_lkm_missing droid_lkm_missing[DROID_LKM_MISSING_MAX];
static int droid_lkm_missing_cnt;

static void droid_lkm_ksym_missing(const char *name, const char *lost)
{
	if (droid_lkm_missing_cnt < DROID_LKM_MISSING_MAX) {
		droid_lkm_missing[droid_lkm_missing_cnt].name = name;
		droid_lkm_missing[droid_lkm_missing_cnt].lost = lost;
	}
	droid_lkm_missing_cnt++;
}

static unsigned long __nocfi droid_lkm_resolve(const char *name)
{
	return droid_lkm_sym(name);
}

/* a symbol only one kernel generation carries, absence is expected */
#define DROID_LKM_THUNK_OPT(_field, _name, _type)                             \
	do {                                                                   \
		droid_lkm_ks._field = (_type)droid_lkm_resolve(_name);                \
	} while (0)

#define DROID_LKM_THUNK(_field, _name, _type)                                         \
	do {                                                                   \
		droid_lkm_ks._field = (_type)droid_lkm_resolve(_name);                       \
		if (!droid_lkm_ks._field)                                             \
			droid_lkm_warn("thunk missing: %s\n", _name);                 \
	} while (0)

int droid_lkm_ksym_init(void)
{
	int missing = 0;

	DROID_LKM_THUNK(proc_alloc_inum, "proc_alloc_inum", typeof(droid_lkm_ks.proc_alloc_inum));
	DROID_LKM_THUNK(proc_free_inum, "proc_free_inum", typeof(droid_lkm_ks.proc_free_inum));
	DROID_LKM_THUNK(disable_pid_allocation, "disable_pid_allocation",
		 typeof(droid_lkm_ks.disable_pid_allocation));
	DROID_LKM_THUNK(group_send_sig_info, "group_send_sig_info",
		 typeof(droid_lkm_ks.group_send_sig_info));
	DROID_LKM_THUNK(kernel_wait4, "kernel_wait4", typeof(droid_lkm_ks.kernel_wait4));
	DROID_LKM_THUNK(do_mmap, "do_mmap", typeof(droid_lkm_ks.do_mmap));
	DROID_LKM_THUNK(do_mmap_legacy, "do_mmap",
			typeof(droid_lkm_ks.do_mmap_legacy));
	DROID_LKM_THUNK_OPT(__do_munmap, "__do_munmap",
			    typeof(droid_lkm_ks.__do_munmap));
	DROID_LKM_THUNK_OPT(do_mas_munmap, "do_mas_munmap",
			    typeof(droid_lkm_ks.do_mas_munmap));
	DROID_LKM_THUNK_OPT(do_vmi_munmap, "do_vmi_munmap",
			    typeof(droid_lkm_ks.do_vmi_munmap));
	DROID_LKM_THUNK(wake_q_add, "wake_q_add", typeof(droid_lkm_ks.wake_q_add));
	DROID_LKM_THUNK(wake_q_add_safe, "wake_q_add_safe", typeof(droid_lkm_ks.wake_q_add_safe));
	DROID_LKM_THUNK(wake_up_q, "wake_up_q", typeof(droid_lkm_ks.wake_up_q));
	DROID_LKM_THUNK(schedule_hrtimeout_range, "schedule_hrtimeout_range",
		 typeof(droid_lkm_ks.schedule_hrtimeout_range));
	DROID_LKM_THUNK(get_timespec64, "get_timespec64", typeof(droid_lkm_ks.get_timespec64));
	DROID_LKM_THUNK(get_old_timespec32, "get_old_timespec32",
		 typeof(droid_lkm_ks.get_old_timespec32));
	DROID_LKM_THUNK(__percpu_counter_sum, "__percpu_counter_sum",
		 typeof(droid_lkm_ks.__percpu_counter_sum));
	DROID_LKM_THUNK(shmem_lock, "shmem_lock", typeof(droid_lkm_ks.shmem_lock));
	DROID_LKM_THUNK(shmem_kernel_file_setup, "shmem_kernel_file_setup",
		 typeof(droid_lkm_ks.shmem_kernel_file_setup));
	DROID_LKM_THUNK(shmem_unlock_mapping, "shmem_unlock_mapping",
		 typeof(droid_lkm_ks.shmem_unlock_mapping));
	DROID_LKM_THUNK(alloc_file_clone, "alloc_file_clone",
		 typeof(droid_lkm_ks.alloc_file_clone));
	DROID_LKM_THUNK(__mm_populate, "__mm_populate", typeof(droid_lkm_ks.__mm_populate));
	DROID_LKM_THUNK(switch_task_namespaces, "switch_task_namespaces",
		 typeof(droid_lkm_ks.switch_task_namespaces));
	DROID_LKM_THUNK(inode_permission, "inode_permission",
		 typeof(droid_lkm_ks.inode_permission));
	DROID_LKM_THUNK(mnt_want_write, "mnt_want_write",
		 typeof(droid_lkm_ks.mnt_want_write));
	DROID_LKM_THUNK(mnt_drop_write, "mnt_drop_write",
		 typeof(droid_lkm_ks.mnt_drop_write));
	DROID_LKM_THUNK(lookup_one_len, "lookup_one_len",
		 typeof(droid_lkm_ks.lookup_one_len));
	DROID_LKM_THUNK(get_tree_nodev, "get_tree_nodev",
		 typeof(droid_lkm_ks.get_tree_nodev));

	DROID_LKM_THUNK(security_ipc_permission, "security_ipc_permission",
		 typeof(droid_lkm_ks.security_ipc_permission));
	DROID_LKM_THUNK(security_mmap_file, "security_mmap_file",
		 typeof(droid_lkm_ks.security_mmap_file));
	DROID_LKM_THUNK(security_msg_msg_alloc, "security_msg_msg_alloc",
		 typeof(droid_lkm_ks.security_msg_msg_alloc));
	DROID_LKM_THUNK(security_msg_msg_free, "security_msg_msg_free",
		 typeof(droid_lkm_ks.security_msg_msg_free));
	DROID_LKM_THUNK(security_msg_queue_alloc, "security_msg_queue_alloc",
		 typeof(droid_lkm_ks.security_msg_queue_alloc));
	DROID_LKM_THUNK(security_msg_queue_free, "security_msg_queue_free",
		 typeof(droid_lkm_ks.security_msg_queue_free));
	DROID_LKM_THUNK(security_msg_queue_associate, "security_msg_queue_associate",
		 typeof(droid_lkm_ks.security_msg_queue_associate));
	DROID_LKM_THUNK(security_msg_queue_msgctl, "security_msg_queue_msgctl",
		 typeof(droid_lkm_ks.security_msg_queue_msgctl));
	DROID_LKM_THUNK(security_msg_queue_msgsnd, "security_msg_queue_msgsnd",
		 typeof(droid_lkm_ks.security_msg_queue_msgsnd));
	DROID_LKM_THUNK(security_msg_queue_msgrcv, "security_msg_queue_msgrcv",
		 typeof(droid_lkm_ks.security_msg_queue_msgrcv));
	DROID_LKM_THUNK(security_shm_alloc, "security_shm_alloc",
		 typeof(droid_lkm_ks.security_shm_alloc));
	DROID_LKM_THUNK(security_shm_free, "security_shm_free",
		 typeof(droid_lkm_ks.security_shm_free));
	DROID_LKM_THUNK(security_shm_associate, "security_shm_associate",
		 typeof(droid_lkm_ks.security_shm_associate));
	DROID_LKM_THUNK(security_shm_shmctl, "security_shm_shmctl",
		 typeof(droid_lkm_ks.security_shm_shmctl));
	DROID_LKM_THUNK(security_shm_shmat, "security_shm_shmat",
		 typeof(droid_lkm_ks.security_shm_shmat));
	DROID_LKM_THUNK(security_sem_alloc, "security_sem_alloc",
		 typeof(droid_lkm_ks.security_sem_alloc));
	DROID_LKM_THUNK(security_sem_free, "security_sem_free",
		 typeof(droid_lkm_ks.security_sem_free));
	DROID_LKM_THUNK(security_sem_associate, "security_sem_associate",
		 typeof(droid_lkm_ks.security_sem_associate));
	DROID_LKM_THUNK(security_sem_semctl, "security_sem_semctl",
		 typeof(droid_lkm_ks.security_sem_semctl));
	DROID_LKM_THUNK(security_sem_semop, "security_sem_semop",
		 typeof(droid_lkm_ks.security_sem_semop));
	DROID_LKM_THUNK(__audit_ipc_obj, "__audit_ipc_obj",
		 typeof(droid_lkm_ks.__audit_ipc_obj));
	DROID_LKM_THUNK(__audit_ipc_set_perm, "__audit_ipc_set_perm",
		 typeof(droid_lkm_ks.__audit_ipc_set_perm));
	DROID_LKM_THUNK(__audit_inode, "__audit_inode",
		 typeof(droid_lkm_ks.__audit_inode));
	DROID_LKM_THUNK(__audit_file, "__audit_file",
		 typeof(droid_lkm_ks.__audit_file));
	DROID_LKM_THUNK(__audit_mq_open, "__audit_mq_open",
		 typeof(droid_lkm_ks.__audit_mq_open));
	DROID_LKM_THUNK(__audit_mq_sendrecv, "__audit_mq_sendrecv",
		 typeof(droid_lkm_ks.__audit_mq_sendrecv));
	DROID_LKM_THUNK(__audit_mq_notify, "__audit_mq_notify",
		 typeof(droid_lkm_ks.__audit_mq_notify));
	DROID_LKM_THUNK(__audit_mq_getsetattr, "__audit_mq_getsetattr",
		 typeof(droid_lkm_ks.__audit_mq_getsetattr));

	/*
	 * missing on 5.10 only, where the shim drops the mqueue rlimit
	 * accounting instead of the queues, so the warning is the signal
	 */
	DROID_LKM_THUNK(ucounts_get, "get_ucounts",
		 typeof(droid_lkm_ks.ucounts_get));
	DROID_LKM_THUNK(ucounts_put, "put_ucounts",
		 typeof(droid_lkm_ks.ucounts_put));
	DROID_LKM_THUNK(ucounts_inc_rlimit, "inc_rlimit_ucounts",
		 typeof(droid_lkm_ks.ucounts_inc_rlimit));
	DROID_LKM_THUNK(ucounts_dec_rlimit, "dec_rlimit_ucounts",
		 typeof(droid_lkm_ks.ucounts_dec_rlimit));

	droid_lkm_ks_sysctl_overcommit_memory =
		(int *)droid_lkm_resolve("sysctl_overcommit_memory");
	if (!droid_lkm_ks_sysctl_overcommit_memory)
		droid_lkm_warn("thunk missing: sysctl_overcommit_memory\n");

	/*
	 * kernel helpers a stock image may not carry. the shims in this module
	 * define the kernel names, so a name that is absent from the kernel is
	 * refused cleanly instead of failing the load, and the report says which
	 * one and what it costs.
	 */
	missing = 0;
#define DROID_LKM_THUNK_MAYBE(_field, _name, _lost)                            \
	do {                                                                   \
		DROID_LKM_THUNK_OPT(_field, _name,                             \
				    typeof(droid_lkm_ks._field));              \
		if (!droid_lkm_ks._field) {                                    \
			droid_lkm_ksym_missing(_name, _lost);                  \
			missing++;                                             \
		}                                                              \
	} while (0)

	DROID_LKM_THUNK_MAYBE(dentry_open, "dentry_open",
			      "the mqueue shim cannot open its file, mqueue stays off");
	DROID_LKM_THUNK_MAYBE(do_send_sig_info, "do_send_sig_info",
			      "SIGEV_SIGNAL mqueue notifications are not delivered");
	DROID_LKM_THUNK_MAYBE(fc_mount, "fc_mount",
			      "mqueuefs cannot be mounted, mqueue stays off");
	DROID_LKM_THUNK_MAYBE(mntget, "mntget",
			      "a mqueuefs mount cannot take a reference, mqueue stays off");
	DROID_LKM_THUNK_MAYBE(put_fs_context, "put_fs_context",
			      "the fs_context of a failed mqueuefs mount leaks");
	DROID_LKM_THUNK_MAYBE(free_ipcs, "free_ipcs",
			      "SysV ids cannot be released through the kernel, they leak with the namespace");
	DROID_LKM_THUNK_MAYBE(put_ipc_ns, "put_ipc_ns",
			      "an ipc namespace reference cannot be dropped, the namespace leaks");
	DROID_LKM_THUNK_MAYBE(pid_nr_ns, "pid_nr_ns",
			      "SysV status reports pid 0 for the creator and the last user");
	DROID_LKM_THUNK_MAYBE(pid_vnr, "pid_vnr",
			      "SysV status reports pid 0 instead of the pid seen in the namespace");
	DROID_LKM_THUNK_MAYBE(proc_dointvec_minmax, "proc_dointvec_minmax",
			      "the ipc limits under /proc/sys/kernel accept no writes");
	DROID_LKM_THUNK_MAYBE(proc_doulongvec_minmax, "proc_doulongvec_minmax",
			      "shmmax and shmall accept no writes");
	DROID_LKM_THUNK_MAYBE(register_kprobe, "register_kprobe",
			      "the do_exit probe is not installed, a dying container pid is not reaped by it");
	DROID_LKM_THUNK_MAYBE(unregister_kprobe, "unregister_kprobe",
			      "the do_exit probe cannot be removed, unload keeps it registered");
	DROID_LKM_THUNK_MAYBE(kern_path, "kern_path",
			      "the ns_last_pid and /proc/self/ns/mnt lookups fail, those pidns steps are skipped");
	DROID_LKM_THUNK_MAYBE(path_put, "path_put",
			      "a path resolved by the module is never released");
	DROID_LKM_THUNK_MAYBE(d_set_d_op, "d_set_d_op",
			      "the container /proc dentries keep the kernel operations and stay cached longer");
	DROID_LKM_THUNK_MAYBE(down_write_killable, "down_write_killable",
			      "a killable mmap lock wait becomes uninterruptible");
	DROID_LKM_THUNK_MAYBE(radix_tree_tagged, "radix_tree_tagged",
			      "an id lookup cannot take the tagged fast path and scans instead");
	DROID_LKM_THUNK_MAYBE(vfs_unlink, "vfs_unlink",
			      "a named message queue cannot be removed, mqueue stays off");
	DROID_LKM_THUNK_MAYBE(unregister_sysctl_table, "unregister_sysctl_table",
			      "an ipc sysctl table cannot be withdrawn and stays in /proc/sys");
	DROID_LKM_THUNK_MAYBE(copy_ipcs, "copy_ipcs",
			      "a kernel owned ipc namespace cannot be copied, it is refused instead");

	/*
	 * present in the running kernel but kept out of the plain export table
	 * on 6.18, where they sit in the ANDROID_GKI_VFS namespace and are
	 * trimmed or protected. the shims in this module define these names, so
	 * the link never asks the loader for them
	 */
	DROID_LKM_THUNK_MAYBE(d_splice_alias, "d_splice_alias",
			      "the container /proc/<pid>/ns entries are never instantiated");
	DROID_LKM_THUNK_MAYBE(in_group_p, "in_group_p",
			      "ipc permission checks never match a supplementary group");
	DROID_LKM_THUNK_MAYBE(ihold, "ihold",
			      "removing a named message queue takes no inode reference");
	DROID_LKM_THUNK_MAYBE(inc_nlink, "inc_nlink",
			      "a message queue directory never gains its link count");
	DROID_LKM_THUNK_MAYBE(drop_nlink, "drop_nlink",
			      "a removed message queue directory keeps its link count");
	DROID_LKM_THUNK_MAYBE(clear_inode, "clear_inode",
			      "evicting a message queue inode leaves its state behind");
	DROID_LKM_THUNK_MAYBE(d_instantiate, "d_instantiate",
			      "a new message queue dentry is never bound to its inode");
	DROID_LKM_THUNK_MAYBE(inode_init_once, "inode_init_once",
			      "message queue inodes are never initialized");
	DROID_LKM_THUNK_MAYBE(simple_inode_init_ts, "simple_inode_init_ts",
			      "a new message queue inode keeps zero timestamps");
	DROID_LKM_THUNK_MAYBE(overflowgid_addr, "overflowgid",
			      "an unmapped ipc gid reports 0 instead of the kernel value");

	/*
	 * overflowgid is data: from_kgid_munged() reads it through an inline the
	 * module cannot intercept, so the module copy is filled from the kernel
	 * address resolved above before any module code runs
	 */
	if (droid_lkm_ks.overflowgid_addr)
		overflowgid = *droid_lkm_ks.overflowgid_addr;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	/*
	 * register_sysctl is the one entry point before 6.1. from 6.1 on it is a
	 * macro over register_sysctl_sz, there is no symbol to find, and this
	 * module registers its ipc tables through setup_sysctl_set and
	 * __register_sysctl_table instead, so the absence costs nothing
	 */
	DROID_LKM_THUNK_MAYBE(register_sysctl, "register_sysctl",
			      "the SysV limits cannot be registered, they stay unexposed");
#endif

	/*
	 * the text patch path the hook engine drives. _end and kimage_voffset are
	 * data symbols, so a kernel built without CONFIG_KALLSYMS_ALL does not
	 * carry them, and the engine then cannot tell a kernel image address from
	 * a vmalloc one
	 */
	DROID_LKM_THUNK_MAYBE(text_start, "_text",
			      "inline hooks have no way to translate a kernel image address");
	DROID_LKM_THUNK_MAYBE(text_end, "_end",
			      "inline hooks have no way to tell an image address from a module one");
	DROID_LKM_THUNK_MAYBE(kimage_voffset, "kimage_voffset",
			      "inline hooks on kernel image addresses are refused");
	DROID_LKM_THUNK_MAYBE(vmalloc_to_pfn, "vmalloc_to_pfn",
			      "inline hooks on module and vmalloc addresses are refused");
	DROID_LKM_THUNK_MAYBE(set_fixmap, "__set_fixmap",
			      "inline hooks cannot open the writable alias, all text patches are refused");
#undef DROID_LKM_THUNK_MAYBE

	droid_lkm_ks_tasklist_lock = (rwlock_t *)droid_lkm_resolve("tasklist_lock");
	if (!droid_lkm_ks_tasklist_lock) {
		droid_lkm_ksym_missing("tasklist_lock",
				       "the pid namespace task walk cannot take the kernel lock");
		missing++;
	}
	droid_lkm_ks_ipcns_operations =
		(const struct proc_ns_operations *)droid_lkm_resolve("ipcns_operations");
	if (!droid_lkm_ks_ipcns_operations) {
		droid_lkm_ksym_missing("ipcns_operations",
				       "the host ipc namespace keeps the kernel operations, ipc isolation stays off");
		missing++;
	}

	if (missing) {
		int i;

		droid_lkm_warn("%d kernel helper(s) missing from kallsyms, each feature that needs one refuses cleanly:\n",
			       missing);
		for (i = 0; i < droid_lkm_missing_cnt &&
			    i < ARRAY_SIZE(droid_lkm_missing); i++)
			droid_lkm_warn("%s: %s\n", droid_lkm_missing[i].name,
				       droid_lkm_missing[i].lost);
	}

	if (!droid_lkm_ks.proc_alloc_inum || !droid_lkm_ks.proc_free_inum ||
	    !droid_lkm_ks.disable_pid_allocation || !droid_lkm_ks.group_send_sig_info ||
	    !droid_lkm_ks.kernel_wait4) {
		droid_lkm_err("required thunks unresolved\n");
		return -ENODATA;
	}

	return 0;
}

/*
 * the hook engine turns a hooked address into a physical one: _text/_end tell an
 * image address from a module one, kimage_voffset converts the former and
 * vmalloc_to_pfn the latter, __set_fixmap opens the writable alias the copy goes
 * through. the engine resolves them itself and falls back silently, so a kernel
 * that hides _end (a data symbol, gone when CONFIG_KALLSYMS_ALL=n) has every
 * image page translated by vmalloc_to_pfn, which cannot walk a block mapping and
 * returns an unrelated pfn. refuse the inline hooks here instead
 */
bool droid_lkm_text_patch_ready(void)
{
	const struct {
		unsigned long addr;
		const char *name;
	} need[] = {
		{ droid_lkm_ks.text_start, "_text" },
		{ droid_lkm_ks.text_end, "_end" },
		{ droid_lkm_ks.kimage_voffset, "kimage_voffset" },
		{ droid_lkm_ks.vmalloc_to_pfn, "vmalloc_to_pfn" },
		{ droid_lkm_ks.set_fixmap, "__set_fixmap" },
	};
	int i;
	int n = 0;

	for (i = 0; i < ARRAY_SIZE(need); i++)
		if (!need[i].addr)
			n++;
	if (!n)
		return true;

	droid_lkm_warn("inline hooks off, the text patch path is missing:");
	for (i = 0; i < ARRAY_SIZE(need); i++)
		if (!need[i].addr)
			pr_cont(" %s", need[i].name);
	pr_cont("\n");
	return false;
}

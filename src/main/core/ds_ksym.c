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
#include "ds_shapes.h"
#include "ds_ksym.h"

struct droid_lkm_ksym droid_lkm_ks = { };
int *droid_lkm_ks_sysctl_overcommit_memory;
rwlock_t *droid_lkm_ks_tasklist_lock;
const struct proc_ns_operations *droid_lkm_ks_ipcns_operations;

/*
 * the module's kernel symbol table, handed to the compat layer at load. the
 * engine is shared with the other modules, this list is what makes it main's
 */
static struct ds_compat_sym droid_lkm_syms[] = {
	{ "proc_alloc_inum", (void **)&droid_lkm_ks.proc_alloc_inum },
	{ "proc_free_inum", (void **)&droid_lkm_ks.proc_free_inum },
	{ "disable_pid_allocation", (void **)&droid_lkm_ks.disable_pid_allocation },
	{ "group_send_sig_info", (void **)&droid_lkm_ks.group_send_sig_info },
	{ "kernel_wait4", (void **)&droid_lkm_ks.kernel_wait4 },
	{ "do_mmap", (void **)&droid_lkm_ks.do_mmap },
	{ "do_mmap", (void **)&droid_lkm_ks.do_mmap_legacy },
	{ "__do_munmap", (void **)&droid_lkm_ks.__do_munmap },
	{ "do_mas_munmap", (void **)&droid_lkm_ks.do_mas_munmap },
	{ "do_vmi_munmap", (void **)&droid_lkm_ks.do_vmi_munmap },
	{ "wake_q_add", (void **)&droid_lkm_ks.wake_q_add },
	{ "wake_q_add_safe", (void **)&droid_lkm_ks.wake_q_add_safe },
	{ "wake_up_q", (void **)&droid_lkm_ks.wake_up_q },
	{ "schedule_hrtimeout_range", (void **)&droid_lkm_ks.schedule_hrtimeout_range },
	{ "get_timespec64", (void **)&droid_lkm_ks.get_timespec64 },
	{ "get_old_timespec32", (void **)&droid_lkm_ks.get_old_timespec32 },
	{ "__percpu_counter_sum", (void **)&droid_lkm_ks.__percpu_counter_sum },
	{ "shmem_lock", (void **)&droid_lkm_ks.shmem_lock },
	{ "shmem_kernel_file_setup", (void **)&droid_lkm_ks.shmem_kernel_file_setup },
	{ "shmem_unlock_mapping", (void **)&droid_lkm_ks.shmem_unlock_mapping },
	{ "alloc_file_clone", (void **)&droid_lkm_ks.alloc_file_clone },
	{ "__mm_populate", (void **)&droid_lkm_ks.__mm_populate },
	{ "switch_task_namespaces", (void **)&droid_lkm_ks.switch_task_namespaces },
	{ "inode_permission", (void **)&droid_lkm_ks.inode_permission },
	{ "mnt_want_write", (void **)&droid_lkm_ks.mnt_want_write },
	{ "mnt_drop_write", (void **)&droid_lkm_ks.mnt_drop_write },
	{ "lookup_one_len", (void **)&droid_lkm_ks.lookup_one_len },
	{ "get_tree_nodev", (void **)&droid_lkm_ks.get_tree_nodev },
	{ "security_ipc_permission", (void **)&droid_lkm_ks.security_ipc_permission },
	{ "security_mmap_file", (void **)&droid_lkm_ks.security_mmap_file },
	{ "security_msg_msg_alloc", (void **)&droid_lkm_ks.security_msg_msg_alloc },
	{ "security_msg_msg_free", (void **)&droid_lkm_ks.security_msg_msg_free },
	{ "security_msg_queue_alloc", (void **)&droid_lkm_ks.security_msg_queue_alloc },
	{ "security_msg_queue_free", (void **)&droid_lkm_ks.security_msg_queue_free },
	{ "security_msg_queue_associate", (void **)&droid_lkm_ks.security_msg_queue_associate },
	{ "security_msg_queue_msgctl", (void **)&droid_lkm_ks.security_msg_queue_msgctl },
	{ "security_msg_queue_msgsnd", (void **)&droid_lkm_ks.security_msg_queue_msgsnd },
	{ "security_msg_queue_msgrcv", (void **)&droid_lkm_ks.security_msg_queue_msgrcv },
	{ "security_shm_alloc", (void **)&droid_lkm_ks.security_shm_alloc },
	{ "security_shm_free", (void **)&droid_lkm_ks.security_shm_free },
	{ "security_shm_associate", (void **)&droid_lkm_ks.security_shm_associate },
	{ "security_shm_shmctl", (void **)&droid_lkm_ks.security_shm_shmctl },
	{ "security_shm_shmat", (void **)&droid_lkm_ks.security_shm_shmat },
	{ "security_sem_alloc", (void **)&droid_lkm_ks.security_sem_alloc },
	{ "security_sem_free", (void **)&droid_lkm_ks.security_sem_free },
	{ "security_sem_associate", (void **)&droid_lkm_ks.security_sem_associate },
	{ "security_sem_semctl", (void **)&droid_lkm_ks.security_sem_semctl },
	{ "security_sem_semop", (void **)&droid_lkm_ks.security_sem_semop },
	{ "__audit_ipc_obj", (void **)&droid_lkm_ks.__audit_ipc_obj },
	{ "__audit_ipc_set_perm", (void **)&droid_lkm_ks.__audit_ipc_set_perm },
	{ "__audit_inode", (void **)&droid_lkm_ks.__audit_inode },
	{ "__audit_file", (void **)&droid_lkm_ks.__audit_file },
	{ "__audit_mq_open", (void **)&droid_lkm_ks.__audit_mq_open },
	{ "__audit_mq_sendrecv", (void **)&droid_lkm_ks.__audit_mq_sendrecv },
	{ "__audit_mq_notify", (void **)&droid_lkm_ks.__audit_mq_notify },
	{ "__audit_mq_getsetattr", (void **)&droid_lkm_ks.__audit_mq_getsetattr },


	/*
	 * missing on 5.10 only, where the shim drops the mqueue rlimit
	 * accounting instead of the queues, so the warning is the signal
	 */
	{ "get_ucounts", (void **)&droid_lkm_ks.ucounts_get },
	{ "put_ucounts", (void **)&droid_lkm_ks.ucounts_put },
	{ "inc_rlimit_ucounts", (void **)&droid_lkm_ks.ucounts_inc_rlimit },
	{ "dec_rlimit_ucounts", (void **)&droid_lkm_ks.ucounts_dec_rlimit },
	{ "sysctl_overcommit_memory", (void **)&droid_lkm_ks_sysctl_overcommit_memory },

	/*
	 * kernel helpers a stock image may not carry. the shims in this module
	 * define the kernel names, so a name the kernel does not carry resolves to
	 * nothing and the feature that needs it stays off
	 */

	{ "dentry_open", (void **)&droid_lkm_ks.dentry_open },
	{ "do_send_sig_info", (void **)&droid_lkm_ks.do_send_sig_info },
	{ "fc_mount", (void **)&droid_lkm_ks.fc_mount },
	{ "mntget", (void **)&droid_lkm_ks.mntget },
	{ "put_fs_context", (void **)&droid_lkm_ks.put_fs_context },
	{ "free_ipcs", (void **)&droid_lkm_ks.free_ipcs },
	{ "put_ipc_ns", (void **)&droid_lkm_ks.put_ipc_ns },
	{ "pid_nr_ns", (void **)&droid_lkm_ks.pid_nr_ns },
	{ "pid_vnr", (void **)&droid_lkm_ks.pid_vnr },
	{ "proc_dointvec_minmax", (void **)&droid_lkm_ks.proc_dointvec_minmax },
	{ "proc_doulongvec_minmax", (void **)&droid_lkm_ks.proc_doulongvec_minmax },
	{ "register_kprobe", (void **)&droid_lkm_ks.register_kprobe },
	{ "unregister_kprobe", (void **)&droid_lkm_ks.unregister_kprobe },
	{ "kern_path", (void **)&droid_lkm_ks.kern_path },
	{ "path_put", (void **)&droid_lkm_ks.path_put },
	/* the module carries its own body for this one, nothing is lost */
	{ "d_set_d_op", (void **)&droid_lkm_ks.d_set_d_op },
	{ "down_write_killable", (void **)&droid_lkm_ks.down_write_killable },
	{ "radix_tree_tagged", (void **)&droid_lkm_ks.radix_tree_tagged },
	{ "vfs_unlink", (void **)&droid_lkm_ks.vfs_unlink },
	{ "unregister_sysctl_table", (void **)&droid_lkm_ks.unregister_sysctl_table },
	{ "copy_ipcs", (void **)&droid_lkm_ks.copy_ipcs },
	{ "d_splice_alias", (void **)&droid_lkm_ks.d_splice_alias },
	{ "in_group_p", (void **)&droid_lkm_ks.in_group_p },
	{ "ihold", (void **)&droid_lkm_ks.ihold },
	{ "inc_nlink", (void **)&droid_lkm_ks.inc_nlink },
	{ "drop_nlink", (void **)&droid_lkm_ks.drop_nlink },
	{ "clear_inode", (void **)&droid_lkm_ks.clear_inode },
	{ "d_instantiate", (void **)&droid_lkm_ks.d_instantiate },
	{ "inode_init_once", (void **)&droid_lkm_ks.inode_init_once },
	{ "simple_inode_init_ts", (void **)&droid_lkm_ks.simple_inode_init_ts },
	{ "overflowgid", (void **)&droid_lkm_ks.overflowgid_addr },
	{ "register_sysctl", (void **)&droid_lkm_ks.register_sysctl },
	{ "_text", (void **)&droid_lkm_ks.text_start },
	{ "_end", (void **)&droid_lkm_ks.text_end },
	{ "kimage_voffset", (void **)&droid_lkm_ks.kimage_voffset },
	{ "vmalloc_to_pfn", (void **)&droid_lkm_ks.vmalloc_to_pfn },
	{ "__set_fixmap", (void **)&droid_lkm_ks.set_fixmap },
	{ "tasklist_lock", (void **)&droid_lkm_ks_tasklist_lock },
	{ "ipcns_operations", (void **)&droid_lkm_ks_ipcns_operations },
};

int droid_lkm_ksym_init(void)
{
	ds_compat_bind(droid_lkm_syms, ARRAY_SIZE(droid_lkm_syms));

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

	droid_lkm_warn("text patch path is missing, function hooks take the kprobe route:");
	for (i = 0; i < ARRAY_SIZE(need); i++)
		if (!need[i].addr)
			pr_cont(" %s", need[i].name);
	pr_cont("\n");
	return false;
}

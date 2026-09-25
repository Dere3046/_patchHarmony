// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DROID_LKM_IPC_COMPAT_H
#define DROID_LKM_IPC_COMPAT_H

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/ipc_namespace.h>
#include <linux/list.h>
#include <linux/security.h>
#include <linux/audit.h>


struct file;
struct list_head;
unsigned long droid_lkm_do_mmap(struct file *file, unsigned long addr,
			 unsigned long len, unsigned long prot,
			 unsigned long flags, unsigned long vm_flags,
			 unsigned long pgoff, unsigned long *populate,
			 struct list_head *uf);



void *droid_lkm_task_undo_list(struct task_struct *tsk);
void droid_lkm_task_undo_list_set(struct task_struct *tsk, void *ul);
struct list_head *droid_lkm_task_shm_clist(struct task_struct *tsk);


bool droid_lkm_task_ipc_exit_defer(struct task_struct *tsk);
bool droid_lkm_task_ipc_deferred_pending(void);
void droid_lkm_task_ipc_deferred_run(void);


struct sem_undo_list;
struct ipc_namespace;
void droid_lkm_exit_sem(struct task_struct *tsk);
void droid_lkm_exit_sem_ns(struct task_struct *tsk, struct ipc_namespace *ns);
void droid_lkm_exit_shm(struct task_struct *tsk);


/*
 * LSM and audit IPC hooks are real calls through droid_lkm_ks in ksym_shim.c.
 * the kernel audit_inode()/audit_file()/audit_mq_*() helpers are inlines that
 * call __audit_* symbols, and those are not exported, so ksym_shim.c defines
 * the same names and forwards them to the functions resolved at load time.
 */

int droid_lkm_overcommit_memory(void);

#endif /* DROID_LKM_IPC_COMPAT_H */

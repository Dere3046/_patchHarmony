// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DROID_LKM_PROC_INODE_H
#define DROID_LKM_PROC_INODE_H

#include <linux/fs.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sysctl.h>

union proc_op {
	int (*proc_get_link)(struct dentry *, struct path *);
	int (*proc_show)(struct seq_file *m, struct pid_namespace *ns,
			 struct pid *pid, struct task_struct *task);
	int lsmid;
};

struct proc_inode {
	struct pid *pid;
	unsigned int fd;
	union proc_op op;
	struct proc_dir_entry *pde;
	struct ctl_table_header *sysctl;
	struct ctl_table *sysctl_entry;
	struct hlist_node sibling_inodes;
	const struct proc_ns_operations *ns_ops;
	struct inode vfs_inode;
} __randomize_layout;

static inline struct proc_inode *PROC_I(const struct inode *inode)
{
	return container_of(inode, struct proc_inode, vfs_inode);
}

static inline struct pid *proc_pid(const struct inode *inode)
{
	return PROC_I(inode)->pid;
}

static inline struct task_struct *get_proc_task(const struct inode *inode)
{
	return get_pid_task(proc_pid(inode), PIDTYPE_PID);
}

#endif

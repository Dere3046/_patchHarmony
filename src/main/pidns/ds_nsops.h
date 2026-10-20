// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DROID_LKM_NSOPS_H
#define DROID_LKM_NSOPS_H

#include <linux/types.h>
#include <linux/ns_common.h>
#include <linux/proc_ns.h>

const struct proc_ns_operations *droid_lkm_ns_ops_alias(const char *name, int type);

int droid_lkm_ns_ops_host_init(void);
const struct proc_ns_operations *droid_lkm_ns_ops_host_pid(void);
const struct proc_ns_operations *droid_lkm_ns_ops_host_pid_for_children(void);

void droid_lkm_ns_neutralize(struct ns_common *ns, const struct proc_ns_operations *ops);

#define DROID_LKM_NS_LEAK_REFCOUNT 0x40000000

#endif

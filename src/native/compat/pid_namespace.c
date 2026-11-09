// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Dere3046
 */

#include <linux/pid_namespace.h>
#include <linux/rcupdate.h>
#include <linux/user_namespace.h>

#include "native_compat.h"
#include "pid_namespace.h"

static void droid_native_pid_namespace_release(struct rcu_head *head)
{
	struct pid_namespace *ns = container_of(head, struct pid_namespace, rcu);

	if (ns->ucounts)
		droid_native_dec_ucount(ns->ucounts, UCOUNT_PID_NAMESPACES);
	droid_native_put_user_ns(ns->user_ns);
}

void droid_native_pid_namespace_destroy(struct pid_namespace *ns)
{
	droid_native_pidns_tree_remove(ns);
	droid_native_pidns_sysctl_unregister(ns);
	call_rcu(&ns->rcu, droid_native_pid_namespace_release);
}

struct pid_namespace *droid_native_pid_namespace_get(struct pid_namespace *ns)
{
	if (ns && ns != &init_pid_ns)
		droid_native_pidns_ref_get(ns);
	return ns;
}

bool droid_native_pid_namespace_put_ref(struct pid_namespace *ns)
{
	return droid_native_pidns_ref_put(ns);
}

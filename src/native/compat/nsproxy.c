// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Dere3046
 */

// #include <linux/nsproxy.h>
// #include <linux/pid_namespace.h>
//
// #include "native_compat.h"
// #include "nsproxy.h"
// #include "pid_namespace.h"
//
// extern void droid_native_put_pid_ns(struct pid_namespace *ns);
//
// struct pid_namespace *droid_native_nsproxy_pid_namespace_get(
// 	struct nsproxy *nsproxy)
// {
// 	if (!nsproxy)
// 		return NULL;
// 	return droid_native_pid_namespace_get(nsproxy->pid_ns_for_children);
// }
//
// void droid_native_nsproxy_pid_namespace_replace(struct nsproxy *nsproxy,
// 						struct pid_namespace *ns)
// {
// 	if (!nsproxy)
// 		return;
// 	droid_native_put_pid_ns(nsproxy->pid_ns_for_children);
// 	nsproxy->pid_ns_for_children = droid_native_pid_namespace_get(ns);
// }

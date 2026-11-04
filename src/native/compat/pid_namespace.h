// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Dere3046
 */

#ifndef DROID_LKM_COMPAT_PID_NAMESPACE_H
#define DROID_LKM_COMPAT_PID_NAMESPACE_H

#include <linux/types.h>

struct pid_namespace;

struct pid_namespace *droid_native_pid_namespace_get(struct pid_namespace *ns);
bool droid_native_pid_namespace_put_ref(struct pid_namespace *ns);
void droid_native_pid_namespace_destroy(struct pid_namespace *ns);

#endif

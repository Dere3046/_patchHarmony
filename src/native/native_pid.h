// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */
#ifndef DROID_LKM_NATIVE_PID_H
#define DROID_LKM_NATIVE_PID_H

struct pid_namespace;
struct user_namespace;

int droid_native_pidns_init(void);
void droid_native_pidns_exit(void);
struct pid_namespace *droid_native_create_pid_namespace(
	struct user_namespace *user_ns, struct pid_namespace *parent);
struct pid_namespace *droid_native_get_pid_ns(struct pid_namespace *ns);
void droid_native_put_pid_ns(struct pid_namespace *ns);

#endif

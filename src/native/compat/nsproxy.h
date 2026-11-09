// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Dere3046
 */

#ifndef DROID_LKM_COMPAT_NSPROXY_H
#define DROID_LKM_COMPAT_NSPROXY_H

struct nsproxy;

int droid_native_nsproxy_init(void);
void droid_native_nsproxy_exit(void);
void droid_native_nsproxy_defer_free(struct nsproxy *ns);
void droid_native_nsproxy_set_free(void (*free_nsproxy)(struct nsproxy *ns));

#endif

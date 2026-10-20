// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DS_HOOK_H
#define DS_HOOK_H

#include <linux/types.h>

#include "hk_redir.h"

enum ds_hook_route {
	DS_HOOK_NONE = 0,	/* nothing is in place */
	DS_HOOK_SLOT,		/* a pointer in a kernel table was replaced */
	DS_HOOK_TEXT,		/* the entry itself was written */
	DS_HOOK_KPROBE,		/* the entry keeps its bytes, a breakpoint carries the call */
};

enum ds_hook_mode {
	DS_HOOK_AUTO = 0,
	DS_HOOK_TEXT_ONLY,
	DS_HOOK_KPROBE_ONLY,
};

struct ds_hook {
	const char *name;	/* the target as it was named, for the report */
	struct hk_redir *redir;	/* the transport, the library's or this one */
	struct hk_redir own;	/* used when the route is forced */
	void **slot;		/* the field the slot route wrote */
	unsigned long addr;	/* the entry a call lands on */
	unsigned long orig;	/* what the replacement calls to reach the original */
	enum ds_hook_route route;
	bool live;
};

/* the module parameters take effect here, before the first install */
void ds_hook_policy_apply(void);

const char *ds_hook_route_name(enum ds_hook_route route);

/* a slot that refuses the write falls through to the function routes */
int ds_hook_install(struct ds_hook *h, const char *sym, const char *wrap);
int ds_hook_install_slot(struct ds_hook *h, void **slot, const void *repl,
			 const char *sym, const char *wrap);

/* a replacement whose absence refuses the load unless degrade=1 */
int ds_hook_install_critical(struct ds_hook *h, const char *sym,
			     const char *wrap);
int ds_hook_install_slot_critical(struct ds_hook *h, void **slot,
				  const void *repl, const char *sym,
				  const char *wrap);
bool ds_hook_critical_ok(void);
bool ds_hook_degrade_enabled(void);

/* the kprobe route parks the calling task, the replacement clears that here */
void ds_hook_leave(struct ds_hook *h);

int ds_hook_remove(struct ds_hook *h);
void ds_hook_exit(void);
void ds_hook_report(void);

/* walk the installs for a report, a non zero return stops the walk */
int ds_hook_enum(int (*fn)(void *ctx, const struct ds_hook *h), void *ctx);

#endif

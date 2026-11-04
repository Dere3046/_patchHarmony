// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Dere3046
 */

#include <linux/llist.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "ds.h"
#include "nsproxy.h"

struct droid_native_nsproxy_deferred {
	struct nsproxy *ns;
	struct llist_node node;
};

static void (*droid_native_kernel_free_nsproxy)(struct nsproxy *ns);
static LLIST_HEAD(droid_native_nsproxy_deferred);
static struct work_struct droid_native_nsproxy_work;
static bool droid_native_nsproxy_ready;

static void droid_native_nsproxy_work_fn(struct work_struct *work)
{
	struct droid_native_nsproxy_deferred *entry, *tmp;
	struct llist_node *node = llist_del_all(&droid_native_nsproxy_deferred);

	llist_for_each_entry_safe(entry, tmp, node, node) {
		droid_native_kernel_free_nsproxy(entry->ns);
		kfree(entry);
	}
}

void droid_native_nsproxy_set_free(void (*free_nsproxy)(struct nsproxy *ns))
{
	droid_native_kernel_free_nsproxy = free_nsproxy;
}

int droid_native_nsproxy_init(void)
{
	INIT_WORK(&droid_native_nsproxy_work, droid_native_nsproxy_work_fn);
	droid_native_nsproxy_ready = true;
	return 0;
}

void droid_native_nsproxy_exit(void)
{
	if (!droid_native_nsproxy_ready)
		return;
	flush_work(&droid_native_nsproxy_work);
}

void droid_native_nsproxy_defer_free(struct nsproxy *ns)
{
	struct droid_native_nsproxy_deferred *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return;
	entry->ns = ns;
	llist_add(&entry->node, &droid_native_nsproxy_deferred);
	schedule_work(&droid_native_nsproxy_work);
}

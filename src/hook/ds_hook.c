// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/string.h>

#include "ds_compat.h"
#include "ds_hook.h"
#include "hk_patch.h"
#include "hk_redir.h"
#include "vendorcompat.h"

#define DS_HOOK_TAG "droid_lkm/hook"

#define ds_hook_info(fmt, ...) pr_info("[" DS_HOOK_TAG "] " fmt, ##__VA_ARGS__)
#define ds_hook_warn(fmt, ...) pr_warn("[" DS_HOOK_TAG "] " fmt, ##__VA_ARGS__)

/*
 * 0 asks the kernel on every install, 1 takes only the entry write, 2 takes only
 * the breakpoint. the forced routes exist so a device can be tested against the
 * mechanism it is not using
 */
static uint ds_hook_mode;
module_param_named(route, ds_hook_mode, uint, 0444);
MODULE_PARM_DESC(route, "0 auto (default), 1 entry write only, 2 breakpoint only");

static bool ds_hook_degrade;
module_param_named(degrade, ds_hook_degrade, bool, 0444);
MODULE_PARM_DESC(degrade,
	"1 keeps running when a container critical hook cannot install, 0 refuses the load");

/*
 * every text write goes through the kernel's own patch primitive: the module
 * fixmap writer faults on the MTK device, so it is not offered at all
 */
void ds_hook_policy_apply(void)
{
	ds_hook_info("policy: route=%u degrade=%d\n", ds_hook_mode,
		     ds_hook_degrade);
}

const char *ds_hook_route_name(enum ds_hook_route route)
{
	static const char *const names[] = {
		[DS_HOOK_NONE] = "none",
		[DS_HOOK_SLOT] = "slot",
		[DS_HOOK_TEXT] = "text",
		[DS_HOOK_KPROBE] = "kprobe",
	};

	if (route > DS_HOOK_KPROBE)
		return "?";
	return names[route];
}

// every install lands here so the load report can name the ones that failed
#define DS_HOOK_RECORDS 8

static struct ds_hook *ds_hook_list[DS_HOOK_RECORDS];
static unsigned int ds_hook_count;

static bool ds_hook_critical_missing;

static void ds_hook_track(struct ds_hook *h)
{
	unsigned int i;

	for (i = 0; i < ds_hook_count; i++) {
		if (ds_hook_list[i] == h)
			return;
	}
	if (ds_hook_count == DS_HOOK_RECORDS)
		return;
	ds_hook_list[ds_hook_count++] = h;
}

static int ds_hook_route_install(struct ds_hook *h, const char *sym,
				 const char *wrap, enum ds_hook_route route)
{
	enum hk_redir_how how = route == DS_HOOK_TEXT ? HK_REDIR_TEXT :
							 HK_REDIR_KPROBE;
	int ret;

	ret = hk_redir_install(&h->own, sym, wrap, how);
	if (ret)
		return ret;
	h->redir = &h->own;
	h->route = route;
	h->addr = h->own.addr;
	h->orig = h->own.orig;
	h->live = true;
	return 0;
}

static int ds_hook_auto(struct ds_hook *h, const char *sym, const char *wrap)
{
	struct vc_result res;
	int ret;

	ret = vc_install(sym, wrap, &res);
	if (ret) {
		ds_hook_warn("hook %s: no route took it (%d)\n", sym, ret);
		return ret;
	}

	h->redir = vc_handle(sym);
	if (!h->redir) {
		ds_hook_warn("hook %s: the route took it and handed back no handle\n",
			     sym);
		return -ENODATA;
	}

	h->route = res.route == VC_ROUTE_SLOT ? DS_HOOK_SLOT :
		   res.route == VC_ROUTE_TEXT ? DS_HOOK_TEXT : DS_HOOK_KPROBE;
	h->addr = h->redir->addr;
	h->orig = h->redir->orig;
	h->live = true;
	return 0;
}

int ds_hook_install(struct ds_hook *h, const char *sym, const char *wrap)
{
	int ret;

	if (!h || !sym || !wrap)
		return -EINVAL;
	memset(h, 0, sizeof(*h));
	h->name = sym;

	switch (ds_hook_mode) {
	case DS_HOOK_KPROBE_ONLY:
		ret = ds_hook_route_install(h, sym, wrap, DS_HOOK_KPROBE);
		break;
	case DS_HOOK_TEXT_ONLY:
		ret = ds_hook_route_install(h, sym, wrap, DS_HOOK_TEXT);
		break;
	default:
		ret = ds_hook_auto(h, sym, wrap);
		break;
	}

	if (ret)
		return ret;

	ds_hook_track(h);
	ds_hook_info("hook %s on the %s route (entry 0x%016lx orig 0x%016lx)\n",
		     sym, ds_hook_route_name(h->route), h->addr, h->orig);
	return 0;
}

int ds_hook_install_slot(struct ds_hook *h, void **slot, const void *repl,
			 const char *sym, const char *wrap)
{
	unsigned long val = (unsigned long)repl;
	unsigned long old, field;
	int ret;

	if (!h || !slot || !repl || !sym || !wrap)
		return -EINVAL;
	/* the old value has to be read before the write replaces it */
	old = *(unsigned long *)slot;
	memset(h, 0, sizeof(*h));
	h->name = sym;

	field = (unsigned long)slot;
	ret = ds_compat_write(slot, val);
	if (!ret) {
		h->slot = slot;
		h->addr = (unsigned long)slot;
		h->orig = old;
		h->route = DS_HOOK_SLOT;
		h->live = true;
		ds_hook_track(h);
		ds_hook_info("hook %s on the slot route (field 0x%016lx)\n", sym,
			     field);
		return 0;
	}

	/*
	 * the writer said no: name the field, the page and the alias it tried, then
	 * let the function routes carry the replacement
	 */
	ds_hook_warn("hook %s: the field refused the write (%d field=0x%016lx level=%d writable=%d pa=0x%lx), trying the entry routes\n",
		     sym, ret, field, hk_va_level(field), hk_va_writable(field),
		     hk_va_to_pa(field));

	return ds_hook_install(h, sym, wrap);
}

int ds_hook_install_critical(struct ds_hook *h, const char *sym,
			     const char *wrap)
{
	int ret = ds_hook_install(h, sym, wrap);

	if (ret)
		ds_hook_critical_missing = true;
	return ret;
}

int ds_hook_install_slot_critical(struct ds_hook *h, void **slot,
				  const void *repl, const char *sym,
				  const char *wrap)
{
	int ret = ds_hook_install_slot(h, slot, repl, sym, wrap);

	if (ret)
		ds_hook_critical_missing = true;
	return ret;
}

bool ds_hook_critical_ok(void)
{
	return !ds_hook_critical_missing;
}

bool ds_hook_degrade_enabled(void)
{
	return ds_hook_degrade;
}

void ds_hook_leave(struct ds_hook *h)
{
	if (h && h->route == DS_HOOK_KPROBE)
		hk_redir_leave(h->redir);
}

int ds_hook_remove(struct ds_hook *h)
{
	int ret = 0;

	if (!h || !h->live)
		return -EINVAL;

	if (h->route == DS_HOOK_SLOT) {
		ret = ds_compat_write(h->slot, h->orig);
	} else {
		ret = hk_redir_remove(h->redir);
	}
	if (ret)
		ds_hook_warn("hook %s did not come back (%d)\n", h->name, ret);
	h->live = false;
	return ret;
}

void ds_hook_exit(void)
{
	/* the library owns every auto route it installed, this takes them back */
	vc_exit();
}

int ds_hook_enum(int (*fn)(void *ctx, const struct ds_hook *h), void *ctx)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ds_hook_count; i++) {
		ret = fn(ctx, ds_hook_list[i]);
		if (ret)
			return ret;
	}
	return 0;
}

void ds_hook_report(void)
{
	unsigned int i, live = 0;

	for (i = 0; i < ds_hook_count; i++) {
		if (ds_hook_list[i]->live)
			live++;
		ds_hook_info("hook: %s %s on the %s route\n", ds_hook_list[i]->name,
			     ds_hook_list[i]->live ? "installed" : "NOT installed",
			     ds_hook_route_name(ds_hook_list[i]->route));
	}
	ds_hook_info("hooks: %u/%u installed\n", live, ds_hook_count);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>

#include "core.h"
#include "btf.h"
#include "ds_compat.h"
#include "hk_patch.h"
#include "type_info.h"

static const char *ds_compat_module = "droid_lkm";
static const char *ds_compat_own_prefix = "";

#define ds_compat_info(fmt, ...)                                               \
	pr_info("[%s] " fmt, ds_compat_module, ##__VA_ARGS__)
#define ds_compat_warn(fmt, ...)                                               \
	pr_warn("[%s] " fmt, ds_compat_module, ##__VA_ARGS__)

int ds_compat_init(const char *module, const char *own_prefix)
{
	if (module)
		ds_compat_module = module;
	if (own_prefix)
		ds_compat_own_prefix = own_prefix;

	find_kallsyms_base();
	if (!klnum_val || !kallrecon_klp) {
		ds_compat_warn("kallsyms recovery failed, nothing can be resolved\n");
		return -ENODATA;
	}

	ds_compat_info("kallsyms recovered, klnum=%u\n", klnum_val);
	return 0;
}

unsigned long ds_compat_sym(const char *name)
{
	unsigned long addr;

	if (!name || !kallrecon_klp)
		return 0;

	addr = kallrecon_klp(name);
	/*
	 * kallrecon_klp is the kernel's own kallsyms_lookup_name. a kernel that
	 * hides an address from it, _text being the image base, still has the
	 * symbol in its table, so the library's own walk answers that case
	 */
	if (!addr)
		addr = kallsyms_name_to_addr(name);
	/*
	 * the shims in a module are defined under the kernel's own names, and the
	 * lookup searches the module tables too, so a kernel that does not carry the
	 * name hands back the shim instead. that is not the kernel symbol: a shim
	 * whose body calls the resolved pointer calls itself. a wrapper carries the
	 * module prefix and is the one kind of symbol that is expected here
	 */
	if (addr && within_module(addr, THIS_MODULE) && ds_compat_own_prefix[0] &&
	    strncmp(name, ds_compat_own_prefix, strlen(ds_compat_own_prefix)))
		return 0;
	return addr;
}

int ds_compat_bind(const struct ds_compat_sym *tab, unsigned int count)
{
	unsigned int i, missing = 0;

	for (i = 0; i < count; i++) {
		unsigned long addr = ds_compat_sym(tab[i].name);

		*tab[i].slot = (void *)addr;
		if (!addr)
			missing++;
	}
	if (!missing)
		return 0;

	/*
	 * a name the kernel does not carry is the only way an entry stays empty,
	 * the features that need one stay off and the names are what says which
	 */
	ds_compat_info("%u/%u kernel symbols absent:", missing, count);
	for (i = 0; i < count; i++) {
		if (!*tab[i].slot)
			pr_cont(" %s", tab[i].name);
	}
	pr_cont("\n");
	return missing;
}

int ds_compat_write(void *dst, unsigned long val)
{
	/*
	 * one entry for data writes: the kernel fixmap slot first then the library
	 * slot and the caller hears about it when neither lands
	 */
	struct hk_write w = { .nofault = true, .verify = true };
	int ret;

	w.slot = HK_SLOT_KERNEL;
	ret = hk_write_run(dst, &val, sizeof(val), &w);
	if (!ret)
		return 0;

	w.slot = HK_SLOT_LIBRARY;
	return hk_write_run(dst, &val, sizeof(val), &w);
}

/* the layout engine reads BTF through the type info reader */
bool ds_compat_layout_init(struct ds_compat_layout *l)
{
	struct ti_resolver res = { .name_to_addr = ds_compat_sym };
	unsigned int ok = 0, differs = 0, missing = 0;
	unsigned int i;

	if (!l || !l->fields || !l->types)
		return false;

	if (ti_init(&res)) {
		ds_compat_warn("layout: type info refused to start, every offset stays compile time\n");
		return false;
	}
	if (!ti_btf_available()) {
		// BTF only
		// anchor.o is not linked on purpose
		ds_compat_warn("layout: the running kernel has no BTF, offsets stay compile time and features that need one must be refused by hand\n");
		return false;
	}

	for (i = 0; i < l->nfields; i++) {
		struct ds_compat_field *f = &l->fields[i];
		u32 type_id, bit_off, bit_sz;

		if (ti_type_by_name(ti_base(), f->type, BIT(BTF_KIND_STRUCT),
				    &type_id)) {
			f->state = DS_COMPAT_NO_TYPE;
			missing++;
			continue;
		}
		if (ti_member_off(ti_base(), type_id, f->member, &bit_off,
				  &bit_sz)) {
			f->state = DS_COMPAT_NO_MEMBER;
			missing++;
			continue;
		}

		f->live = bit_off / 8;
		/* build is zero for a field the module has no build reference for */
		if (f->build && f->live != f->build) {
			f->state = DS_COMPAT_DIFFERS;
			differs++;
			continue;
		}
		f->state = DS_COMPAT_OK;
		ok++;
	}

	for (i = 0; i < l->ntypes; i++) {
		struct ds_compat_type *t = &l->types[i];
		u32 type_id;

		if (ti_type_by_name(ti_base(), t->name, BIT(BTF_KIND_STRUCT),
				    &type_id)) {
			t->state = DS_COMPAT_NO_TYPE;
			missing++;
			continue;
		}
		t->live = ti_type_size(ti_base(), type_id);
		if (!t->live) {
			t->state = DS_COMPAT_NO_TYPE;
			missing++;
			continue;
		}
		/* build is zero when the module only needs the running size */
		if (t->build && t->live != t->build) {
			t->state = DS_COMPAT_DIFFERS;
			differs++;
			continue;
		}
		t->state = DS_COMPAT_OK;
	}

	ds_compat_info("layout: BTF %u/%u fields as built, %u differ, %u not in BTF\n",
		       ok, l->nfields, differs, missing);

	for (i = 0; i < l->nfields; i++) {
		struct ds_compat_field *f = &l->fields[i];

		if (f->state == DS_COMPAT_DIFFERS)
			ds_compat_warn("%s.%s is at %u in the running kernel, this build uses %u\n",
				       f->type, f->member, f->live, f->build);
	}

	return !differs && !missing;
}

void ds_compat_layout_report(struct ds_compat_layout *l)
{
	unsigned int ok = 0, i;

	if (!l)
		return;

	for (i = 0; i < l->nfields; i++) {
		if (l->fields[i].state == DS_COMPAT_OK)
			ok++;
	}

	for (i = 0; i < l->ntypes; i++) {
		if (l->types[i].state == DS_COMPAT_DIFFERS)
			ds_compat_warn("sizeof(struct %s) is %u in the running kernel, this build uses %u\n",
				       l->types[i].name, l->types[i].live,
				       l->types[i].build);
	}

	ds_compat_info("layout: %u/%u offsets from BTF\n", ok, l->nfields);
}

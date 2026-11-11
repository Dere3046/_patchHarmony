// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DS_COMPAT_H
#define DS_COMPAT_H

#include <linux/types.h>

enum ds_compat_state {
	DS_COMPAT_OK = 0,
	DS_COMPAT_NO_TYPE,	/* the running kernel has no such struct in BTF */
	DS_COMPAT_NO_MEMBER,	/* the struct is there, the member is not */
	DS_COMPAT_DIFFERS,	/* the member is a different width in the kernel */
	DS_COMPAT_UNVERIFIED,	/* no backend answered for it */
	DS_COMPAT_ANCHORED,	/* the anchor backend found it */
	DS_COMPAT_VERIFIED,	/* an anchor or a cross check confirmed it */
	DS_COMPAT_MANUAL,	/* a build time table supplied the offset */
	DS_COMPAT_BUILD,	/* module owned struct, or the build fallback */
};

/* the port that asks for a field, ANY means every port */
enum ds_compat_need {
	DS_COMPAT_NEED_ANY = 0,
	DS_COMPAT_NEED_SYSV,
	DS_COMPAT_NEED_MQUEUE,
};

struct ds_compat_field {
	const char *type;
	const char *member;
	u32 build;		/* offsetof from the headers this module was built with */
	u32 live;		/* offset in the running kernel, 0 when refused */
	u32 build_size;		/* sizeof of the member, 0 for an array */
	u32 live_size;		/* the same member in the running kernel */
	u8 state;
	u8 need;
};

struct ds_compat_type {
	const char *name;
	u32 build;
	u32 live;
	u8 state;
};

struct ds_compat_layout {
	const char *module;
	struct ds_compat_field *fields;
	unsigned int nfields;
	struct ds_compat_type *types;
	unsigned int ntypes;
};

/*
 * own_prefix is what the module's own replacement symbols start with, so a
 * kernel name that resolves back into this module is recognised as the shim it
 * is instead of being taken for the kernel's
 */
int ds_compat_init(const char *module, const char *own_prefix);
unsigned long ds_compat_sym(const char *name);

/* one entry of a module's kernel symbol table */
struct ds_compat_sym {
	const char *name;
	void **slot;
};

/* resolve one module's symbol table, a name the kernel does not carry stays 0 */
int ds_compat_bind(const struct ds_compat_sym *tab, unsigned int count);

/* one write for a pointer field, the cascade every data patch uses */
int ds_compat_write(void *dst, unsigned long val);

bool ds_compat_layout_init(struct ds_compat_layout *l);
void ds_compat_layout_fallback(struct ds_compat_layout *l);
void ds_compat_layout_force_build(struct ds_compat_layout *l);
const char *ds_compat_state_name(u8 state);

/* no BTF: verify the kernel owned structs against symbols, refuse the rest */
bool ds_compat_layout_anchor(struct ds_compat_layout *l);
void ds_compat_layout_report(struct ds_compat_layout *l);

#endif

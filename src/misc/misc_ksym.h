// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_MISC_KSYM_H
#define DROID_LKM_MISC_KSYM_H

#include <linux/types.h>
#include <linux/cred.h>
#include <linux/key.h>
#include <linux/proc_ns.h>
#include <linux/nsproxy.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6.h>
#include <net/route.h>
#include <net/ipv6.h>

struct net;
struct net_device;
struct dst_entry;
struct flowi;
struct in6_addr;
struct cred;
struct key;
struct mnt_idmap;
struct user_namespace;
struct nsproxy;
struct fs_struct;
struct task_struct;
struct droid_lkm_ns_kind_reg;

/*
 * CONFIG_TRIM_UNUSED_KSYMS and the vendor abi_symbollist mean an exported symbol is not
 * necessarily in the ksymtab. everything is resolved by name, a missing thunk disables the
 * feature that needs it and never becomes an unresolved import.
 *
 * a thunk's type is the kernel's own, taken with typeof(&symbol) from the declaration this
 * build is compiling against. clang's cfi hashes the whole function type at an indirect call,
 * so a prototype written out by hand here is a fatal cfi failure on the device the moment it
 * drifts by one qualifier, and a warning never gets there first.
 *
 * the two that are still written out are the ones with no declaration to copy: the kernel
 * defines create_new_namespaces in kernel/nsproxy.c and never declares it in a header, and
 * the ns_kind pair is this module's own export.
 */
struct droid_lkm_misc_ksym {
	/* xt */
	typeof(&xt_register_matches)	xt_register_matches;
	typeof(&xt_unregister_matches)	xt_unregister_matches;
	typeof(&inet_dev_addr_type)	inet_dev_addr_type;
	typeof(&ipv6_chk_addr)		ipv6_chk_addr;
	typeof(&__ipv6_addr_type)	__ipv6_addr_type;
	typeof(&__nf_ip6_route)		__nf_ip6_route;

	/* fake namespaces */
	typeof(&proc_alloc_inum)	proc_alloc_inum;
	typeof(&proc_free_inum)		proc_free_inum;
	typeof(&prepare_creds)		prepare_creds;
	typeof(&commit_creds)		commit_creds;
	typeof(&key_put)		key_put;

	/* optional main module entry points, exported by droid_lkm.ko itself */
	int (*ns_kind_register)(const struct droid_lkm_ns_kind_reg *kind);
	void (*ns_kind_unregister)(const struct droid_lkm_ns_kind_reg *kind);

	/* the single capability decision function, resolved for the trace only */
	int (*cap_capable)(const struct cred *cred, struct user_namespace *targ_ns,
			   int cap, unsigned int opts);

	/* address of the kernel's "no idmap" sentinel, compared by pointer */
	const struct mnt_idmap *nop_mnt_idmap;

	/* namespace construction, called with the container's own user namespace */
	struct nsproxy *(*create_new_namespaces)(unsigned long flags,
						 struct task_struct *tsk,
						 struct user_namespace *user_ns,
						 struct fs_struct *new_fs);

	/* the initial task, read for the initial mount namespace pointer */
	struct task_struct *init_task;
};

extern struct droid_lkm_misc_ksym droid_lkm_misc_ks;

/* mirrors the kernel inline, which masks the raw type down to 16 bits */
static inline int droid_lkm_misc_ipv6_addr_type(const struct in6_addr *addr)
{
	return droid_lkm_misc_ks.__ipv6_addr_type(addr) & 0xffff;
}

int droid_lkm_misc_ksym_init(void);
void droid_lkm_misc_ksym_probe(void);

#endif

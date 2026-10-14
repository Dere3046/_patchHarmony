// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * The chokepoints that cannot be answered by correcting state.
 *
 * ksys_unshare and copy_creds are where a request for CLONE_NEWUSER is turned
 * into a fake namespace. The flag is stripped before the kernel sees it, the
 * kernel keeps handling the other namespaces, and the credential side is
 * installed afterwards: on current for unshare, on the child for clone, which
 * cannot have run yet at that point.
 *
 * unshare_nsproxy_namespaces and capable_wrt_inode_uidgid are the two decisions
 * that read current_user_ns(), which this kernel compiles to a constant, so
 * there is no state to correct: the question itself names the host. Both keep
 * the original as the fast path and only add the container case. The owners the
 * kernel fills in through the get_user_ns() stub are corrected in
 * userns_owner.c, which is why may_mount() and mount_capable() have no hook:
 * may_mount() is inlined into path_mount() and could not be hooked anyway.
 */

#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/nsproxy.h>
#include <linux/utsname.h>
#include <linux/cgroup.h>
#include <linux/time_namespace.h>
#include <net/net_namespace.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mnt_idmapping.h>
#include <linux/err.h>
#include <linux/uidgid.h>

#include "misc.h"
#include "misc_ksym.h"
#include "ds_compat.h"
#include "userns.h"
#include "hk_inline.h"

static struct hk_inline droid_lkm_userns_unshare_hook;
static struct hk_inline droid_lkm_userns_nsproxy_hook;
static struct hk_inline droid_lkm_userns_creds_hook;
static struct hk_inline droid_lkm_userns_inodecap_hook;

static int (*droid_lkm_userns_unshare_orig)(unsigned long flags);
static int (*droid_lkm_userns_nsproxy_orig)(unsigned long unshare_flags,
					    struct nsproxy **new_nsp,
					    struct cred *new_cred,
					    struct fs_struct *new_fs);
static int (*droid_lkm_userns_creds_orig)(struct task_struct *p,
					  unsigned long clone_flags);
/*
 * capable_wrt_inode_uidgid's mapping argument moved with inode_permission's, an id mapping
 * from 6.3 and a user namespace before that, so it folds through the same pair. spelling it
 * struct mnt_idmap unconditionally is a cfi hash mismatch on 5.15 and 6.1.
 */
static bool (*droid_lkm_userns_inodecap_orig)(DROID_LKM_IDMAP_PARAM
					      const struct inode *inode, int cap);

/*
 * unshare_nsproxy_namespaces() picks current_user_ns(), the initial namespace here, and the
 * capability test then fails for a task in a fake one. build them with the task's own
 * namespace as owner, through create_new_namespaces() so every constructor runs
 */
__nocfi noinline int droid_lkm_userns_nsproxy_wrap(unsigned long unshare_flags,
						   struct nsproxy **new_nsp,
						   struct cred *new_cred,
						   struct fs_struct *new_fs)
{
	struct droid_lkm_userns *u = droid_lkm_userns_of(current);
	struct nsproxy *nsp;
	int ret;

	if (!u || new_cred || !cap_raised(current_cred()->cap_effective, CAP_SYS_ADMIN))
		return droid_lkm_userns_nsproxy_orig(unshare_flags, new_nsp, new_cred, new_fs);

	nsp = droid_lkm_misc_ks.create_new_namespaces(unshare_flags, current, &u->uns,
						      new_fs ? new_fs : current->fs);
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	/*
	 * get_user_ns() is a stub returning the initial namespace, so the constructors stored the
	 * wrong owner. only namespaces this call created are touched, the mount namespace is covered
	 * by may_mount()
	 */
	if (unshare_flags & CLONE_NEWUTS)
		nsp->uts_ns->user_ns = &u->uns;
	if (unshare_flags & CLONE_NEWNET)
		nsp->net_ns->user_ns = &u->uns;
	if (unshare_flags & CLONE_NEWCGROUP)
		nsp->cgroup_ns->user_ns = &u->uns;
	if ((unshare_flags & CLONE_NEWTIME) && nsp->time_ns_for_children)
		nsp->time_ns_for_children->user_ns = &u->uns;

	*new_nsp = nsp;
	ret = 0;

	return ret;
}

__nocfi noinline int droid_lkm_userns_unshare_wrap(unsigned long flags)
{
	struct droid_lkm_userns *u;
	unsigned long rest;
	int ret;

	if (!(flags & CLONE_NEWUSER))
		return droid_lkm_userns_unshare_orig(flags);

	/* upstream rejects this pair before it touches anything */
	if (flags & CLONE_FS)
		return -EINVAL;

	/* allocate before the kernel changes anything, so failure is free */
	u = droid_lkm_userns_create(droid_lkm_userns_of(current));
	if (!u)
		return -ENOMEM;

	rest = flags & ~CLONE_NEWUSER;
	ret = droid_lkm_userns_unshare_orig(rest);
	if (ret) {
		droid_lkm_userns_destroy(u);
		return ret;
	}

	ret = droid_lkm_userns_install_current(u);
	if (ret)
		droid_lkm_misc_warn("unshare: credential install failed: %d\n", ret);

	return ret;
}

__nocfi noinline int droid_lkm_userns_creds_wrap(struct task_struct *p,
						unsigned long clone_flags)
{
	struct droid_lkm_userns *u;
	unsigned long rest;
	int ret;

	/* a thread clone shares credentials, so it cannot own a namespace */
	if (!(clone_flags & CLONE_NEWUSER) || (clone_flags & CLONE_THREAD))
		return droid_lkm_userns_creds_orig(p, clone_flags);

	u = droid_lkm_userns_create(droid_lkm_userns_of(current));
	if (!u)
		return -ENOMEM;

	rest = clone_flags & ~CLONE_NEWUSER;
	ret = droid_lkm_userns_creds_orig(p, rest);
	if (ret) {
		droid_lkm_userns_destroy(u);
		return ret;
	}

	/* the child has not run and copy_creds() built a fresh credential, so this is race free */
	droid_lkm_userns_install_cred((struct cred *)p->cred, u);

	return 0;
}

__nocfi noinline bool droid_lkm_userns_inodecap_wrap(DROID_LKM_IDMAP_PARAM
						     const struct inode *inode,
						     int cap)
{
	struct droid_lkm_userns *u;

	if (droid_lkm_userns_inodecap_orig(DROID_LKM_IDMAP_PASS inode, cap))
		return true;

	/*
	 * an idmapped mount is a different rule set, it arrives with the lens. the
	 * comparison is through void * because below 6.3 the argument above is a user
	 * namespace while the sentinel is an id mapping, and a kernel that old carries
	 * no sentinel at all: the field is NULL there and the first test short circuits
	 */
	if (droid_lkm_misc_ks.nop_mnt_idmap &&
	    (const void *)idmap != (const void *)droid_lkm_misc_ks.nop_mnt_idmap)
		return false;

	u = droid_lkm_userns_of(current);
	if (!u)
		return false;

	if (!cap_raised(current_cred()->cap_effective, cap))
		return false;

	/* privileged_wrt_inode_uidgid(): both ids must be mapped in the namespace */
	return droid_lkm_userns_map_ok(u, __kuid_val(inode->i_uid)) &&
	       droid_lkm_userns_map_ok(u, __kgid_val(inode->i_gid));
}

static int droid_lkm_userns_hook_one(struct hk_inline *h, const char *sym,
				     const char *wrap, void **orig)
{
	int ret;

	ret = hk_inline_hook(h, sym, wrap);
	if (ret) {
		droid_lkm_misc_warn("hook %s failed: %d\n", sym, ret);
		return ret;
	}

	*orig = (void *)h->orig;
	droid_lkm_misc_dbg("hook %-28s addr=0x%lx orig=0x%lx\n", sym, h->addr, h->orig);

	return 0;
}

int droid_lkm_userns_hooks_init(void)
{
	int ret;

	ret = droid_lkm_userns_hook_one(&droid_lkm_userns_unshare_hook, "ksys_unshare",
		"droid_lkm_userns_unshare_wrap", (void **)&droid_lkm_userns_unshare_orig);
	if (ret)
		return ret;

	ret = droid_lkm_userns_hook_one(&droid_lkm_userns_nsproxy_hook,
		"unshare_nsproxy_namespaces", "droid_lkm_userns_nsproxy_wrap",
		(void **)&droid_lkm_userns_nsproxy_orig);
	if (ret)
		goto out_unshare;

	ret = droid_lkm_userns_hook_one(&droid_lkm_userns_creds_hook, "copy_creds",
		"droid_lkm_userns_creds_wrap", (void **)&droid_lkm_userns_creds_orig);
	if (ret)
		goto out_nsproxy;

	ret = droid_lkm_userns_hook_one(&droid_lkm_userns_inodecap_hook,
		"capable_wrt_inode_uidgid", "droid_lkm_userns_inodecap_wrap",
		(void **)&droid_lkm_userns_inodecap_orig);
	if (ret)
		goto out_creds;

	droid_lkm_misc_info("userns hooks installed\n");
	return 0;

out_creds:
	hk_inline_unhook(&droid_lkm_userns_creds_hook);
out_nsproxy:
	hk_inline_unhook(&droid_lkm_userns_nsproxy_hook);
out_unshare:
	hk_inline_unhook(&droid_lkm_userns_unshare_hook);
	return ret;
}

void droid_lkm_userns_hooks_exit(void)
{
	hk_inline_unhook(&droid_lkm_userns_inodecap_hook);
	hk_inline_unhook(&droid_lkm_userns_creds_hook);
	hk_inline_unhook(&droid_lkm_userns_nsproxy_hook);
	hk_inline_unhook(&droid_lkm_userns_unshare_hook);
}

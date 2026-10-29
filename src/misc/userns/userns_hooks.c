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
 * userns_owner.c, which is why may_mount() has no hook: it is inlined into
 * path_mount() and could not be hooked anyway. mount_capable() is a real function
 * and is hooked here: for a filesystem that is not FS_USERNS_MOUNT it asks for
 * CAP_SYS_ADMIN in the initial namespace, which a privileged container should be
 * allowed to answer for itself.
 */

#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/nsproxy.h>
#include <linux/utsname.h>
#include <linux/cgroup.h>
#include <linux/ipc_namespace.h>
#include <linux/time_namespace.h>
#include <net/net_namespace.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#include <linux/mnt_idmapping.h>
#endif
#include <linux/err.h>
#include <linux/uidgid.h>
#include <asm/ptrace.h>

#include "misc.h"
#include "misc_layout.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "userns.h"
#include "hk_inline.h"
#include "ds_shapes.h"

/* fs/internal.h is not a module header; this is the kernel's own declaration */
bool mount_capable(struct fs_context *fc);

static struct hk_inline droid_lkm_userns_unshare_hook;
static struct hk_inline droid_lkm_userns_nsproxy_hook;
static struct hk_inline droid_lkm_userns_creds_hook;
static struct hk_inline droid_lkm_userns_inodecap_hook;
static struct hk_inline droid_lkm_userns_mountcap_hook;
static struct hk_inline droid_lkm_userns_prepcred_hook;
static struct hk_inline droid_lkm_userns_putcred_hook;
static struct hk_inline droid_lkm_userns_seccap_hook;
static struct hk_inline droid_lkm_userns_fchownat_hook;

static int (*droid_lkm_userns_unshare_orig)(unsigned long flags);
static int (*droid_lkm_userns_nsproxy_orig)(unsigned long unshare_flags,
					    struct nsproxy **new_nsp,
					    struct cred *new_cred,
					    struct fs_struct *new_fs);
static int (*droid_lkm_userns_creds_orig)(struct task_struct *p,
					  unsigned long clone_flags);

static bool (*droid_lkm_userns_inodecap_orig)(DROID_LKM_IDMAP_PARAM
					      const struct inode *inode, int cap);
static bool (*droid_lkm_userns_mountcap_orig)(struct fs_context *fc);

static struct cred *(*droid_lkm_userns_prepcred_orig)(void);
static void (*droid_lkm_userns_putcred_orig)(struct cred *cred);
static int (*droid_lkm_userns_seccap_orig)(const struct cred *cred,
					   struct user_namespace *ns,
					   int cap, unsigned int opts);
static long (*droid_lkm_userns_fchownat_orig)(const struct pt_regs *regs);

/*
 * the original behind a hook, falling back to the hook's own record. a wrapper can run in
 * the window between hk_inline_hook() installing the detour and the pointer being published
 * here, and a NULL at that point would be an indirect call into nothing. the fallback is
 * what makes the window harmless, and it was written out at every wrapper before
 */
#define DROID_LKM_USERNS_ORIG(_name)                                           \
	({                                                                     \
		typeof(droid_lkm_userns_##_name##_orig) __orig =               \
			READ_ONCE(droid_lkm_userns_##_name##_orig);            \
		if (unlikely(!__orig))                                         \
			__orig = (typeof(__orig))                              \
				droid_lkm_userns_##_name##_hook.orig;          \
		__orig;                                                        \
	})

__nocfi noinline struct cred *droid_lkm_userns_prepcred_wrap(void)
{
	struct cred *new;
	struct droid_lkm_userns *u;
	typeof(droid_lkm_userns_prepcred_orig) orig = DROID_LKM_USERNS_ORIG(prepcred);

	if (!orig)
		return NULL;

	new = orig();
	if (!new)
		return NULL;

	/*
	 * In kernels with CONFIG_USER_NS disabled, prepare_creds() shallow-copies
	 * old->user_ns via memcpy, but get_user_ns() is a stub returning &init_user_ns
	 * without refcounting. Acquire a reference for our fake user namespace.
	 */
	u = droid_lkm_userns_from_ns(new->user_ns);
	if (u)
		droid_lkm_userns_get(u);

	return new;
}

__nocfi noinline void droid_lkm_userns_putcred_wrap(struct cred *cred)
{
	struct droid_lkm_userns *u;
	typeof(droid_lkm_userns_putcred_orig) orig = DROID_LKM_USERNS_ORIG(putcred);

	if (cred) {
		u = droid_lkm_userns_from_ns(cred->user_ns);
		if (u)
			droid_lkm_userns_put(u);
	}

	if (orig)
		orig(cred);
}

__nocfi noinline int droid_lkm_userns_seccap_wrap(const struct cred *cred,
						  struct user_namespace *ns,
						  int cap, unsigned int opts)
{
	struct droid_lkm_userns *targ_u, *cred_u;
	typeof(droid_lkm_userns_seccap_orig) orig = DROID_LKM_USERNS_ORIG(seccap);

	if (!orig)
		return -EINVAL;

	/* Fast path: host check on host credentials */
	if (likely(ns == droid_lkm_userns_init_uns &&
		   (!cred || cred->user_ns == droid_lkm_userns_init_uns)))
		return orig(cred, ns, cap, opts);

	/*
	 * Security boundary 1: a fake user namespace NEVER holds capabilities
	 * over the initial user namespace (host root). Strictly refuse with
	 * -EPERM to prevent container escape and host privilege escalation.
	 */
	if (ns == droid_lkm_userns_init_uns)
		return -EPERM;

	targ_u = droid_lkm_userns_from_ns(ns);
	if (!targ_u)
		return orig(cred, ns, cap, opts);

	if (!cred)
		return -EPERM;

	cred_u = droid_lkm_userns_from_ns(cred->user_ns);

	/*
	 * Mirrors cap_capable() from security/commoncap.c:
	 * Examine target namespace and all of its ancestors.
	 */
	while (targ_u) {
		/* Do we have the capability in the current namespace? */
		if (cred_u == targ_u) {
			if (cap_raised(cred->cap_effective, cap))
				return 0;
			return -EPERM;
		}

		/* If we're already at a lower level than we're looking for, stop */
		if (targ_u->level <= (cred_u ? cred_u->level : 0))
			return -EPERM;

		/* The owner of the user namespace has all caps in it */
		if (targ_u->uns.parent == (cred_u ? &cred_u->uns : droid_lkm_userns_init_uns)) {
			if (uid_eq(cred->euid, targ_u->uns.owner))
				return 0;
			if (cred_u) {
				if (cap_raised(cred->cap_effective, cap))
					return 0;
			} else {
				if (orig(cred, droid_lkm_userns_init_uns, cap, opts) == 0)
					return 0;
			}
			return -EPERM;
		}

		/* Walk up towards root */
		targ_u = droid_lkm_userns_from_ns(targ_u->uns.parent);
	}

	return -EPERM;
}

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
	const struct cred *cred = new_cred ? new_cred : current_cred();
	struct droid_lkm_userns *u = cred ? droid_lkm_userns_from_ns(cred->user_ns) : NULL;
	struct nsproxy *nsp;
	typeof(droid_lkm_userns_nsproxy_orig) orig = DROID_LKM_USERNS_ORIG(nsproxy);

	if (!orig)
		return -EINVAL;

	if (!u || !cap_raised(cred->cap_effective, CAP_SYS_ADMIN))
		return orig(unshare_flags, new_nsp, new_cred, new_fs);

	nsp = droid_lkm_misc_ks.create_new_namespaces(unshare_flags, current, &u->uns,
						      new_fs ? new_fs : droid_lkm_misc_task_fs(current));
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	/*
	 * get_user_ns() is a stub returning the initial namespace, so constructors stored
	 * the wrong owner. Only namespaces created by this call are touched.
	 */
	if (unshare_flags & CLONE_NEWUTS)
		nsp->uts_ns->user_ns = &u->uns;
	if ((unshare_flags & CLONE_NEWIPC) && nsp->ipc_ns)
		nsp->ipc_ns->user_ns = &u->uns;
	if (unshare_flags & CLONE_NEWNET)
		nsp->net_ns->user_ns = &u->uns;
	if (unshare_flags & CLONE_NEWCGROUP)
		nsp->cgroup_ns->user_ns = &u->uns;
	if ((unshare_flags & CLONE_NEWTIME) && nsp->time_ns_for_children)
		nsp->time_ns_for_children->user_ns = &u->uns;

	*new_nsp = nsp;
	return 0;
}

__nocfi noinline int droid_lkm_userns_unshare_wrap(unsigned long flags)
{
	struct droid_lkm_userns *u;
	const struct cred *old;
	unsigned long rest;
	int ret;
	typeof(droid_lkm_userns_unshare_orig) orig = DROID_LKM_USERNS_ORIG(unshare);

	if (!orig)
		return -EINVAL;

	if (!(flags & CLONE_NEWUSER))
		return orig(flags);

	/* upstream rejects this pair before it touches anything */
	if (flags & CLONE_FS)
		return -EINVAL;

	/* allocate before the kernel changes anything, so failure is free */
	u = droid_lkm_userns_create(droid_lkm_userns_of(current));
	if (IS_ERR(u))
		return PTR_ERR(u);

	old = get_task_cred(current);

	ret = droid_lkm_userns_install_current(u);
	if (ret) {
		put_cred(old);
		droid_lkm_userns_put(u);
		return ret;
	}

	rest = flags & ~CLONE_NEWUSER;
	ret = orig(rest);
	if (ret) {
		droid_lkm_misc_ks.commit_creds((struct cred *)old);
		droid_lkm_userns_put(u);
		return ret;
	}

	put_cred(old);
	droid_lkm_userns_put(u);

	return 0;
}

__nocfi noinline int droid_lkm_userns_creds_wrap(struct task_struct *p,
						unsigned long clone_flags)
{
	struct droid_lkm_userns *u;
	unsigned long rest;
	int ret;
	typeof(droid_lkm_userns_creds_orig) orig = DROID_LKM_USERNS_ORIG(creds);

	if (!orig)
		return -EINVAL;

	/* a thread clone shares credentials, so it cannot own a namespace */
	if (!(clone_flags & CLONE_NEWUSER) || (clone_flags & CLONE_THREAD))
		return orig(p, clone_flags);

	u = droid_lkm_userns_create(droid_lkm_userns_of(current));
	if (IS_ERR(u))
		return PTR_ERR(u);

	rest = clone_flags & ~CLONE_NEWUSER;
	ret = orig(p, rest);
	if (ret) {
		droid_lkm_userns_put(u);
		return ret;
	}

	/* the child has not run and copy_creds() built a fresh credential, write both cred pointers */
	{
		struct cred *p_real = (struct cred *)droid_lkm_misc_task_real_cred(p);
		struct cred *p_cred = (struct cred *)droid_lkm_misc_task_cred(p);

		if (p_real)
			droid_lkm_userns_install_cred(p_real, u);
		if (p_cred && p_cred != p_real)
			droid_lkm_userns_install_cred(p_cred, u);
	}

	droid_lkm_userns_put(u);

	return 0;
}

__nocfi noinline bool droid_lkm_userns_inodecap_wrap(DROID_LKM_IDMAP_PARAM
						     const struct inode *inode,
						     int cap)
{
	struct droid_lkm_userns *u;
	typeof(droid_lkm_userns_inodecap_orig) orig = DROID_LKM_USERNS_ORIG(inodecap);

	if (!orig)
		return false;

	if (orig(DROID_LKM_IDMAP_PASS inode, cap))
		return true;

	/* an idmapped mount is a different rule set, it arrives with the lens */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	if (!droid_lkm_misc_ks.nop_mnt_idmap ||
	    (const void *)idmap != (const void *)droid_lkm_misc_ks.nop_mnt_idmap)
		return false;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	if (mnt_userns && mnt_userns != &init_user_ns)
		return false;
#endif

	u = droid_lkm_userns_of(current);
	if (!u)
		return false;

	if (!cap_raised(current_cred()->cap_effective, cap))
		return false;

	/* privileged_wrt_inode_uidgid(): both ids must be mapped in the namespace */
	return droid_lkm_userns_has_mapping(u, __kuid_val(inode->i_uid), true) &&
	       droid_lkm_userns_has_mapping(u, __kgid_val(inode->i_gid), false);
}

/*
 * mount_capable() asks for CAP_SYS_ADMIN in the initial namespace when the filesystem
 * is not FS_USERNS_MOUNT, because capable() is compiled against current_user_ns(). a
 * namespace created by host root is allowed to mount those (devtmpfs above all); a
 * confined one is not, it only gets what a user namespace may mount
 */
__nocfi noinline bool droid_lkm_userns_mountcap_wrap(struct fs_context *fc)
{
	struct droid_lkm_userns *u;
	typeof(droid_lkm_userns_mountcap_orig) orig = DROID_LKM_USERNS_ORIG(mountcap);

	if (!orig)
		return false;

	if (orig(fc))
		return true;

	if (!fc || !fc->fs_type || (fc->fs_type->fs_flags & FS_USERNS_MOUNT))
		return false;

	if (!cap_raised(current_cred()->cap_effective, CAP_SYS_ADMIN))
		return false;

	u = droid_lkm_userns_from_ns(fc->user_ns);
	if (!u || !u->privileged)
		return false;

	return true;
}

/*
 * chown_common() resolves the new owner with make_kuid(current_user_ns(), user), and this
 * kernel compiles away both halves of that: current_user_ns() is the constant initial
 * namespace and make_kuid() is the identity. A container asking for its own id 0 therefore
 * writes kuid 0, the host's root, onto a file it owns, and then cannot undo it: the id it
 * holds is no longer mapped in its namespace, so every later chown or chmod there is
 * refused and the file stays behind on the host as root's.
 *
 * There is no function to correct, the map lookup is inlined at every caller, so do at the
 * entry what the missing lookup would have done: translate the id the caller named, and
 * refuse one the namespace does not map, which is the -EINVAL that make_kuid() returning
 * an invalid id makes chown_common() return.
 */
__nocfi noinline long droid_lkm_userns_fchownat_wrap(const struct pt_regs *regs)
{
	typeof(droid_lkm_userns_fchownat_orig) orig = DROID_LKM_USERNS_ORIG(fchownat);
	struct pt_regs *r = (struct pt_regs *)regs;
	struct droid_lkm_userns *u;
	u32 id;

	if (!orig)
		return -EINVAL;

	u = droid_lkm_userns_of(current);
	if (u) {
		/* -1 is not an id, it is how the caller says leave this one alone */
		if ((uid_t)r->regs[2] != (uid_t)-1) {
			id = droid_lkm_userns_map_down(u, (uid_t)r->regs[2], true);
			if (id == DROID_LKM_USERNS_ID_UNMAPPED)
				return -EINVAL;
			r->regs[2] = id;
		}

		if ((gid_t)r->regs[3] != (gid_t)-1) {
			id = droid_lkm_userns_map_down(u, (gid_t)r->regs[3], false);
			if (id == DROID_LKM_USERNS_ID_UNMAPPED)
				return -EINVAL;
			r->regs[3] = id;
		}
	}

	return orig(regs);
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

	WRITE_ONCE(*orig, (void *)h->orig);
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

	ret = droid_lkm_userns_hook_one(&droid_lkm_userns_mountcap_hook, "mount_capable",
		"droid_lkm_userns_mountcap_wrap",
		(void **)&droid_lkm_userns_mountcap_orig);
	if (ret)
		goto out_inodecap;

	ret = droid_lkm_userns_hook_one(&droid_lkm_userns_seccap_hook, "security_capable",
		"droid_lkm_userns_seccap_wrap",
		(void **)&droid_lkm_userns_seccap_orig);
	if (ret)
		goto out_mountcap;

	ret = droid_lkm_userns_hook_one(&droid_lkm_userns_prepcred_hook, "prepare_creds",
		"droid_lkm_userns_prepcred_wrap",
		(void **)&droid_lkm_userns_prepcred_orig);
	if (ret)
		goto out_seccap;

	ret = droid_lkm_userns_hook_one(&droid_lkm_userns_putcred_hook, "__put_cred",
		"droid_lkm_userns_putcred_wrap",
		(void **)&droid_lkm_userns_putcred_orig);
	if (ret)
		goto out_prepcred;

	/*
	 * an addition rather than a requirement: without it ids pass through as they always
	 * have, which is wrong for chown but no worse than the answer the kernel gives on
	 * its own, and losing the whole feature over it would be the larger harm
	 */
	if (droid_lkm_userns_hook_one(&droid_lkm_userns_fchownat_hook,
				      "__arm64_sys_fchownat",
				      "droid_lkm_userns_fchownat_wrap",
				      (void **)&droid_lkm_userns_fchownat_orig)) {
		droid_lkm_misc_report("userns chown map", "unsupported",
				      "fchownat hook unavailable, ids pass through");
	} else {
		droid_lkm_misc_report("userns chown map", "ready",
				      "uid and gid translated at fchownat");
	}

	droid_lkm_misc_info("userns hooks installed\n");
	return 0;

out_prepcred:
	hk_inline_unhook(&droid_lkm_userns_prepcred_hook);
out_seccap:
	hk_inline_unhook(&droid_lkm_userns_seccap_hook);
out_mountcap:
	hk_inline_unhook(&droid_lkm_userns_mountcap_hook);
out_inodecap:
	hk_inline_unhook(&droid_lkm_userns_inodecap_hook);
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
	hk_inline_unhook(&droid_lkm_userns_fchownat_hook);
	hk_inline_unhook(&droid_lkm_userns_putcred_hook);
	hk_inline_unhook(&droid_lkm_userns_prepcred_hook);
	hk_inline_unhook(&droid_lkm_userns_seccap_hook);
	hk_inline_unhook(&droid_lkm_userns_mountcap_hook);
	hk_inline_unhook(&droid_lkm_userns_inodecap_hook);
	hk_inline_unhook(&droid_lkm_userns_creds_hook);
	hk_inline_unhook(&droid_lkm_userns_nsproxy_hook);
	hk_inline_unhook(&droid_lkm_userns_unshare_hook);
}

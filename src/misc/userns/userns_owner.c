// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * The owners the kernel filled in through a stub.
 *
 * get_user_ns() and current_user_ns() are both compiled against
 * CONFIG_USER_NS. Without it the first returns the initial namespace whatever
 * it is handed, and the second does not read the credential at all. Every
 * object the kernel fills in through one of them, and every decision it makes
 * from one of them, therefore names the host where it should name the
 * container.
 *
 * may_mount() is the sharp end. It is one line, so the compiler inlines it into
 * path_mount() and no hook can reach the copy that runs, while ns_capable()
 * refuses a capability question about the initial namespace to a credential
 * that sits below it - the level test that makes a user namespace an isolation
 * boundary in the first place. A container that owns a user namespace then
 * cannot mount anything, so the state is corrected here instead of the
 * decisions.
 *
 * copy_mnt_ns() is the one place a task's mount namespace is created and the
 * kernel hands it the owner it means to record. fs_context_for_mount() is where
 * a mount configuration gets its owner, which mount_capable() then asks about
 * and which becomes the superblock's s_user_ns. Both are given the container's
 * own namespace.
 *
 * struct mnt_namespace is private to fs/mount.h and not part of a module build,
 * so that one field is located at runtime: the namespace is identified through
 * ns_common, the value to search for comes from the kernel's own mntns_owner()
 * accessor, and a write is only kept when that accessor reads it back. struct
 * fs_context is public and is written directly.
 */

#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mnt_namespace.h>
#include <linux/ns_common.h>
#include <linux/nsproxy.h>
#include <linux/proc_ns.h>
#include <linux/user_namespace.h>

#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "ds.h"
#include "userns.h"
#include "hk_inline.h"

/* last offset of struct mnt_namespace that can still hold its owner */
#define DROID_LKM_USERNS_MNT_SCAN 128
/* how far above the text base a resolved callback is allowed to sit */
#define DROID_LKM_USERNS_TEXT_WIN (64UL << 20)

typedef struct user_namespace *(*droid_lkm_userns_owner_t)(struct ns_common *ns);

struct user_namespace *droid_lkm_userns_init_uns;

static struct hk_inline droid_lkm_userns_copymntns_hook;
static struct hk_inline droid_lkm_userns_fsctx_hook;
static struct mnt_namespace *(*droid_lkm_userns_copymntns_orig)(
		unsigned long flags, struct mnt_namespace *ns,
		struct user_namespace *user_ns, struct fs_struct *new_fs);
static struct fs_context *(*droid_lkm_userns_fsctx_orig)(
		struct file_system_type *type, unsigned int sb_flags);
static const struct proc_ns_operations *droid_lkm_userns_mnt_ops;
static droid_lkm_userns_owner_t droid_lkm_userns_mnt_owner;
static unsigned int droid_lkm_userns_mnt_off;

static bool droid_lkm_userns_is_text(unsigned long addr)
{
	return addr >= kernel_base && addr - kernel_base < DROID_LKM_USERNS_TEXT_WIN &&
	       !(addr & (sizeof(void *) - 1));
}

/*
 * the layout is checked before use: name reads "mnt" and type is CLONE_NEWNS, which pins the
 * two fields the offset follows from
 */
static int droid_lkm_userns_mnt_ops_resolve(void)
{
	struct proc_ns_operations ops;
	char text[8] = { };
	unsigned long addr;

	addr = droid_lkm_misc_sym("mntns_operations");
	if (!addr) {
		droid_lkm_misc_warn("mntns_operations not found\n");
		return -ENOENT;
	}

	if (safe_read(&ops, (void *)addr, sizeof(ops)))
		return -EFAULT;

	if (!ops.name || safe_read(text, ops.name, 4) || memcmp(text, "mnt", 4))
		return -EINVAL;

	/*
	 * 6.18 moved the type off proc_ns_operations and onto ns_common::ns_type,
	 * where this table is not the thing that carries it. the name check above
	 * is what pins the table on those kernels; on every version that still
	 * has the member, keep the second answer
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
	if (ops.type != CLONE_NEWNS)
		return -EINVAL;
#endif

	if (!ops.owner || !droid_lkm_userns_is_text((unsigned long)ops.owner))
		return -EINVAL;

	droid_lkm_userns_mnt_ops = (const struct proc_ns_operations *)addr;
	droid_lkm_userns_mnt_owner = ops.owner;

	return 0;
}

/*
 * a candidate must hold the answer the stub gave, and the write is kept only when the
 * kernel's own accessor reads it back
 */
static int droid_lkm_userns_mnt_try(struct mnt_namespace *mnt_ns,
				    unsigned int off, struct user_namespace *want)
{
	void **field = (void **)((char *)mnt_ns + off);
	void *old;

	if (safe_read(&old, field, sizeof(old)))
		return -EFAULT;

	if (old != droid_lkm_userns_init_uns)
		return -ENOENT;

	WRITE_ONCE(*field, want);
	if (droid_lkm_userns_mnt_owner((struct ns_common *)mnt_ns) == want)
		return 0;

	WRITE_ONCE(*field, old);

	return -EINVAL;
}

static int droid_lkm_userns_mnt_own(struct mnt_namespace *mnt_ns,
				    struct user_namespace *want)
{
	struct ns_common *ns = (struct ns_common *)mnt_ns;
	unsigned int off;

	if (ns->ops != droid_lkm_userns_mnt_ops)
		return -EINVAL;

	if (droid_lkm_userns_mnt_owner(ns) == want)
		return 0;

	if (droid_lkm_userns_mnt_off)
		return droid_lkm_userns_mnt_try(mnt_ns, droid_lkm_userns_mnt_off, want);

	for (off = sizeof(*ns); off <= DROID_LKM_USERNS_MNT_SCAN; off += sizeof(void *)) {
		if (droid_lkm_userns_mnt_try(mnt_ns, off, want))
			continue;

		droid_lkm_userns_mnt_off = off;
		droid_lkm_misc_dbg("mnt ns owner at offset %u\n", off);
		droid_lkm_misc_report("userns mount owner", "ready",
				      "field found and verified");

		return 0;
	}

	droid_lkm_misc_report("userns mount owner", "unsupported",
			      "field not found, mounts in the namespace will fail");

	return -ENOTSUPP;
}

__nocfi noinline struct mnt_namespace *
droid_lkm_userns_copymntns_wrap(unsigned long flags, struct mnt_namespace *ns,
				struct user_namespace *user_ns,
				struct fs_struct *new_fs)
{
	struct mnt_namespace *new_ns;

	if (!droid_lkm_userns_copymntns_orig)
		return ERR_PTR(-EAGAIN);

	new_ns = droid_lkm_userns_copymntns_orig(flags, ns, user_ns, new_fs);

	if (IS_ERR(new_ns) || !(flags & CLONE_NEWNS))
		return new_ns;

	/* user_ns is the owner the kernel was handed, ours only for our own */
	if (!droid_lkm_userns_is_ours(user_ns))
		return new_ns;

	if (droid_lkm_userns_mnt_own(new_ns, user_ns))
		droid_lkm_misc_err("mount namespace kept the host as owner, mounts in it will fail\n");

	return new_ns;
}

__nocfi noinline struct fs_context *
droid_lkm_userns_fsctx_wrap(struct file_system_type *type, unsigned int sb_flags)
{
	struct fs_context *fc;
	struct droid_lkm_userns *u;

	if (!droid_lkm_userns_fsctx_orig)
		return ERR_PTR(-EAGAIN);

	fc = droid_lkm_userns_fsctx_orig(type, sb_flags);

	/*
	 * alloc_fs_context() took the owner from current_user_ns() and get_user_ns() took no
	 * reference, so replacing the field is all
	 */
	u = IS_ERR(fc) ? NULL : droid_lkm_userns_of(current);
	if (u && fc->user_ns != &u->uns) {
		fc->user_ns = &u->uns;
		droid_lkm_misc_dbg("fs_context for %s owned by the container\n", type->name);
	}

	return fc;
}

int droid_lkm_userns_owner_init(void)
{
	int ret;

	droid_lkm_userns_init_uns = (struct user_namespace *)
		droid_lkm_misc_sym("init_user_ns");
	if (!droid_lkm_userns_init_uns) {
		droid_lkm_misc_warn("init_user_ns not found\n");
		return -ENOENT;
	}

	ret = droid_lkm_userns_mnt_ops_resolve();
	if (ret) {
		droid_lkm_misc_warn("mntns_operations unusable: %d\n", ret);
		return ret;
	}

	ret = droid_lkm_inline_hook(&droid_lkm_userns_copymntns_hook, "copy_mnt_ns",
				    "droid_lkm_userns_copymntns_wrap");
	if (ret) {
		droid_lkm_misc_warn("hook copy_mnt_ns failed: %d\n", ret);
		return ret;
	}
	droid_lkm_userns_copymntns_orig = (void *)droid_lkm_userns_copymntns_hook.orig;

	ret = droid_lkm_inline_hook(&droid_lkm_userns_fsctx_hook, "fs_context_for_mount",
				    "droid_lkm_userns_fsctx_wrap");
	if (ret) {
		droid_lkm_misc_warn("hook fs_context_for_mount failed: %d\n", ret);
		hk_inline_unhook(&droid_lkm_userns_copymntns_hook);
		droid_lkm_userns_copymntns_orig = NULL;
		return ret;
	}
	droid_lkm_userns_fsctx_orig = (void *)droid_lkm_userns_fsctx_hook.orig;

	droid_lkm_misc_dbg("owner: mntns_ops=0x%lx owner=0x%lx, both hooks up\n",
		(unsigned long)droid_lkm_userns_mnt_ops,
		(unsigned long)droid_lkm_userns_mnt_owner);

	return 0;
}

void droid_lkm_userns_owner_exit(void)
{
	if (droid_lkm_userns_fsctx_orig) {
		hk_inline_unhook(&droid_lkm_userns_fsctx_hook);
		droid_lkm_userns_fsctx_orig = NULL;
	}
	if (droid_lkm_userns_copymntns_orig) {
		hk_inline_unhook(&droid_lkm_userns_copymntns_hook);
		droid_lkm_userns_copymntns_orig = NULL;
	}
}

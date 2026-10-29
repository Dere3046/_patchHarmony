// SPDX-License-Identifier: GPL-2.0-only
/*
 * Credential side of installing a fake user namespace. Mirrors
 * set_cred_user_ns() from kernel/user_namespace.c: the full capability set goes
 * to the new namespace, where cap_capable() honours it, while every check
 * against the initial namespace fails because of the level comparison.
 *
 * One deliberate departure from set_cred_user_ns(): the inheritable and ambient
 * sets are full too. cap_bprm_creds_from_file() keys the root behaviour off
 * euid, and this kernel has no id translation (CONFIG_USER_NS off compiles
 * from_kuid to the identity), so a confined creator keeps its host euid and
 * never presents euid 0. With an empty ambient set the first execve() of a
 * container binary would therefore drop every capability and leave a namespace
 * that can no longer mount or create anything. Ambient survives execve() and is
 * folded into pP' and pE', which is what keeps the grant alive across the
 * exec the runtime always performs. Privileged files still clear it, so a
 * setuid binary inside the namespace gains nothing from this.
 */

#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <linux/key.h>

#include "misc.h"
#include "misc_ksym.h"
#include "userns.h"

void droid_lkm_userns_install_cred(struct cred *new, struct droid_lkm_userns *u)
{
	new->securebits = SECUREBITS_DEFAULT;
	new->cap_inheritable = CAP_FULL_SET;
	new->cap_permitted = CAP_FULL_SET;
	new->cap_effective = CAP_FULL_SET;
	new->cap_ambient = CAP_FULL_SET;
	new->cap_bset = CAP_FULL_SET;
#ifdef CONFIG_KEYS
	if (new->request_key_auth) {
		/*
		 * the old key belongs to the namespace being left, so upstream drops
		 * it here. the thunk is optional: when the kernel hides key_put the
		 * pointer is left alone and the credential's own release path puts it,
		 * which is better than clearing the reference and leaking the key
		 */
		if (droid_lkm_misc_ks.key_put) {
			droid_lkm_misc_ks.key_put(new->request_key_auth);
			new->request_key_auth = NULL;
		}
	}
#endif
	if (new->user_ns == (u ? &u->uns : droid_lkm_userns_init_uns))
		return;

	{
		struct droid_lkm_userns *old_u = droid_lkm_userns_from_ns(new->user_ns);

		if (u)
			droid_lkm_userns_get(u);
		new->user_ns = u ? &u->uns : droid_lkm_userns_init_uns;
		if (old_u)
			droid_lkm_userns_put(old_u);
	}
}

int droid_lkm_userns_install_current(struct droid_lkm_userns *u)
{
	struct cred *new;

	new = droid_lkm_misc_ks.prepare_creds();
	if (!new)
		return -ENOMEM;

	droid_lkm_userns_install_cred(new, u);
	droid_lkm_misc_ks.commit_creds(new);

	return 0;
}

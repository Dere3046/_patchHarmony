// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * The install time self test, three layers.
 *
 * Layer one sweeps the map functions over their whole input domain and lives
 * next to them, in userns_map.c. Layer two checks that nothing the module
 * promised reported itself refused. Layer three walks the path a container
 * takes, from a kernel thread: take a user namespace, take a uts namespace and
 * check that it is owned by it, take a mount namespace and check that it is
 * owned by it too - which is the field may_mount() reads, and the one a
 * namespace left owned by the host gets wrong.
 *
 * The mount itself is asked about rather than made, and that is not a
 * simplification: path_mount() runs security_sb_mount() ahead of may_mount(), so
 * a mount from kernel context is refused by the LSM before the capability is
 * ever consulted. Attempting it would fail here for a reason that has nothing to
 * do with the namespace, which is exactly the false alarm this test used to
 * raise.
 *
 * Layer three is the one that matters, because it runs the same code a
 * container runs, on the device, before any container exists. A device where
 * this fails is a device where the first container to mount something would
 * have failed, and it says so in the log instead. It runs in its own thread
 * with its own namespaces and disappears with them, so nothing outside is
 * touched.
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/nsproxy.h>
#include <linux/mnt_namespace.h>

#include "misc.h"
#include "misc_ksym.h"
#include "misc_proc.h"
#include "userns.h"

static int (*droid_lkm_userns_selftest_unshare)(unsigned long flags);

static unsigned int droid_lkm_userns_selftest_failed;
static struct completion droid_lkm_userns_selftest_done;

static void droid_lkm_userns_selftest_step(const char *what, int ok, int ret)
{
	if (ok)
		return;

	droid_lkm_userns_selftest_failed++;
	droid_lkm_misc_err("selftest: %s failed: %d\n", what, ret);
}

/*
 * the sample: a user namespace of this module's making, a uts namespace recording it as
 * owner, and a mount namespace that has to record it too
 */
static int droid_lkm_userns_selftest_thread(void *unused)
{
	struct droid_lkm_userns *u;
	int ret;

	if (droid_lkm_userns_of(current))
		droid_lkm_userns_selftest_step("starts outside a namespace", 0, 0);

	ret = droid_lkm_userns_selftest_unshare(CLONE_NEWUSER);
	u = droid_lkm_userns_of(current);
	droid_lkm_userns_selftest_step("unshare(CLONE_NEWUSER)", ret == 0 && u, ret);

	if (!u)
		goto out;

	ret = droid_lkm_userns_selftest_unshare(CLONE_NEWUTS);
	droid_lkm_userns_selftest_step("unshare(CLONE_NEWUTS)", ret == 0, ret);
	if (!ret)
		droid_lkm_userns_selftest_step("the uts namespace is owned by it",
			current->nsproxy->uts_ns->user_ns == &u->uns, 0);

	ret = droid_lkm_userns_selftest_unshare(CLONE_NEWNS);
	droid_lkm_userns_selftest_step("unshare(CLONE_NEWNS)", ret == 0, ret);
	if (ret)
		goto out;

	/*
	 * the field may_mount() reads, and the one this whole half turns on: a mount
	 * namespace created while the task already carries the namespace comes out
	 * owned by it, and one created before - which is what a single
	 * unshare(CLONE_NEWUSER|CLONE_NEWNS) did while the credential was installed
	 * afterwards - comes out owned by the host, and then no mount in it can ever
	 * succeed
	 */
	droid_lkm_userns_selftest_step("the mount namespace is owned by it",
		droid_lkm_userns_mntns_owner(current->nsproxy->mnt_ns) == &u->uns, 0);

out:
	complete(&droid_lkm_userns_selftest_done);

	return 0;
}

static int droid_lkm_userns_selftest_e2e(void)
{
	struct task_struct *task;

	task = kthread_run(droid_lkm_userns_selftest_thread, NULL, "dlm_userns_probe");
	if (IS_ERR(task)) {
		droid_lkm_misc_err("selftest: no thread: %ld\n", PTR_ERR(task));
		return PTR_ERR(task);
	}

	if (!wait_for_completion_timeout(&droid_lkm_userns_selftest_done, 10 * HZ)) {
		droid_lkm_misc_err("selftest: timed out\n");
		droid_lkm_userns_selftest_failed++;
	}

	return 0;
}

static int droid_lkm_userns_selftest_resolve(void)
{
	struct {
		const char *name;
		void **slot;
	} syms[] = {
		{ "ksys_unshare",	(void **)&droid_lkm_userns_selftest_unshare },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(syms); i++) {
		*syms[i].slot = (void *)droid_lkm_misc_sym(syms[i].name);
		if (!*syms[i].slot) {
			droid_lkm_misc_warn("selftest: %s not found\n", syms[i].name);
			return -ENOENT;
		}
	}

	return 0;
}

/* layer two counts the refusals the report already holds */
static void droid_lkm_userns_selftest_coverage(void)
{
	unsigned int refused = droid_lkm_misc_report_count("unsupported");

	droid_lkm_userns_selftest_failed += refused;

	if (refused)
		droid_lkm_misc_err("selftest: %u features reported unsupported\n",
				   refused);
}

void droid_lkm_userns_selftest_run(void)
{
	char detail[64];
	int ret;

	droid_lkm_userns_selftest_coverage();

	ret = droid_lkm_userns_selftest_resolve();
	if (ret) {
		droid_lkm_misc_report("userns selftest", "unsupported",
				      "a symbol for the sample is missing");
		return;
	}

	init_completion(&droid_lkm_userns_selftest_done);
	droid_lkm_userns_selftest_e2e();

	if (droid_lkm_userns_selftest_failed) {
		scnprintf(detail, sizeof(detail), "%u checks failed",
			  droid_lkm_userns_selftest_failed);
		droid_lkm_misc_report("userns selftest", "failed", detail);
		droid_lkm_misc_err("userns selftest failed: %s\n", detail);
		return;
	}

	droid_lkm_misc_report("userns selftest", "ready",
			      "3 layers, no failures");
	droid_lkm_misc_info("userns selftest passed\n");
}

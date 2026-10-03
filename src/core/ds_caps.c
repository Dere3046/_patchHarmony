// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/user_namespace.h>
#include <asm/unistd.h>

#include "ds.h"
#include "ds_caps.h"
#include "ds_ksym.h"

struct droid_lkm_caps droid_lkm_caps;

const char *droid_lkm_owner_name(enum droid_lkm_owner owner)
{
	switch (owner) {
	case DROID_LKM_KERNEL:
		return "kernel";
	case DROID_LKM_MODULE:
		return "module";
	default:
		return "absent";
	}
}

/*
 * the syscall slot alone cannot answer "does this kernel serve the call": on
 * arm64 every number is wired, and a syscall whose config is off points at the
 * __weak COND_SYSCALL stub that still carries the real name. mq_open therefore
 * reads as __arm64_sys_mq_open on a kernel that has no mqueue at all and only
 * returns ENOSYS. presence is answered by symbols only that feature compiles,
 * and for mqueue the filesystem registration in ipc_mqueue.c overrules this.
 * the slot dump stays because it is what shows the stub is there
 */
static void droid_lkm_caps_slot_dump(unsigned long *tab, int nr, const char *what)
{
	char sym[64];
	unsigned long cur;

	if (!tab || nr <= 0)
		return;
	cur = READ_ONCE(tab[nr]);
	if (sym_name_at(cur, sym, sizeof(sym)) < 0)
		snprintf(sym, sizeof(sym), "?");
	droid_lkm_dbg("classify slot %d (%s): 0x%lx[%s]\n", nr, what, cur, sym);
}

/* first name the running kernel carries, 0 when it carries none of them */
static unsigned long droid_lkm_caps_sym_any(const char *const *names, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		unsigned long addr = droid_lkm_sym(names[i]);

		if (addr)
			return addr;
	}
	return 0;
}

static void droid_lkm_caps_classify(void)
{
	static const char *const mqueue_syms[] = {
		"mqueue_fs_type", "mqueue_inode_cachep", "mq_clear_sbinfo",
	};
	/* all three live in ipc/namespace.c, built by CONFIG_IPC_NS */
	static const char *const ipcns_syms[] = {
		"free_ipcs", "ipcns_get", "copy_ipcs",
	};
	/*
	 * sysvipc is asked of ipc/util.c and ipc/msg.c only. free_ipcs reads like a
	 * SysV helper and is not one: it lives in ipc/namespace.c, so it answers
	 * CONFIG_IPC_NS and says nothing about CONFIG_SYSVIPC. a stock GKI 6.1
	 * kernel splits exactly that pair, namespaces on and SysV off, which is how
	 * a wrong owner word got printed for it
	 */
	static const char *const sysvipc_syms[] = {
		"ksys_msgget", "ipc_addid", "ipc_obtain_object_check",
	};
	struct droid_lkm_caps *caps = &droid_lkm_caps;
	unsigned long *tab = (unsigned long *)droid_lkm_sym("sys_call_table");

	droid_lkm_caps_slot_dump(tab, __NR_mq_open, "mq_open");
	droid_lkm_caps_slot_dump(tab, __NR_msgget, "msgget");

	/*
	 * ipcns_operations is emitted only when the kernel wires ipc namespaces.
	 * without it there is no field to install into either, and the module
	 * serves its own namespace objects out of its own tables
	 */
	if (droid_lkm_ks_ipcns_operations ||
	    droid_lkm_caps_sym_any(ipcns_syms, ARRAY_SIZE(ipcns_syms))) {
		caps->ipc_ns.owner = DROID_LKM_KERNEL;
		caps->ipc_ns.reason = "the kernel's ipc namespace objects are in the image";
	} else {
		caps->ipc_ns.owner = DROID_LKM_MODULE;
		caps->ipc_ns.reason = "no ipcns_operations, the module serves the table";
	}

	if (droid_lkm_caps_sym_any(mqueue_syms, ARRAY_SIZE(mqueue_syms))) {
		caps->posix_mqueue.owner = DROID_LKM_KERNEL;
		caps->posix_mqueue.reason = "the kernel's own mqueue objects are in the image";
	} else if (caps->ipc_ns.owner == DROID_LKM_KERNEL) {
		caps->posix_mqueue.owner = DROID_LKM_ABSENT;
		caps->posix_mqueue.reason = "the kernel owns ipc namespaces and has no mqueue, a namespace it makes has no mqueue mount";
	} else {
		caps->posix_mqueue.owner = DROID_LKM_MODULE;
		caps->posix_mqueue.reason = "no kernel mqueue objects, the module port serves it";
	}

	if (droid_lkm_caps_sym_any(sysvipc_syms, ARRAY_SIZE(sysvipc_syms))) {
		caps->sysvipc.owner = DROID_LKM_KERNEL;
		caps->sysvipc.reason = "the kernel's SysV calls are in the image";
	} else {
		caps->sysvipc.owner = DROID_LKM_MODULE;
		caps->sysvipc.reason = "no ksys_msgget/ipc_addid, the module port serves the calls";
	}

	if (droid_lkm_sym("compat_sys_call_table")) {
		caps->compat32.owner = DROID_LKM_KERNEL;
		caps->compat32.reason = "compat_sys_call_table is in the image";
	} else {
		caps->compat32.owner = DROID_LKM_ABSENT;
		caps->compat32.reason = "no compat_sys_call_table, 32 bit slots stay as they are";
	}
}

static unsigned int droid_lkm_caps_parse_version(void)
{
	unsigned int major = 0, minor = 0, patch = 0;

	if (sscanf(utsname()->release, "%u.%u.%u", &major, &minor, &patch) < 2)
		return 0;
	return DROID_LKM_VERSION(major, minor, patch);
}

int droid_lkm_caps_init(void)
{
	struct droid_lkm_caps *caps = &droid_lkm_caps;
	void *idmap_none = (void *)droid_lkm_sym("nop_mnt_idmap");

	caps->version = droid_lkm_caps_parse_version();
	if (!caps->version) {
		droid_lkm_err("cannot parse the kernel release, refusing to load\n");
		return -ENODEV;
	}

	if (caps->version < DROID_LKM_VERSION(5, 10, 0)) {
		droid_lkm_err("kernel %u.%u is older than 5.10, refusing to load\n",
			      caps->version >> 16, (caps->version >> 8) & 0xff);
		return -ENODEV;
	}

	/*
	 * the idmap argument replaced the user namespace one in 6.3. the symbol
	 * probe decides when it answers, the version only covers a kernel that
	 * carries neither name.
	 */
	caps->perm_takes_idmap = idmap_none != NULL ||
				 caps->version >= DROID_LKM_VERSION(6, 3, 0);
	caps->idmap_none = idmap_none ? idmap_none : (void *)&init_user_ns;
	/*
	 * do_mmap gained vm_flags in the same generation that moved the locked
	 * unmap entry to do_vmi_munmap, so the symbol answers this one and the
	 * version is not consulted.
	 */
	caps->mmap_takes_vm_flags = droid_lkm_sym("do_vmi_munmap") != 0;
	caps->has_ns_count = caps->version >= DROID_LKM_VERSION(5, 15, 0);
	caps->ctl_takes_table = caps->version < DROID_LKM_VERSION(6, 12, 0);

	/*
	 * a version derived value that contradicts a symbol probe means this
	 * kernel is not one this build knows. loading would install a handler
	 * with the wrong shape and corrupt state, so refuse instead.
	 */
	if (caps->version >= DROID_LKM_VERSION(6, 6, 0) &&
	    !droid_lkm_sym("do_vmi_munmap")) {
		droid_lkm_err("kernel %u.%u has no do_vmi_munmap, refusing to load\n",
			      caps->version >> 16, (caps->version >> 8) & 0xff);
		return -ENODEV;
	}
	if (caps->version < DROID_LKM_VERSION(6, 1, 0) &&
	    !droid_lkm_sym("__do_munmap")) {
		droid_lkm_err("kernel %u.%u has no __do_munmap, refusing to load\n",
			      caps->version >> 16, (caps->version >> 8) & 0xff);
		return -ENODEV;
	}

	droid_lkm_caps_classify();

	return 0;
}

/*
 * printed again once the mqueue port has run, because that is the one feature
 * whose owner is settled by registering the filesystem rather than by a symbol
 */
void droid_lkm_caps_report_features(void)
{
	const struct droid_lkm_caps *caps = &droid_lkm_caps;

	droid_lkm_info("features: ipcns=%s mqueue=%s sysvipc=%s compat32=%s\n",
		       droid_lkm_owner_name(caps->ipc_ns.owner),
		       droid_lkm_owner_name(caps->posix_mqueue.owner),
		       droid_lkm_owner_name(caps->sysvipc.owner),
		       droid_lkm_owner_name(caps->compat32.owner));
	/* the reason matters while the owner is still a surprise in a log */
	droid_lkm_dbg("feature reasons: ipcns=%s | mqueue=%s | sysvipc=%s | compat32=%s\n",
		      caps->ipc_ns.reason, caps->posix_mqueue.reason,
		      caps->sysvipc.reason, caps->compat32.reason);
}

void droid_lkm_caps_report(void)
{
	const struct droid_lkm_caps *caps = &droid_lkm_caps;

	droid_lkm_info("caps: kernel=%u.%u.%u idmap=%d idmap_none=%px vm_flags=%d ns_count=%d ctl_table=%d\n",
		       caps->version >> 16, (caps->version >> 8) & 0xff,
		       caps->version & 0xff, caps->perm_takes_idmap,
		       caps->idmap_none, caps->mmap_takes_vm_flags,
		       caps->has_ns_count, caps->ctl_takes_table);
	droid_lkm_caps_report_features();
}

/*
 * droid_lkm_toy: a joke module that makes "droidspaces check" print every
 * tick. it implements nothing: it answers the two probes that check.c makes
 *
 *   access("/proc/self/ns/<name>", F_OK)  -> faccessat/faccessat2 slot
 *   unshare(CLONE_NEWxxx) in a child      -> unshare slot
 *
 * every other path and flag falls through to the kernel, so nothing here
 * changes how the system actually behaves. the module is independent of
 * droid_lkm.ko and droid_lkm_misc.ko, and exists for the tick marks.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/shmem_fs.h>
#include <linux/netfilter.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <asm/ptrace.h>

#include "sc.h"
#include "core.h"
#include "hk.h"

#define TOY_NS_FLAGS                                                           \
	(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWIPC |            \
	 CLONE_NEWUSER | CLONE_NEWNET | CLONE_NEWCGROUP | CLONE_NEWTIME)

#define TOY_NR_UNSHARE		97
#define TOY_NR_FACCESSAT	48
#define TOY_NR_FACCESSAT2	439
#define TOY_NR_GETSOCKOPT	209

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
#define TOY_FD_FILE(_f)	fd_file(_f)
#else
#define TOY_FD_FILE(_f)	((_f).file)
#endif

static inline struct socket *toy_sock_from_file(struct file *f)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	return sock_from_file(f);
#else
	int err = 0;

	return sock_from_file(f, &err);
#endif
}
#define TOY_NR_OPENAT		56

#define TOY_NF_BASE		64
#define TOY_IP6T_GET_INFO	(TOY_NF_BASE)
#define TOY_IP6T_GET_ENTRIES	(TOY_NF_BASE + 1)
#define TOY_IP6T_GET_REV_MATCH	(TOY_NF_BASE + 4)
#define TOY_IP6T_GET_REV_TARGET	(TOY_NF_BASE + 5)
#define TOY_IPT_GET_INFO	(TOY_NF_BASE)
#define TOY_IPT_GET_ENTRIES	(TOY_NF_BASE + 1)
#define TOY_IPT_GET_REV_MATCH	(TOY_NF_BASE + 2)
#define TOY_IPT_GET_REV_TARGET	(TOY_NF_BASE + 3)

static unsigned long toy_orig_unshare;
static unsigned long toy_orig_faccessat;
static unsigned long toy_orig_faccessat2;
static unsigned long toy_orig_getsockopt;
static unsigned long toy_orig_openat;
static ssize_t (*toy_kernel_write)(struct file *f, const void *buf, size_t len,
				   loff_t *pos);
/*
 * KernCall takes its layout by pointer injection: without a resolve callback
 * sc_init refuses, so names go to KallRecon. this module defines no commands
 * of its own, only the key and the resolver
 */
static unsigned long __nocfi toy_sc_resolve(const char *name)
{
	return kallsyms_name_to_addr(name);
}

static const struct sc_layout toy_sc_layout = {
	.resolve = toy_sc_resolve,
};

static struct sc_cfg toy_sc_cfg = {
	.layout = &toy_sc_layout,
	
	.no_patch = true,
};
static unsigned int toy_ticks;

static bool toy_netfilter = true;
module_param_named(netfilter, toy_netfilter, bool, 0444);
MODULE_PARM_DESC(netfilter, "answer the ip6tables getters on raw sockets, default 1");

static bool toy_cgroups = true;
module_param_named(cgroups, toy_cgroups, bool, 0444);
MODULE_PARM_DESC(cgroups, "serve /proc/cgroups and cgroup.controllers from a fake file, default 1");

/*
 * the names check.c asks for, plus the rest of the set, so any caller probing
 * a namespace entry gets the same answer
 */
static const char *const toy_ns_names[] = {
	"pid", "ipc", "mnt", "uts", "user", "net", "cgroup", "time", "pid_for_children",
};

static bool toy_is_ns_path(const char *path)
{
	const char *p = strstr(path, "/ns/");
	unsigned int i;

	if (!p || strncmp(path, "/proc/", 6))
		return false;
	p += 4;
	for (i = 0; i < ARRAY_SIZE(toy_ns_names); i++) {
		if (!strcmp(p, toy_ns_names[i]))
			return true;
	}
	return false;
}

static bool toy_seen_unshare;

static long toy_unshare(const struct pt_regs *regs)
{
	unsigned long flags = regs->regs[0];

	if (!toy_seen_unshare) {
		toy_seen_unshare = true;
		pr_info("[droid_lkm_toy] reached: unshare flags=0x%lx\n", flags);
	}
	if (flags & TOY_NS_FLAGS) {
		toy_ticks++;
		return 0;
	}
	return ((long (*)(const struct pt_regs *))toy_orig_unshare)(regs);
}

static long toy_faccessat(const struct pt_regs *regs, unsigned long orig)
{
	static unsigned int toy_seen_faccessat;
	char path[64];
	long n;

	if (toy_seen_faccessat < 3) {
		toy_seen_faccessat++;
		n = strncpy_from_user(path, (const char __user *)regs->regs[1],
				      sizeof(path));
		pr_info("[droid_lkm_toy] faccessat: path=%s (len=%ld)\n",
			n < 0 ? "(unreadable)" : path, n);
	} else {
		n = strncpy_from_user(path, (const char __user *)regs->regs[1],
				      sizeof(path));
	}
	
	if (n >= 0 && toy_is_ns_path(path)) {
		toy_ticks++;
		return 0;
	}
	return ((long (*)(const struct pt_regs *))orig)(regs);
}

static long toy_faccessat_h(const struct pt_regs *regs)
{
	return toy_faccessat(regs, toy_orig_faccessat);
}

static long toy_faccessat2_h(const struct pt_regs *regs)
{
	return toy_faccessat(regs, toy_orig_faccessat2);
}

/*
 * /proc/filesystems lists registered filesystems, so a stub with the name is
 * enough for the devtmpfs line. it mounts nothing: the checker greps the name
 */
static struct dentry *toy_devtmpfs_mount(struct file_system_type *type, int flags,
					 const char *dev, void *data)
{
	return ERR_PTR(-ENODEV);
}

/* the mount always fails, so this never runs; kill_litter_super is protected */
static void toy_kill_sb(struct super_block *sb)
{
}

static struct file_system_type toy_devtmpfs_type = {
	.owner = THIS_MODULE,
	.name = "devtmpfs",
	.fs_flags = FS_REQUIRES_DEV,
	.mount = toy_devtmpfs_mount,
	.kill_sb = toy_kill_sb,
};

/*
 * the ip6 nat probe asks its raw socket for the nat table and for the
 * MASQUERADE revision. an empty table with that name is answered here, so the
 * lookup succeeds on a kernel whose config has neither
 */

static bool toy_is_raw_socket(int fd)
{
	
	struct fd f = fdget(fd);
	bool raw = false;

	if (TOY_FD_FILE(f)) {
		struct socket *sock = toy_sock_from_file(TOY_FD_FILE(f));

		if (sock && sock->type == SOCK_RAW)
			raw = true;
	}
	fdput(f);
	return raw;
}

static long toy_getsockopt(const struct pt_regs *regs)
{
	int fd = (int)regs->regs[0];
	int level = (int)regs->regs[1];
	int optname = (int)regs->regs[2];
	void __user *optval = (void __user *)regs->regs[3];
	bool get6, get4;

	{
		static unsigned int seen;
		bool raw = toy_is_raw_socket(fd);

		if (seen < 4) {
			seen++;
			pr_info("[droid_lkm_toy] getsockopt#%u fd=%d level=%d opt=%d raw=%d\n",
				seen, fd, level, optname, raw);
		}
		if (!toy_netfilter || !raw)
			goto pass;
	}

	get6 = level == SOL_IPV6 &&
	       (optname == TOY_IP6T_GET_INFO || optname == TOY_IP6T_GET_ENTRIES ||
		optname == TOY_IP6T_GET_REV_MATCH ||
		optname == TOY_IP6T_GET_REV_TARGET);
	get4 = level == SOL_IP &&
	       (optname == TOY_IPT_GET_INFO || optname == TOY_IPT_GET_ENTRIES ||
		optname == TOY_IPT_GET_REV_MATCH ||
		optname == TOY_IPT_GET_REV_TARGET);
	if (!get6 && !get4)
		goto pass;

	if (optname == TOY_IP6T_GET_INFO || optname == TOY_IPT_GET_INFO) {
		
		char info[84];

		memset(info, 0, sizeof(info));
		strscpy(info, "nat", 32);
		if (optval && copy_to_user(optval, info, sizeof(info)))
			return -EFAULT;
	}
	pr_info("[droid_lkm_toy] nf reply: level=%d opt=%d\n", level, optname);
	toy_ticks++;
	return 0;

pass:
	return ((long (*)(const struct pt_regs *))toy_orig_getsockopt)(regs);
}

/*
 * the controller list is a file the kernel builds at boot, so a kernel without
 * CONFIG_CGROUP_PIDS never lists pids in it. the read is answered with a file
 * of our own that does, which is all the check looks at
 */
static long toy_openat(const struct pt_regs *regs)
{
	static const char controllers[] = "cpuset cpu io memory pids\n";
	
	static const char proc_cgroups[] =
		"#subsys_name\thierarchy\tnum_cgroups\tenabled\n"
		"cpuset\t0\t1\t1\ncpu\t0\t1\t1\nio\t0\t1\t1\n"
		"memory\t0\t1\t1\npids\t0\t1\t1\n";
	const char *content = controllers;
	unsigned int clen;
	bool match = false;
	char path[96];
	struct file *f;
	loff_t pos = 0;
	int fd;

	static bool toy_seen_openat;

	if (!toy_seen_openat) {
		toy_seen_openat = true;
		pr_info("[droid_lkm_toy] reached: openat\n");
	}
	if (!toy_cgroups || !toy_kernel_write)
		goto pass;

	if (strncpy_from_user(path, (const char __user *)regs->regs[1],
			      sizeof(path)) >= 0) {
		
		if (!strcmp(path, "/sys/fs/cgroup/cgroup.controllers") &&
		    (regs->regs[2] & O_ACCMODE) == O_RDONLY)
			match = true;
		else if (!strcmp(path, "/proc/cgroups") &&
			 (regs->regs[2] & O_ACCMODE) == O_RDONLY) {
			match = true;
			content = proc_cgroups;
		}
	}
	if (match) {
		clen = (unsigned int)strlen(content);
		f = shmem_file_setup("toy-controllers", PAGE_SIZE, VM_NORESERVE);
		if (IS_ERR(f))
			return PTR_ERR(f);
		if (toy_kernel_write)
			toy_kernel_write(f, content, clen, &pos);
		else
			pr_warn("[droid_lkm_toy] kernel_write missing, pids tick degrades\n");
		fd = get_unused_fd_flags(O_RDONLY);
		if (fd < 0) {
			fput(f);
			return fd;
		}
		fd_install(fd, f);
		toy_ticks++;
		return fd;
	}
pass:
	return ((long (*)(const struct pt_regs *))toy_orig_openat)(regs);
}

static bool toy_slot_is(unsigned long nr, unsigned long want)
{
	unsigned long *table = (unsigned long *)kallsyms_name_to_addr("sys_call_table");
	unsigned long got = 0;

	if (!table)
		return false;
	if (copy_from_kernel_nofault(&got, &table[nr], sizeof(got)))
		return false;
	return got == want;
}

static int toy_restore_slot(const char *name, unsigned long nr, unsigned long orig)
{
	if (!orig)
		return 0;
	sc_unpatch(nr);
	
	if (toy_slot_is(nr, orig))
		return 0;
	pr_warn("[droid_lkm_toy] slot %s(%lu) still points into this module\n",
		name, nr);
	return 1;
}

static void toy_check_slot(const char *name, unsigned long nr, unsigned long want)
{
	unsigned long *table = (unsigned long *)kallsyms_name_to_addr("sys_call_table");
	unsigned long got = 0;

	if (!table)
		return;
	if (copy_from_kernel_nofault(&got, &table[nr], sizeof(got)))
		return;
	if (got == want)
		pr_info("[droid_lkm_toy] slot %s(%lu) armed\n", name, nr);
	else
		pr_warn("[droid_lkm_toy] slot %s(%lu) NOT armed: wrote %px read %px\n",
			name, nr, (void *)want, (void *)got);
}

static int __init toy_init(void)
{
	int ret = 0;

	find_kallsyms_base();

	strscpy(toy_sc_cfg.key, "droid_lkm_toy", sizeof(toy_sc_cfg.key));
	ret = sc_init(&toy_sc_cfg);
	
	if (ret && ret != -EALREADY) {
		pr_err("[droid_lkm_toy] sc_init failed: %d\n", ret);
		return ret;
	}
	if (sc_patch(TOY_NR_UNSHARE, (unsigned long)toy_unshare, &toy_orig_unshare))
		ret = -ENOENT;
	if (sc_patch(TOY_NR_FACCESSAT, (unsigned long)toy_faccessat_h,
		     &toy_orig_faccessat))
		ret = -ENOENT;
	if (sc_patch(TOY_NR_FACCESSAT2, (unsigned long)toy_faccessat2_h,
		     &toy_orig_faccessat2))
		ret = -ENOENT;
	if (sc_patch(TOY_NR_GETSOCKOPT, (unsigned long)toy_getsockopt,
		     &toy_orig_getsockopt))
		ret = -ENOENT;
	if (sc_patch(TOY_NR_OPENAT, (unsigned long)toy_openat, &toy_orig_openat))
		ret = -ENOENT;

	toy_check_slot("unshare", TOY_NR_UNSHARE, (unsigned long)toy_unshare);
	toy_check_slot("faccessat", TOY_NR_FACCESSAT, (unsigned long)toy_faccessat_h);
	toy_check_slot("faccessat2", TOY_NR_FACCESSAT2, (unsigned long)toy_faccessat2_h);
	toy_check_slot("getsockopt", TOY_NR_GETSOCKOPT, (unsigned long)toy_getsockopt);
	toy_check_slot("openat", TOY_NR_OPENAT, (unsigned long)toy_openat);

	/*
	 * kernel_write is a protected symbol on GKI, so it is resolved here and
	 * called through the pointer. without it the fake controller list stays
	 * empty and only that one tick is lost
	 */
	toy_kernel_write = (void *)kallsyms_name_to_addr("kernel_write");
	if (!toy_kernel_write)
		pr_warn("[droid_lkm_toy] kernel_write not resolved, pids tick degrades\n");

	/* EBUSY means the kernel already carries the real one, which is a tick too */
	if (register_filesystem(&toy_devtmpfs_type) &&
	    register_filesystem(&toy_devtmpfs_type) != -EBUSY)
		pr_warn("[droid_lkm_toy] devtmpfs name not registered\n");

	pr_info("[droid_lkm_toy] loaded, %s\n", ret ? "partly ticked" : "every tick is yours");
	return ret;
}

static void __exit toy_exit(void)
{
	int failed = 0;

	failed += toy_restore_slot("unshare", TOY_NR_UNSHARE, toy_orig_unshare);
	failed += toy_restore_slot("faccessat", TOY_NR_FACCESSAT,
				   toy_orig_faccessat);
	failed += toy_restore_slot("faccessat2", TOY_NR_FACCESSAT2,
				   toy_orig_faccessat2);
	failed += toy_restore_slot("getsockopt", TOY_NR_GETSOCKOPT,
				   toy_orig_getsockopt);
	failed += toy_restore_slot("openat", TOY_NR_OPENAT, toy_orig_openat);

	sc_exit();
	hk_exit();
	unregister_filesystem(&toy_devtmpfs_type);

	pr_info("[droid_lkm_toy] unloaded after %u tick%s, %d restore failure(s)\n",
		toy_ticks, toy_ticks == 1 ? "" : "s", failed);

	if (failed) {
		pr_err("[droid_lkm_toy] %d slot(s) still point into this module, refusing to unload\n",
		       failed);
		for (;;)
			msleep(1000);
	}
}

module_init(toy_init);
module_exit(toy_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("makes droidspaces check print every tick, for fun");

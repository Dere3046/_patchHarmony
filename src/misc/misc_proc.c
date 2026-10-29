// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 *
 * The control plane, and the plumbing the proc files this module adds share.
 *
 * Droidspaces has to be able to ask what is available instead of probing for
 * behaviour it might not get, and an operator has to be able to see which
 * feature was refused and on which symbol. /proc/droid_lkm_misc/status is that
 * answer: one line per feature, filled in by the init path that owns it, and
 * open for reading to everyone because it describes the kernel rather than any
 * one container.
 *
 * It is read only. The nodes that take a write, the cgroup limits in stage 4
 * above all, arrive with their enforcement and refuse a container task at the
 * point where the write would take effect, not here.
 */

#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "misc.h"
#include "misc_proc.h"

#define DROID_LKM_MISC_REPORT_SLOTS 24
#define DROID_LKM_MISC_REPORT_NAME 28
#define DROID_LKM_MISC_REPORT_STATE 14
#define DROID_LKM_MISC_REPORT_DETAIL 72

static struct {
	bool used;
	char name[DROID_LKM_MISC_REPORT_NAME];
	char state[DROID_LKM_MISC_REPORT_STATE];
	char detail[DROID_LKM_MISC_REPORT_DETAIL];
	void (*live)(char *buf, size_t size);
} droid_lkm_misc_report_slots[DROID_LKM_MISC_REPORT_SLOTS];

static DEFINE_MUTEX(droid_lkm_misc_report_lock);

/* the slot for this name, the first free one or the one already holding it. caller holds the lock */
static int droid_lkm_misc_report_claim(const char *name)
{
	unsigned int i;

	for (i = 0; i < DROID_LKM_MISC_REPORT_SLOTS; i++) {
		if (droid_lkm_misc_report_slots[i].used &&
		    strcmp(droid_lkm_misc_report_slots[i].name, name))
			continue;
		return i;
	}

	return -1;
}

static struct proc_dir_entry *droid_lkm_misc_dir;
static struct proc_dir_entry *(*droid_lkm_misc_proc_mkdir)(const char *name,
		struct proc_dir_entry *parent);
static struct proc_dir_entry *(*droid_lkm_misc_proc_create)(const char *name,
		umode_t mode, struct proc_dir_entry *parent,
		const struct proc_ops *ops);
static int (*droid_lkm_misc_proc_remove_subtree)(const char *name,
		struct proc_dir_entry *parent);

void droid_lkm_misc_report(const char *name, const char *state, const char *detail)
{
	int i;

	if (!name || !state)
		return;

	mutex_lock(&droid_lkm_misc_report_lock);

	i = droid_lkm_misc_report_claim(name);
	if (i < 0) {
		droid_lkm_misc_warn("report full, dropped: %s\n", name);
		mutex_unlock(&droid_lkm_misc_report_lock);
		return;
	}

	droid_lkm_misc_report_slots[i].used = true;
	droid_lkm_misc_report_slots[i].live = NULL;
	strscpy(droid_lkm_misc_report_slots[i].name, name,
		sizeof(droid_lkm_misc_report_slots[i].name));
	strscpy(droid_lkm_misc_report_slots[i].state, state,
		sizeof(droid_lkm_misc_report_slots[i].state));
	strscpy(droid_lkm_misc_report_slots[i].detail, detail ? detail : "",
		sizeof(droid_lkm_misc_report_slots[i].detail));

	mutex_unlock(&droid_lkm_misc_report_lock);
}

void droid_lkm_misc_report_live(const char *name, const char *state,
				void (*live)(char *buf, size_t size))
{
	int i;

	if (!name || !state || !live)
		return;

	mutex_lock(&droid_lkm_misc_report_lock);

	i = droid_lkm_misc_report_claim(name);
	if (i < 0) {
		droid_lkm_misc_warn("report full, dropped: %s\n", name);
		mutex_unlock(&droid_lkm_misc_report_lock);
		return;
	}

	droid_lkm_misc_report_slots[i].used = true;
	droid_lkm_misc_report_slots[i].live = live;
	strscpy(droid_lkm_misc_report_slots[i].name, name,
		sizeof(droid_lkm_misc_report_slots[i].name));
	strscpy(droid_lkm_misc_report_slots[i].state, state,
		sizeof(droid_lkm_misc_report_slots[i].state));
	droid_lkm_misc_report_slots[i].detail[0] = '\0';

	mutex_unlock(&droid_lkm_misc_report_lock);
}

unsigned int droid_lkm_misc_report_count(const char *state)
{
	unsigned int i, count = 0;

	mutex_lock(&droid_lkm_misc_report_lock);

	for (i = 0; i < DROID_LKM_MISC_REPORT_SLOTS; i++) {
		if (!droid_lkm_misc_report_slots[i].used)
			continue;
		if (!strcmp(droid_lkm_misc_report_slots[i].state, state))
			count++;
	}

	mutex_unlock(&droid_lkm_misc_report_lock);

	return count;
}

ssize_t droid_lkm_misc_proc_read_text(char __user *ubuf, size_t count,
				      loff_t *ppos, const char *text, int len)
{
	if (*ppos < 0)
		return -EINVAL;

	if (*ppos >= len)
		return 0;

	if (count > len - *ppos)
		count = len - *ppos;

	if (copy_to_user(ubuf + *ppos, text + *ppos, count))
		return -EFAULT;

	*ppos += count;

	return count;
}

loff_t droid_lkm_misc_proc_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t pos;

	switch (whence) {
	case SEEK_SET:
		pos = offset;
		break;
	case SEEK_CUR:
		pos = file->f_pos + offset;
		break;
	/* these files carry no size, so SEEK_END is the start plus offset */
	case SEEK_END:
		pos = offset;
		break;
	default:
		return -EINVAL;
	}

	if (pos < 0)
		return -EINVAL;

	file->f_pos = pos;

	return pos;
}

static int droid_lkm_misc_report_text(char *buf, int size)
{
	char detail[DROID_LKM_MISC_REPORT_DETAIL];
	int len = 0;
	unsigned int i;

	mutex_lock(&droid_lkm_misc_report_lock);

	for (i = 0; i < DROID_LKM_MISC_REPORT_SLOTS; i++) {
		const char *d;

		if (!droid_lkm_misc_report_slots[i].used)
			continue;
		if (len >= size - 1)
			break;

		/* a live detail is produced here rather than stored, for counts that move after init */
		if (droid_lkm_misc_report_slots[i].live) {
			detail[0] = '\0';
			droid_lkm_misc_report_slots[i].live(detail, sizeof(detail));
			d = detail;
		} else {
			d = droid_lkm_misc_report_slots[i].detail;
		}

		len += scnprintf(buf + len, size - len, "%-24s %-14s %s\n",
			droid_lkm_misc_report_slots[i].name,
			droid_lkm_misc_report_slots[i].state,
			d);
	}

	mutex_unlock(&droid_lkm_misc_report_lock);

	return len;
}

static ssize_t droid_lkm_misc_status_read(struct file *file, char __user *ubuf,
					  size_t count, loff_t *ppos)
{
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	ssize_t ret;

	if (!buf)
		return -ENOMEM;

	ret = droid_lkm_misc_proc_read_text(ubuf, count, ppos, buf,
					    droid_lkm_misc_report_text(buf, PAGE_SIZE));
	kfree(buf);

	return ret;
}

static ssize_t droid_lkm_misc_version_read(struct file *file, char __user *ubuf,
					   size_t count, loff_t *ppos)
{
	static const char version[] = __stringify(DROID_LKM_MISC_ABI) "\n";

	return droid_lkm_misc_proc_read_text(ubuf, count, ppos, version,
					     sizeof(version) - 1);
}

static const struct proc_ops droid_lkm_misc_status_ops = {
	.proc_read	= droid_lkm_misc_status_read,
	.proc_lseek	= droid_lkm_misc_proc_llseek,
};

static const struct proc_ops droid_lkm_misc_version_ops = {
	.proc_read	= droid_lkm_misc_version_read,
	.proc_lseek	= droid_lkm_misc_proc_llseek,
};

static int droid_lkm_misc_proc_resolve(void)
{
	struct {
		const char *name;
		void **slot;
	} syms[] = {
		{ "proc_mkdir",		(void **)&droid_lkm_misc_proc_mkdir },
		{ "proc_create",	(void **)&droid_lkm_misc_proc_create },
		{ "remove_proc_subtree", (void **)&droid_lkm_misc_proc_remove_subtree },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(syms); i++) {
		*syms[i].slot = (void *)droid_lkm_misc_sym(syms[i].name);
		if (!*syms[i].slot) {
			droid_lkm_misc_warn("control plane: %s not found\n",
					    syms[i].name);
			return -ENOENT;
		}
	}

	return 0;
}

int droid_lkm_misc_proc_init(void)
{
	int ret;

	droid_lkm_misc_report("module", "ready", "droid_lkm_misc abi "
			      __stringify(DROID_LKM_MISC_ABI));
	droid_lkm_misc_report("kallsyms", klnum_val ? "ready" : "unsupported",
			      klnum_val ? "resolved by name" : "recovery failed");

	ret = droid_lkm_misc_proc_resolve();
	if (ret) {
		droid_lkm_misc_report("control plane", "unsupported",
				      "procfs symbols not found");
		return ret;
	}

	droid_lkm_misc_dir = droid_lkm_misc_proc_mkdir("droid_lkm_misc", NULL);
	if (!droid_lkm_misc_dir) {
		droid_lkm_misc_report("control plane", "unsupported", "mkdir failed");
		return -ENOMEM;
	}

	if (!droid_lkm_misc_proc_create("status", 0444, droid_lkm_misc_dir,
					&droid_lkm_misc_status_ops) ||
	    !droid_lkm_misc_proc_create("version", 0444, droid_lkm_misc_dir,
					&droid_lkm_misc_version_ops)) {
		droid_lkm_misc_proc_remove_subtree("droid_lkm_misc", NULL);
		droid_lkm_misc_dir = NULL;
		droid_lkm_misc_report("control plane", "unsupported", "create failed");
		return -ENOMEM;
	}

	return 0;
}

void droid_lkm_misc_proc_exit(void)
{
	if (!droid_lkm_misc_dir)
		return;

	/* int, not void, KCFI hashes the return type too */
	droid_lkm_misc_proc_remove_subtree("droid_lkm_misc", NULL);
	droid_lkm_misc_dir = NULL;
}

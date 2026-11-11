// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef DROID_LKM_MISC_PROC_H
#define DROID_LKM_MISC_PROC_H

#include <linux/types.h>
#include <linux/fs.h>

/*
 * the interface version Droidspaces probes for, bump when a file under
 * /proc/droid_lkm_misc changes meaning
 */
#define DROID_LKM_MISC_ABI 1

/*
 * one line of the health surface by name, a later call replaces an earlier one, so a feature
 * that only becomes known after init can still say what it is. what was refused and why is
 * reported here too
 */
void droid_lkm_misc_report(const char *name, const char *state, const char *detail);

/*
 * the same line with a detail that is produced on every read instead of stored once, for a
 * count that moves after init. the callback runs under the report lock, so it must be a
 * short read of a value the caller already guards, and must not report itself
 */
void droid_lkm_misc_report_live(const char *name, const char *state,
				void (*live)(char *buf, size_t size));

/* how many features reported this state, for the install time coverage check */
unsigned int droid_lkm_misc_report_count(const char *state);

int droid_lkm_misc_proc_init(void);
void droid_lkm_misc_proc_exit(void);

/* the plumbing the proc files this module adds share */
ssize_t droid_lkm_misc_proc_read_text(char __user *ubuf, size_t count,
				      loff_t *ppos, const char *text, int len);
loff_t droid_lkm_misc_proc_llseek(struct file *file, loff_t offset, int whence);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/mm.h>
#include "ds.h"
#include "ds_ksym.h"
#include "compat/ds_ipc_compat.h"

/*
 * unmap while the caller already holds mmap_write_lock, so unlock stays false.
 * the locked entry point moved twice, the Makefile picks this unit by what the
 * target kernel actually carries.
 */
__nocfi noinline __nocfi noinline int droid_lkm_munmap_locked(struct mm_struct *mm, unsigned long start,
			    unsigned long end, struct list_head *uf)
{
	if (!droid_lkm_ks.__do_munmap)
		return -ENOSYS;
	return droid_lkm_ks.__do_munmap(mm, start, end - start, uf, false);
}

int droid_lkm_munmap_init(void)
{
	if (!droid_lkm_ks.__do_munmap) {
		droid_lkm_err("__do_munmap is missing, refusing to load\n");
		return -ENODEV;
	}
	return 0;
}

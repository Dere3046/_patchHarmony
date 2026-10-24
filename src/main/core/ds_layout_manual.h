// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */
#ifndef DROID_LKM_DS_LAYOUT_MANUAL_H
#define DROID_LKM_DS_LAYOUT_MANUAL_H

/* build time offsets for a target the anchors cannot recover */
/* field X(type, member, offset), type X(type, size) */
#define DROID_LKM_MANUAL_OFFSETS(X)					\
	/* X(task_struct, pid, 0x0) */

#define DROID_LKM_MANUAL_TYPES(X)					\
	/* X(pid_namespace, 0x0) */

#endif

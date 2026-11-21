// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DS_STATUS_H
#define DS_STATUS_H

int droid_lkm_status_init(void);
void droid_lkm_status_exit(void);
bool droid_lkm_status_ready(void);
bool droid_lkm_status_can_insert_pos(void);

#endif

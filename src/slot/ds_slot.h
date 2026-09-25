// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */


#ifndef DROID_LKM_SLOT_H
#define DROID_LKM_SLOT_H

int droid_lkm_slot_init(void);
void droid_lkm_slot_exit(void);

bool droid_lkm_slot_skip_sysvipc(void);
bool droid_lkm_slot_no_fake_ns(void);

#endif

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ported from kernel/user_namespace.c (android16-6.12): map_id_range_down_base,
 * map_id_range_down, map_id_up_base, map_id_up. The forward and reverse extent
 * arrays are not used here because a map written by this module never has more
 * than UID_GID_MAP_MAX_BASE_EXTENTS entries.
 */

#include "misc.h"
#include "userns.h"

#define DROID_LKM_UID_UNMAPPED ((u32)-1)

static u32 droid_lkm_map_id_range_down_base(unsigned int extents,
					    struct uid_gid_map *map,
					    u32 id, u32 count)
{
	unsigned int idx;
	u32 first, last, id2;

	id2 = id + count - 1;

	for (idx = 0; idx < extents; idx++) {
		first = map->extent[idx].first;
		last = first + map->extent[idx].count - 1;
		if (id >= first && id <= last && id2 >= first && id2 <= last)
			break;
	}

	if (idx < extents)
		id = (id - first) + map->extent[idx].lower_first;
	else
		id = DROID_LKM_UID_UNMAPPED;

	return id;
}

static u32 droid_lkm_map_id_range_down(struct uid_gid_map *map, u32 id, u32 count)
{
	unsigned int extents = map->nr_extents;

	smp_rmb();
	if (extents > UID_GID_MAP_MAX_BASE_EXTENTS)
		return DROID_LKM_UID_UNMAPPED;

	return droid_lkm_map_id_range_down_base(extents, map, id, count);
}

static u32 droid_lkm_map_id_up_base(unsigned int extents, struct uid_gid_map *map,
				    u32 id)
{
	unsigned int idx;
	u32 first, last;

	for (idx = 0; idx < extents; idx++) {
		first = map->extent[idx].lower_first;
		last = first + map->extent[idx].count - 1;
		if (id >= first && id <= last)
			return (id - first) + map->extent[idx].first;
	}

	return DROID_LKM_UID_UNMAPPED;
}

static u32 droid_lkm_map_id_up(struct uid_gid_map *map, u32 id)
{
	unsigned int extents = map->nr_extents;

	smp_rmb();
	if (extents > UID_GID_MAP_MAX_BASE_EXTENTS)
		return DROID_LKM_UID_UNMAPPED;

	return droid_lkm_map_id_up_base(extents, map, id);
}

u32 droid_lkm_userns_map_down(const struct droid_lkm_userns *u, u32 id)
{
	return droid_lkm_map_id_range_down((struct uid_gid_map *)&u->uns.uid_map, id, 1);
}

u32 droid_lkm_userns_map_up(const struct droid_lkm_userns *u, u32 id)
{
	return droid_lkm_map_id_up((struct uid_gid_map *)&u->uns.uid_map, id);
}

bool droid_lkm_userns_map_ok(const struct droid_lkm_userns *u, u32 id)
{
	return droid_lkm_userns_map_down(u, id) != DROID_LKM_UID_UNMAPPED;
}

/*
 * the identity map, what a runtime gets on a kernel with user namespaces when the container
 * root is the host root
 */
void droid_lkm_userns_map_identity(struct droid_lkm_userns *u)
{
	struct uid_gid_map *uid = &u->uns.uid_map;
	struct uid_gid_map *gid = &u->uns.gid_map;

	uid->extent[0].first = 0;
	uid->extent[0].lower_first = 0;
	uid->extent[0].count = 4294967295U;
	uid->nr_extents = 1;

	gid->extent[0].first = 0;
	gid->extent[0].lower_first = 0;
	gid->extent[0].count = 4294967295U;
	gid->nr_extents = 1;

	u->map_written = true;
}

/* layer one, the map functions are pure, the whole input domain runs without a task */
void droid_lkm_userns_map_selftest(void)
{
	struct droid_lkm_userns probe;
	unsigned long bad = 0;
	u32 id;

	memset(&probe, 0, sizeof(probe));

	/* empty map: nothing maps */
	for (id = 0; id < 65536; id++) {
		if (droid_lkm_userns_map_down(&probe, id) != DROID_LKM_UID_UNMAPPED)
			bad++;
	}
	if (droid_lkm_userns_map_down(&probe, 4294967295U) != DROID_LKM_UID_UNMAPPED)
		bad++;

	droid_lkm_userns_map_identity(&probe);

	/* identity map: round trip must hold over the whole domain */
	for (id = 0; id < 65536; id++) {
		if (!droid_lkm_userns_map_ok(&probe, id))
			bad++;
		if (droid_lkm_userns_map_down(&probe, id) != id ||
		    droid_lkm_userns_map_up(&probe, id) != id)
			bad++;
	}
	/* upper bound of the identity extent (4294967294U) must map cleanly */
	if (!droid_lkm_userns_map_ok(&probe, 4294967294U))
		bad++;
	if (droid_lkm_userns_map_down(&probe, 4294967294U) != 4294967294U ||
	    droid_lkm_userns_map_up(&probe, 4294967294U) != 4294967294U)
		bad++;

	/* 4294967295U ((u32)-1) is INVALID_UID and must remain unmapped */
	if (droid_lkm_userns_map_ok(&probe, 4294967295U))
		bad++;
	if (droid_lkm_userns_map_down(&probe, 4294967295U) != DROID_LKM_UID_UNMAPPED ||
	    droid_lkm_userns_map_up(&probe, 4294967295U) != DROID_LKM_UID_UNMAPPED)
		bad++;

	/* multi-extent map: container subuid layout (0->1000 count 1, 1->100000 count 65536) */
	memset(&probe, 0, sizeof(probe));
	probe.uns.uid_map.extent[0].first = 0;
	probe.uns.uid_map.extent[0].lower_first = 1000;
	probe.uns.uid_map.extent[0].count = 1;
	probe.uns.uid_map.extent[1].first = 1;
	probe.uns.uid_map.extent[1].lower_first = 100000;
	probe.uns.uid_map.extent[1].count = 65536;
	probe.uns.uid_map.nr_extents = 2;

	/* root mapping */
	if (droid_lkm_userns_map_down(&probe, 0) != 1000 ||
	    droid_lkm_userns_map_up(&probe, 1000) != 0)
		bad++;

	/* first subuid */
	if (droid_lkm_userns_map_down(&probe, 1) != 100000 ||
	    droid_lkm_userns_map_up(&probe, 100000) != 1)
		bad++;

	/* last subuid */
	if (droid_lkm_userns_map_down(&probe, 65536) != 165535 ||
	    droid_lkm_userns_map_up(&probe, 165535) != 65536)
		bad++;

	/* out of range */
	if (droid_lkm_userns_map_down(&probe, 65537) != DROID_LKM_UID_UNMAPPED ||
	    droid_lkm_userns_map_up(&probe, 999) != DROID_LKM_UID_UNMAPPED ||
	    droid_lkm_userns_map_up(&probe, 1001) != DROID_LKM_UID_UNMAPPED)
		bad++;

	if (bad)
		droid_lkm_misc_err("userns map selftest failed: %lu checks\n", bad);
	else
		droid_lkm_misc_info("userns map selftest passed\n");
}

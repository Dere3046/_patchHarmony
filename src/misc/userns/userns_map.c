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
	/*
	 * the identity map's single extent covers 0 .. 4294967295 - 1, so the top
	 * of the range is not in it, and 4294967295 happens to be both the id and
	 * the unmapped answer -- a round trip comparison cannot tell those two
	 * apart, it passes whichever way the map behaves. ask whether the id is
	 * covered instead, and round trip the last id the map does cover
	 */
	if (droid_lkm_userns_map_ok(&probe, 4294967295U))
		bad++;
	if (droid_lkm_userns_map_down(&probe, 4294967294U) != 4294967294U ||
	    droid_lkm_userns_map_up(&probe, 4294967294U) != 4294967294U)
		bad++;

	if (bad)
		droid_lkm_misc_err("userns map selftest failed: %lu checks\n", bad);
	else
		droid_lkm_misc_info("userns map selftest passed\n");
}

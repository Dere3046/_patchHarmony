// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ported from kernel/user_namespace.c (android16-6.12): map_id_range_down_base,
 * map_id_range_down, map_id_up_base, map_id_up. The forward and reverse extent
 * arrays are not used here because a map written by this module never has more
 * than UID_GID_MAP_MAX_BASE_EXTENTS entries.
 */

#include <linux/string.h>

#include "misc.h"
#include "userns.h"

#define DROID_LKM_UID_UNMAPPED DROID_LKM_USERNS_ID_UNMAPPED

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

static struct uid_gid_map *droid_lkm_userns_map(const struct droid_lkm_userns *u,
						bool is_uid)
{
	return (struct uid_gid_map *)(is_uid ? &u->uns.uid_map :
						 &u->uns.gid_map);
}

u32 droid_lkm_userns_map_down(const struct droid_lkm_userns *u, u32 id, bool is_uid)
{
	return droid_lkm_map_id_range_down(droid_lkm_userns_map(u, is_uid), id, 1);
}

u32 droid_lkm_userns_map_up(const struct droid_lkm_userns *u, u32 id, bool is_uid)
{
	return droid_lkm_map_id_up(droid_lkm_userns_map(u, is_uid), id);
}

/* a lower id as written in the parent's namespace, translated to the global space */
u32 droid_lkm_userns_map_parent_down(const struct droid_lkm_userns *u, u32 id,
				     u32 count, bool is_uid)
{
	struct user_namespace *parent = u->uns.parent ? u->uns.parent : droid_lkm_userns_init_uns;

	/*
	 * In kernels with CONFIG_USER_NS disabled, init_user_ns.uid_map.nr_extents is 0.
	 * In the initial user namespace, all IDs are already global 1:1.
	 */
	if (!parent || parent == droid_lkm_userns_init_uns || !droid_lkm_userns_is_ours(parent)) {
		if ((u64)id + count > 4294967295ULL)
			return DROID_LKM_USERNS_ID_UNMAPPED;
		return id;
	}

	return droid_lkm_map_id_range_down(is_uid ? &parent->uid_map : &parent->gid_map, id, count);
}

bool droid_lkm_userns_map_ok(const struct droid_lkm_userns *u, u32 id, bool is_uid)
{
	return droid_lkm_userns_map_down(u, id, is_uid) !=
		DROID_LKM_USERNS_ID_UNMAPPED;
}

/* kuid_has_mapping()/kgid_has_mapping(): a global id the namespace can name */
bool droid_lkm_userns_has_mapping(const struct droid_lkm_userns *u, u32 id,
				  bool is_uid)
{
	return droid_lkm_userns_map_up(u, id, is_uid) !=
		DROID_LKM_USERNS_ID_UNMAPPED;
}

/*
 * the identity map, what a privileged creator gets: container root is the host's root, which
 * is the point of a privileged container
 */
void droid_lkm_userns_map_identity(struct droid_lkm_userns *u)
{
	struct uid_gid_map *uid = &u->uns.uid_map;
	struct uid_gid_map *gid = &u->uns.gid_map;

	memset(uid, 0, sizeof(*uid));
	uid->extent[0].first = 0;
	uid->extent[0].lower_first = 0;
	uid->extent[0].count = 4294967295U;
	uid->nr_extents = 1;

	memset(gid, 0, sizeof(*gid));
	gid->extent[0].first = 0;
	gid->extent[0].lower_first = 0;
	gid->extent[0].count = 4294967295U;
	gid->nr_extents = 1;
}

/*
 * the confined map: container id 0 is the creator's own uid and gid, and nothing else is mapped.
 * container root keeps its meaning inside the namespace while the host's root (and every other
 * host id) stays outside it, so a DAC check over a host owned inode cannot be bypassed. a
 * creator whose own id is 0 is refused: mapping it would be the identity map by another name
 */
bool droid_lkm_userns_map_confine(struct droid_lkm_userns *u)
{
	struct uid_gid_map *uid = &u->uns.uid_map;
	struct uid_gid_map *gid = &u->uns.gid_map;
	u32 owner = __kuid_val(u->uns.owner);
	u32 group = __kgid_val(u->uns.group);

	if (!owner || !group)
		return false;

	memset(uid, 0, sizeof(*uid));
	uid->extent[0].first = 0;
	uid->extent[0].lower_first = owner;
	uid->extent[0].count = 1;
	uid->nr_extents = 1;

	memset(gid, 0, sizeof(*gid));
	gid->extent[0].first = 0;
	gid->extent[0].lower_first = group;
	gid->extent[0].count = 1;
	gid->nr_extents = 1;

	return true;
}

/* layer one, the map functions are pure, the whole input domain runs without a task */
void droid_lkm_userns_map_selftest(void)
{
	struct droid_lkm_userns probe;
	unsigned long bad = 0;
	bool is_uid;
	u32 id;
	int pass;

	memset(&probe, 0, sizeof(probe));

	/* empty map: nothing maps, in either direction, for either id space */
	for (pass = 0; pass < 2; pass++) {
		is_uid = pass == 0;

		for (id = 0; id < 65536; id++) {
			if (droid_lkm_userns_map_down(&probe, id, is_uid) !=
			    DROID_LKM_USERNS_ID_UNMAPPED)
				bad++;
			if (droid_lkm_userns_map_up(&probe, id, is_uid) !=
			    DROID_LKM_USERNS_ID_UNMAPPED)
				bad++;
		}
		if (droid_lkm_userns_map_down(&probe, 4294967295U, is_uid) !=
		    DROID_LKM_USERNS_ID_UNMAPPED)
			bad++;
	}

	droid_lkm_userns_map_identity(&probe);

	/* identity map: round trip must hold over the whole domain */
	for (pass = 0; pass < 2; pass++) {
		is_uid = pass == 0;

		for (id = 0; id < 65536; id++) {
			if (!droid_lkm_userns_map_ok(&probe, id, is_uid))
				bad++;
			if (!droid_lkm_userns_has_mapping(&probe, id, is_uid))
				bad++;
			if (droid_lkm_userns_map_down(&probe, id, is_uid) != id ||
			    droid_lkm_userns_map_up(&probe, id, is_uid) != id)
				bad++;
		}
		/* upper bound of the identity extent (4294967294U) must map cleanly */
		if (!droid_lkm_userns_map_ok(&probe, 4294967294U, is_uid) ||
		    droid_lkm_userns_map_down(&probe, 4294967294U, is_uid) != 4294967294U ||
		    droid_lkm_userns_map_up(&probe, 4294967294U, is_uid) != 4294967294U)
			bad++;

		/* 4294967295U ((u32)-1) is INVALID_UID and must remain unmapped */
		if (droid_lkm_userns_map_ok(&probe, 4294967295U, is_uid) ||
		    droid_lkm_userns_has_mapping(&probe, 4294967295U, is_uid) ||
		    droid_lkm_userns_map_down(&probe, 4294967295U, is_uid) !=
			DROID_LKM_USERNS_ID_UNMAPPED ||
		    droid_lkm_userns_map_up(&probe, 4294967295U, is_uid) !=
			DROID_LKM_USERNS_ID_UNMAPPED)
			bad++;
	}

	/*
	 * confined map: container root is the creator and nothing else. the host's
	 * root must stay outside the namespace, which is what keeps a DAC check
	 * over a host owned inode meaningful
	 */
	memset(&probe, 0, sizeof(probe));
	probe.uns.owner = KUIDT_INIT(1000);
	probe.uns.group = KGIDT_INIT(1000);
	if (!droid_lkm_userns_map_confine(&probe))
		bad++;
	for (pass = 0; pass < 2; pass++) {
		is_uid = pass == 0;

		if (!droid_lkm_userns_map_ok(&probe, 0, is_uid) ||
		    droid_lkm_userns_map_down(&probe, 0, is_uid) != 1000 ||
		    droid_lkm_userns_map_up(&probe, 1000, is_uid) != 0 ||
		    !droid_lkm_userns_has_mapping(&probe, 1000, is_uid))
			bad++;

		/* host root, another host uid and container id 1 are all outside */
		if (droid_lkm_userns_has_mapping(&probe, 0, is_uid) ||
		    droid_lkm_userns_has_mapping(&probe, 1001, is_uid) ||
		    droid_lkm_userns_map_ok(&probe, 1, is_uid))
			bad++;
	}

	/* a confined namespace refuses to be born mapping the host's root */
	probe.uns.owner = GLOBAL_ROOT_UID;
	if (droid_lkm_userns_map_confine(&probe))
		bad++;
	probe.uns.owner = KUIDT_INIT(1000);
	probe.uns.group = GLOBAL_ROOT_GID;
	if (droid_lkm_userns_map_confine(&probe))
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
	probe.uns.gid_map = probe.uns.uid_map;

	for (pass = 0; pass < 2; pass++) {
		is_uid = pass == 0;

		/* root mapping */
		if (droid_lkm_userns_map_down(&probe, 0, is_uid) != 1000 ||
		    droid_lkm_userns_map_up(&probe, 1000, is_uid) != 0)
			bad++;

		/* first subuid */
		if (droid_lkm_userns_map_down(&probe, 1, is_uid) != 100000 ||
		    droid_lkm_userns_map_up(&probe, 100000, is_uid) != 1)
			bad++;

		/* last subuid */
		if (droid_lkm_userns_map_down(&probe, 65536, is_uid) != 165535 ||
		    droid_lkm_userns_map_up(&probe, 165535, is_uid) != 65536)
			bad++;

		/* out of range */
		if (droid_lkm_userns_map_down(&probe, 65537, is_uid) !=
			DROID_LKM_USERNS_ID_UNMAPPED ||
		    droid_lkm_userns_map_up(&probe, 999, is_uid) !=
			DROID_LKM_USERNS_ID_UNMAPPED ||
		    droid_lkm_userns_map_up(&probe, 1001, is_uid) !=
			DROID_LKM_USERNS_ID_UNMAPPED)
			bad++;
	}

	if (bad)
		droid_lkm_misc_err("userns map selftest failed: %lu checks\n", bad);
	else
		droid_lkm_misc_info("userns map selftest passed\n");
}

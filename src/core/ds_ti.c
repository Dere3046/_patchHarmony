// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/ipc_namespace.h>
#include <linux/pid_namespace.h>
#include <linux/pid.h>
#include <linux/seq_file.h>

#include "ds.h"
#include "ds_ti.h"
#include "type_info.h"
#include "btf.h"

// build offset kept so a kernel that disagrees with the headers is visible
#define DROID_LKM_FIELD(_id, _type, _member)                                   \
	[_id] = { .type = #_type, .member = #_member,                          \
		  .build = offsetof(struct _type, _member) },

struct droid_lkm_field {
	const char *type;
	const char *member;
	u32 build;
	u32 live;
	u8 state;
};

enum {
	DROID_LKM_FIELD_OK,
	DROID_LKM_FIELD_NO_TYPE,
	DROID_LKM_FIELD_NO_MEMBER,
	DROID_LKM_FIELD_DIFFERS,
};

static struct droid_lkm_field droid_lkm_fields[DROID_LKM_FIELD_COUNT] = {
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_IDS, ipc_namespace, ids)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MNT, ipc_namespace, mq_mnt)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_USER_NS, ipc_namespace, user_ns)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_UCOUNTS, ipc_namespace, ucounts)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_NS, ipc_namespace, ns)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_IDR, pid_namespace, idr)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_LEVEL, pid_namespace, level)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_PARENT, pid_namespace, parent)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_PID_CACHEP, pid_namespace, pid_cachep)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_CHILD_REAPER, pid_namespace, child_reaper)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_USER_NS, pid_namespace, user_ns)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_UCOUNTS, pid_namespace, ucounts)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_PID_ALLOCATED, pid_namespace, pid_allocated)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NS_NS, pid_namespace, ns)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_IPC_NS, nsproxy, ipc_ns)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_PID_NS_FOR_CHILDREN, nsproxy, pid_ns_for_children)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_UTS_NS, nsproxy, uts_ns)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_MNT_NS, nsproxy, mnt_ns)
	DROID_LKM_FIELD(DROID_LKM_F_NSPROXY_NET_NS, nsproxy, net_ns)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_PID, task_struct, pid)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_TGID, task_struct, tgid)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_COMM, task_struct, comm)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_NSPROXY, task_struct, nsproxy)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_THREAD_PID, task_struct, thread_pid)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_GROUP_LEADER, task_struct, group_leader)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_REAL_PARENT, task_struct, real_parent)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_SIGNAL, task_struct, signal)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_SIGHAND, task_struct, sighand)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_MM, task_struct, mm)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_FLAGS, task_struct, flags)
	DROID_LKM_FIELD(DROID_LKM_F_TASK_SE, task_struct, se)
	DROID_LKM_FIELD(DROID_LKM_F_SEQ_FILE_BUF, seq_file, buf)
	DROID_LKM_FIELD(DROID_LKM_F_SEQ_FILE_COUNT, seq_file, count)
	DROID_LKM_FIELD(DROID_LKM_F_SEQ_FILE_SIZE, seq_file, size)
	DROID_LKM_FIELD(DROID_LKM_F_PID_LEVEL, pid, level)
	DROID_LKM_FIELD(DROID_LKM_F_PID_NUMBERS, pid, numbers)
	DROID_LKM_FIELD(DROID_LKM_F_UPID_NR, upid, nr)
	DROID_LKM_FIELD(DROID_LKM_F_UPID_NS, upid, ns)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_QUEUES_MAX, ipc_namespace, mq_queues_max)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MSG_MAX, ipc_namespace, mq_msg_max)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MSGSIZE_MAX, ipc_namespace, mq_msgsize_max)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MSG_DEFAULT, ipc_namespace, mq_msg_default)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_MSGSIZE_DEFAULT, ipc_namespace, mq_msgsize_default)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SEM_CTLS, ipc_namespace, sem_ctls)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MSG_CTLMAX, ipc_namespace, msg_ctlmax)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MSG_CTLMNB, ipc_namespace, msg_ctlmnb)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MSG_CTLMNI, ipc_namespace, msg_ctlmni)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SHM_CTLMAX, ipc_namespace, shm_ctlmax)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SHM_CTLALL, ipc_namespace, shm_ctlall)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SHM_CTLMNI, ipc_namespace, shm_ctlmni)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_SHM_RMID_FORCED, ipc_namespace, shm_rmid_forced)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_SET, ipc_namespace, mq_set)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_IPC_SET, ipc_namespace, ipc_set)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_MQ_SYSCTLS, ipc_namespace, mq_sysctls)
	DROID_LKM_FIELD(DROID_LKM_F_IPC_NS_IPC_SYSCTLS, ipc_namespace, ipc_sysctls)
#endif
};

#define DROID_LKM_TYPE(_id, _type)                                            	[_id] = { .name = #_type, .build = sizeof(struct _type) },

struct droid_lkm_type {
	const char *name;
	u32 build;
	u32 live;
	u8 state;
};

static struct droid_lkm_type droid_lkm_types[DROID_LKM_TYPE_COUNT] = {
	DROID_LKM_TYPE(DROID_LKM_T_UPID, upid)
};

static bool droid_lkm_layout_ready;

static unsigned long __nocfi droid_lkm_layout_resolve(const char *name)
{
	return droid_lkm_sym(name);
}

void droid_lkm_layout_init(void)
{
	struct ti_resolver res = { .name_to_addr = droid_lkm_layout_resolve };
	unsigned int ok = 0, differs = 0, missing = 0;
	unsigned int i;

	if (ti_init(&res)) {
		droid_lkm_warn("layout: type info refused to start, every offset stays compile time\n");
		return;
	}
	if (!ti_btf_available()) {
		// BTF only
		// anchor.o is not linked on purpose
		droid_lkm_warn("layout: the running kernel has no BTF, offsets stay compile time and features that need one must be refused by hand\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(droid_lkm_fields); i++) {
		struct droid_lkm_field *f = &droid_lkm_fields[i];
		u32 type_id, bit_off, bit_sz;

		if (ti_type_by_name(ti_base(), f->type, BIT(BTF_KIND_STRUCT),
				    &type_id)) {
			f->state = DROID_LKM_FIELD_NO_TYPE;
			missing++;
			continue;
		}
		if (ti_member_off(ti_base(), type_id, f->member, &bit_off,
				  &bit_sz)) {
			f->state = DROID_LKM_FIELD_NO_MEMBER;
			missing++;
			continue;
		}

		f->live = bit_off / 8;
		if (f->live != f->build) {
			f->state = DROID_LKM_FIELD_DIFFERS;
			differs++;
			continue;
		}
		f->state = DROID_LKM_FIELD_OK;
		ok++;
	}

	for (i = 0; i < ARRAY_SIZE(droid_lkm_types); i++) {
		struct droid_lkm_type *t = &droid_lkm_types[i];
		u32 type_id;

		if (ti_type_by_name(ti_base(), t->name, BIT(BTF_KIND_STRUCT),
				    &type_id)) {
			t->state = DROID_LKM_FIELD_NO_TYPE;
			missing++;
			continue;
		}
		t->live = ti_type_size(ti_base(), type_id);
		if (!t->live || t->live != t->build) {
			t->state = DROID_LKM_FIELD_DIFFERS;
			differs++;
			continue;
		}
		t->state = DROID_LKM_FIELD_OK;
	}

	droid_lkm_layout_ready = !differs && !missing;
	droid_lkm_info("layout: BTF %u/%zu fields as built, %u differ, %u not in BTF\n",
		       ok, ARRAY_SIZE(droid_lkm_fields), differs, missing);

	for (i = 0; i < ARRAY_SIZE(droid_lkm_fields); i++) {
		struct droid_lkm_field *f = &droid_lkm_fields[i];

		if (f->state == DROID_LKM_FIELD_DIFFERS)
			droid_lkm_warn("%s.%s is at %u in the running kernel, this build uses %u\n",
				       f->type, f->member, f->live, f->build);
		else if (f->state != DROID_LKM_FIELD_OK)
			droid_lkm_dbg("%s.%s is not in the running kernel's BTF\n",
				      f->type, f->member);
	}
}

u32 droid_lkm_layout_off(enum droid_lkm_field_id id)
{
	if (id >= DROID_LKM_FIELD_COUNT)
		return 0;
	return droid_lkm_fields[id].live;
}

bool droid_lkm_layout_ok(enum droid_lkm_field_id id)
{
	if (id >= DROID_LKM_FIELD_COUNT)
		return false;
	return droid_lkm_fields[id].state == DROID_LKM_FIELD_OK;
}

void *droid_lkm_layout_ptr(void *base, enum droid_lkm_field_id id)
{
	if (!base || !droid_lkm_layout_ok(id))
		return NULL;
	return (char *)base + droid_lkm_layout_off(id);
}

u32 droid_lkm_layout_type_size(enum droid_lkm_type_id id)
{
	if (id >= DROID_LKM_TYPE_COUNT)
		return 0;
	return droid_lkm_types[id].live;
}

bool droid_lkm_layout_type_ok(enum droid_lkm_type_id id)
{
	if (id >= DROID_LKM_TYPE_COUNT)
		return false;
	return droid_lkm_types[id].state == DROID_LKM_FIELD_OK;
}

void droid_lkm_layout_report(void)
{
	unsigned int ok = 0, i;

	for (i = 0; i < ARRAY_SIZE(droid_lkm_fields); i++) {
		if (droid_lkm_fields[i].state == DROID_LKM_FIELD_OK)
			ok++;
	}

	for (i = 0; i < ARRAY_SIZE(droid_lkm_types); i++) {
		if (droid_lkm_types[i].state == DROID_LKM_FIELD_DIFFERS)
			droid_lkm_warn("sizeof(struct %s) is %u in the running kernel, this build uses %u\n",
				       droid_lkm_types[i].name,
				       droid_lkm_types[i].live,
				       droid_lkm_types[i].build);
	}

	droid_lkm_info("layout: %u/%zu offsets from BTF%s\n", ok,
		       ARRAY_SIZE(droid_lkm_fields),
		       droid_lkm_layout_ready ? "" : ", some fields refused");
}

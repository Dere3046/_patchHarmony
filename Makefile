obj-m := droid_lkm.o

KDIR := $(KDIR)
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
# a missing VER must not point at out/ itself
# make clean would wipe every target
ODIR := $(MDIR)/out/$(if $(VER),$(VER),unknown)

DEPS := Type_info KernCall HooKern vendorcompat
include $(MDIR)/mk/deps.mk

# both ko's link the whole type info reader now
# the exports that used to collide are gated upstream (CONFIG_TI_MODNAME)

# optional HooKern features we do not use
DEPS_OBJS_ALL := $(filter-out deps/HooKern/lib/hk_binder.o deps/HooKern/lib/hk_lsm.o,$(DEPS_OBJS_ALL))

# second ko in the same Kbuild: vendor compatibility fixups
#   ghost task fallback for find_task_by_vpid
#   oppo ddl history table
# the vendor module supports 6.6 and 6.12 only; the main module is the one
# being ported to the other branches
VENDOR_VER := $(shell sed -n 's/.*UTS_RELEASE "\([0-9]*\.[0-9]*\).*/\1/p' \
	$(KDIR)/include/generated/utsrelease.h 2>/dev/null)
DLKM_OPT := $(strip $(if $(filter 6.6 6.12,$(VENDOR_VER)),\
	$(shell grep -q ' nop_mnt_idmap$$' $(KDIR)/System.map 2>/dev/null && echo m),))
obj-$(DLKM_OPT) += droid_lkm_vendor.o
DLC_VENDOR_OBJS := src/compat/vendor/compat_main.o src/compat/vendor/ghost.o \
	src/compat/vendor/ddl_hit.o src/compat/vendor/vendor_syms.o \
	src/compat/ds_compat.o src/compat/ds_layout_anchor.o \
	src/hook/ds_hook.o
droid_lkm_vendor-y := $(DLC_VENDOR_OBJS) $(DEPS_OBJS_ALL)

# droid_lkm_misc.o is not built yet, it is unfinished; re-enable it with the
# line below once its sources are ready
# obj-$(DLKM_OPT) += droid_lkm_misc.o
DLM_OBJS := src/misc/misc_main.o \
	src/misc/misc_ksym.o \
	src/misc/misc_scope.o \
	src/misc/misc_proc.o \
	src/misc/xt/xt_reg.o \
	src/misc/xt/xt_addrtype.o \
	src/misc/xt/xt_AUDIT.o \
	src/misc/xt/xt_CHECKSUM.o \
	src/misc/xt/xt_CLASSIFY.o \
	src/misc/xt/xt_cluster.o \
	src/misc/xt/xt_cgroup.o \
	src/misc/xt/xt_comment.o \
	src/misc/xt/xt_connlabel.o \
	src/misc/xt/xt_connlimit.o \
	src/misc/xt/xt_conntrack.o \
	src/misc/xt/xt_cpu.o \
	src/misc/xt/xt_dccp.o \
	src/misc/xt/xt_devgroup.o \
	src/misc/xt/xt_dscp.o \
	src/misc/xt/xt_DSCP.o \
	src/misc/xt/xt_ecn.o \
	src/misc/xt/xt_esp.o \
	src/misc/xt/xt_hl.o \
	src/misc/xt/xt_HL.o \
	src/misc/xt/xt_HMARK.o \
	src/misc/xt/xt_ipcomp.o \
	src/misc/xt/xt_iprange.o \
	src/misc/xt/xt_l2tp.o \
	src/misc/xt/xt_LED.o \
	src/misc/xt/xt_length.o \
	src/misc/xt/xt_limit.o \
	src/misc/xt/xt_LOG.o \
	src/misc/xt/xt_mac.o \
	src/misc/xt/xt_mark.o \
	src/misc/xt/xt_MASQUERADE.o \
	src/misc/xt/xt_multiport.o \
	src/misc/xt/xt_nat.o \
	src/misc/xt/xt_NETMAP.o \
	src/misc/xt/xt_NFLOG.o \
	src/misc/xt/xt_NFQUEUE.o \
	src/misc/xt/xt_owner.o \
	src/misc/xt/xt_pkttype.o \
	src/misc/xt/xt_policy.o \
	src/misc/xt/xt_quota.o \
	src/misc/xt/xt_RATEEST.o \
	src/misc/xt/xt_rateest.o \
	src/misc/xt/xt_realm.o \
	src/misc/xt/xt_sctp.o \
	src/misc/xt/xt_SECMARK.o \
	src/misc/xt/xt_socket.o \
	src/misc/xt/xt_state.o \
	src/misc/xt/xt_statistic.o \
	src/misc/xt/xt_string.o \
	src/misc/xt/xt_tcpmss.o \
	src/misc/xt/xt_TCPMSS.o \
	src/misc/xt/xt_TCPOPTSTRIP.o \
	src/misc/xt/xt_tcpudp.o \
	src/misc/xt/xt_TEE.o \
	src/misc/xt/xt_time.o \
	src/misc/xt/xt_TPROXY.o \
	src/misc/xt/xt_TRACE.o \
	src/misc/xt/xt_u32.o \
	src/misc/devtmpfs/devtmpfs.o \
	src/misc/userns/userns_core.o \
	src/misc/userns/userns_map.o \
	src/misc/userns/userns_cred.o \
	src/misc/userns/userns_nsops.o \
	src/misc/userns/userns_owner.o \
	src/misc/userns/userns_ids.o \
	src/misc/userns/userns_proc.o \
	src/misc/userns/userns_selftest.o \
	src/misc/userns/userns_captrace.o \
	src/misc/userns/userns_hooks.o
droid_lkm_misc-y := $(DLM_OBJS) $(DEPS_OBJS_ALL)

# the locked unmap entry moved twice, pick the unit by what the target kernel
# carries rather than by a version number
MUNMAP_UNIT := $(shell if grep -qE ' [tT] do_vmi_munmap$$' $(KDIR)/System.map 2>/dev/null; then echo src/main/ipcns/munmap_vmi.o; \
	elif grep -qE ' [tT] do_mas_munmap$$' $(KDIR)/System.map 2>/dev/null; then echo src/main/ipcns/munmap_mas.o; \
	else echo src/main/ipcns/munmap_legacy.o; fi)

droid_lkm-y := src/main/core/main.o src/main/core/ds_features.o \
	src/hook/ds_hook.o src/compat/ds_compat.o \
	src/compat/ds_layout_anchor.o \
	$(MUNMAP_UNIT) \
	src/main/core/ds_ksym.o \
	src/main/core/ds_caps.o \
	src/main/core/ds_ti.o \
	src/main/slot/ds_slot.o \
	src/main/pidns/pidns.o \
	src/main/pidns/ds_nsops.o \
	src/main/pidns/ds_proc.o \
	src/main/pidns/ds_status.o \
	src/main/ipcns/ipcns.o \
	src/main/ipcns/ksym_shim.o \
	src/main/ipcns/ipc_shim.o \
	src/main/ipcns/ipc_util.o \
	src/main/ipcns/ipc_syscall.o \
	src/main/ipcns/sysv/ipc_msg.o \
	src/main/ipcns/sysv/ipc_sem.o \
	src/main/ipcns/sysv/ipc_shm.o \
	src/main/ipcns/sysv/ipc_msgutil.o \
	src/main/ipcns/mqueue/ipc_mqueue.o \
	src/main/ipcns/mqueue/ipc_mqueue_shim.o \
	src/main/ipcns/mqueue/ipc_mq_sysctl.o \
	src/main/ipcns/sysctl/ipc_sysctl.o \
	src/main/ipcns/compat/ipc_compat32.o \
	$(DEPS_OBJS_ALL)

ccflags-y += -std=gnu11
ccflags-y += -Wno-declaration-after-statement
# dead static hk_build_jump in HooKern
ccflags-y += -Wno-unused-function
ccflags-y += -Wno-strict-prototypes
ccflags-y += -I$(src)/src -I$(src)/src/compat/vendor
ccflags-y += -I$(src)/src/hook -I$(src)/src/compat
ccflags-y += -I$(src)/src/main/core -I$(src)/src/main/slot -I$(src)/src/main/pidns
ccflags-y += -I$(src)/src/main/ipcns -I$(src)/src/main/ipcns/sysv
ccflags-y += -I$(src)/src/main/ipcns/mqueue -I$(src)/src/main/ipcns/sysctl
ccflags-y += -I$(src)/src/main/ipcns/compat
ccflags-y += -I$(src)/src/misc -I$(src)/src/misc/xt -I$(src)/src/misc/userns
ccflags-y += -I$(src)/src/misc/devtmpfs
ccflags-y += $(addprefix -I$(src)/,$(DEPS_INCS_ALL))

# 6.18 turns on -Wmissing-prototypes, and this module both defines kernel
# names the newer kernel stops declaring and exposes entry points through
# headers the shim files do not include; older compilers ignore the flag
ccflags-y += -Wno-missing-prototypes

# KernCall: enable sys_call_table slot patching
# off by default. enable with make DROID_LKM_LAYOUT_FALLBACK=1
ifeq ($(DROID_LKM_LAYOUT_FALLBACK),1)
ccflags-y += -DDROID_LKM_LAYOUT_FALLBACK
endif
ifeq ($(DROID_LKM_LAYOUT_FORCE_BUILD),1)
ccflags-y += -DDROID_LKM_LAYOUT_FORCE_BUILD
endif
ifeq ($(DROID_LKM_LAYOUT_MANUAL),1)
ccflags-y += -DDROID_LKM_LAYOUT_MANUAL
endif
ifneq ($(DROID_LKM_LAYOUT_TEST_BUILD_SHIFT),)
ccflags-y += -DDROID_LKM_LAYOUT_TEST_BUILD_SHIFT=$(DROID_LKM_LAYOUT_TEST_BUILD_SHIFT)
endif
ccflags-y += -DCONFIG_KERNSC_PATCH -DCONFIG_KERNSC_DISCOVER

all:
	@mkdir -p $(sort $(dir $(addprefix $(ODIR)/,$(droid_lkm-y) $(droid_lkm_vendor-y) $(droid_lkm_misc-y))))
	# 6.18 kbuild derives src from srcroot, older kernels ignore it
	make -C $(KDIR) M=$(ODIR) src=$(MDIR) srcroot=$(MDIR) modules
clean:
	make -C $(KDIR) M=$(ODIR) src=$(MDIR) srcroot=$(MDIR) clean

$(obj)/%.o: $(src)/%.c $(recordmcount_source) FORCE
	$(call if_changed_rule,cc_o_c)
	$(call cmd,force_checksrc)

$(obj)/%.o: $(src)/%.S FORCE
	$(call if_changed_rule,as_o_S)

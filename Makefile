obj-m := droid_lkm.o

KDIR := $(KDIR)
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
# a missing VER must not point at out/ itself
# make clean would wipe every target
ODIR := $(MDIR)/out/$(if $(VER),$(VER),unknown)

DEPS := Type_info KernCall HooKern
include $(MDIR)/mk/deps.mk

# both ko's link the whole type info reader now
# the exports that used to collide are gated upstream (CONFIG_TI_MODNAME)

# optional HooKern features we do not use
DEPS_OBJS_ALL := $(filter-out deps/HooKern/lib/hk_binder.o deps/HooKern/lib/hk_lsm.o,$(DEPS_OBJS_ALL))

# second ko in the same Kbuild: device quirk fixups, independent of droid_lkm
#   ghost task fallback for find_task_by_vpid
#   selftest probe, TEST=1 builds only
# the two optional modules are built only for the targets they compile on.
# the main module is the one being ported, so probe the target kernel for a
# feature that only the newer GKI branches carry instead of naming a version.
DLKM_OPT := $(shell grep -q ' nop_mnt_idmap$$' $(KDIR)/System.map 2>/dev/null && echo m)
obj-$(DLKM_OPT) += droid_lkm_compat.o
DLC_COMPAT_OBJS := compat/compat_main.o compat/ghost.o
ifeq ($(TEST),1)
ccflags-y += -DCONFIG_DROID_LKM_SELFTEST
DLC_COMPAT_OBJS += compat/selftest.o
endif
droid_lkm_compat-y := $(DLC_COMPAT_OBJS) $(DEPS_OBJS_ALL)

# third ko in the same Kbuild: kernel features the device lacks, container scoped
#   xt matches and targets through the exported registration API
#   cgroup pids and device effect, devtmpfs, optional link types
obj-m += droid_lkm_toy.o
droid_lkm_toy-y := toy/toy_main.o $(DEPS_OBJS_ALL)

# fourth ko in the same Kbuild: the container scoped features the stock GKI was
# built without -- devtmpfs and a user namespace. gated on the header devtmpfs'
# vfs calls are written against, not on a version, and deliberately not on
# nop_mnt_idmap the way droid_lkm_compat is: that symbol is exactly the one
# userns path that degrades without it, so it can not be the gate for the
# module that ships the rest of userns. android13-5.10 predates idmapped mounts
# and gets no droid_lkm_misc.ko, everything from 5.12 up builds.
DLM_OPT := $(shell test -f $(KDIR)/include/linux/mnt_idmapping.h 2>/dev/null && echo m)
obj-$(DLM_OPT) += droid_lkm_misc.o
DLM_OBJS := src/misc/misc_main.o \
	src/misc/misc_ksym.o \
	src/misc/misc_scope.o \
	src/misc/misc_proc.o \
	src/misc/xt/xt_reg.o \
	src/misc/xt/xt_addrtype.o \
	src/misc/devtmpfs/devtmpfs.o \
	src/misc/userns/userns_core.o \
	src/misc/userns/userns_map.o \
	src/misc/userns/userns_cred.o \
	src/misc/userns/userns_nsops.o \
	src/misc/userns/userns_owner.o \
	src/misc/userns/userns_proc.o \
	src/misc/userns/userns_selftest.o \
	src/misc/userns/userns_captrace.o \
	src/misc/userns/userns_hooks.o
droid_lkm_misc-y := $(DLM_OBJS) $(DEPS_OBJS_ALL)

# the locked unmap entry moved twice, pick the unit by what the target kernel
# carries rather than by a version number
MUNMAP_UNIT := $(shell if grep -qE ' [tT] do_vmi_munmap$$' $(KDIR)/System.map 2>/dev/null; then echo src/ipcns/munmap_vmi.o; \
	elif grep -qE ' [tT] do_mas_munmap$$' $(KDIR)/System.map 2>/dev/null; then echo src/ipcns/munmap_mas.o; \
	else echo src/ipcns/munmap_legacy.o; fi)

droid_lkm-y := src/core/main.o src/core/ds_hook.o \
	$(MUNMAP_UNIT) \
	src/core/ds_ksym.o \
	src/core/ds_caps.o \
	src/core/ds_ti.o \
	src/slot/ds_slot.o \
	src/pidns/pidns.o \
	src/pidns/ds_nsops.o \
	src/pidns/ds_proc.o \
	src/pidns/ds_status.o \
	src/ipcns/ipcns.o \
	src/ipcns/ksym_shim.o \
	src/ipcns/ipc_shim.o \
	src/ipcns/ipc_util.o \
	src/ipcns/ipc_syscall.o \
	src/ipcns/sysv/ipc_msg.o \
	src/ipcns/sysv/ipc_sem.o \
	src/ipcns/sysv/ipc_shm.o \
	src/ipcns/sysv/ipc_msgutil.o \
	src/ipcns/mqueue/ipc_mqueue.o \
	src/ipcns/mqueue/ipc_mqueue_shim.o \
	src/ipcns/mqueue/ipc_mq_sysctl.o \
	src/ipcns/sysctl/ipc_sysctl.o \
	src/ipcns/compat/ipc_compat32.o \
	$(DEPS_OBJS_ALL)

ccflags-y += -std=gnu11
ccflags-y += -Wno-declaration-after-statement
# dead static hk_build_jump in HooKern
ccflags-y += -Wno-unused-function
ccflags-y += -Wno-strict-prototypes
ccflags-y += -I$(src)/src -I$(src)/compat
ccflags-y += -I$(src)/src/core -I$(src)/src/slot -I$(src)/src/pidns
ccflags-y += -I$(src)/src/ipcns -I$(src)/src/ipcns/sysv
ccflags-y += -I$(src)/src/ipcns/mqueue -I$(src)/src/ipcns/sysctl
ccflags-y += -I$(src)/src/ipcns/compat
ccflags-y += -I$(src)/src/misc -I$(src)/src/misc/xt -I$(src)/src/misc/userns
ccflags-y += -I$(src)/src/misc/devtmpfs
ccflags-y += $(addprefix -I$(src)/,$(DEPS_INCS_ALL))

# 6.18 turns on -Wmissing-prototypes, and this module both defines kernel
# names the newer kernel stops declaring and exposes entry points through
# headers the shim files do not include; older compilers ignore the flag
ccflags-y += -Wno-missing-prototypes

# KernCall: enable sys_call_table slot patching
ccflags-y += -DCONFIG_KERNSC_PATCH -DCONFIG_KERNSC_DISCOVER

all:
	@mkdir -p $(sort $(dir $(addprefix $(ODIR)/,$(droid_lkm-y) $(droid_lkm_compat-y) $(droid_lkm_misc-y) $(droid_lkm_toy-y))))
	# 6.18 kbuild derives src from srcroot, older kernels ignore it
	make -C $(KDIR) M=$(ODIR) src=$(MDIR) srcroot=$(MDIR) modules
clean:
	make -C $(KDIR) M=$(ODIR) src=$(MDIR) srcroot=$(MDIR) clean

$(obj)/%.o: $(src)/%.c $(recordmcount_source) FORCE
	$(call if_changed_rule,cc_o_c)
	$(call cmd,force_checksrc)

$(obj)/%.o: $(src)/%.S FORCE
	$(call if_changed_rule,as_o_S)

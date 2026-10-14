obj-m := droid_lkm.o

KDIR := $(KDIR)
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
# a missing VER must not point at out/ itself
# make clean would wipe every target
ODIR := $(MDIR)/out/$(if $(VER),$(VER),unknown)

DEPS := Type_info KernCall HooKern
include $(MDIR)/mk/deps.mk

# the ko links the whole type info reader now
# the exports that used to collide are gated upstream (CONFIG_TI_MODNAME)

# optional HooKern features we do not use
DEPS_OBJS_ALL := $(filter-out deps/HooKern/lib/hk_binder.o deps/HooKern/lib/hk_lsm.o,$(DEPS_OBJS_ALL))

# One ko. It used to be four: the main module, droid_lkm_compat.ko with the OPPO
# ghost fixup, droid_lkm_misc.ko with devtmpfs and the fake user namespace, and
# the toy. Each of the first three linked its own copy of the deps and called
# hk_init() on its own, so three hook engines kept three patch ledgers over one
# kernel text, and shipping meant telling people which ko their kernel wanted.
# The two halves are phases of this module now, compiled in where they build and
# stubbed out where they do not.
#
#   compat: the OPPO ghost fixup. built where the target carries the symbol the
#   branch split shows up in, the same probe the tree used when it was its own ko
#   misc: devtmpfs and the fake user namespace. needs the 5.12 idmapped mount
#   signatures, so it is gated on the header devtmpfs' vfs calls are written
#   against rather than on a version, and deliberately not on nop_mnt_idmap:
#   that symbol is exactly the one userns path that degrades without it, which
#   makes it the wrong gate for the half that ships the rest of userns.
#   android13-5.10 predates idmapped mounts and goes without it, 5.12 up builds.
#   $(KDIR) is a shim: include/ there holds config and generated only and the
#   real headers live in the tree it points at through source/, so probe both and
#   the gate does not depend on which layout the ddk image shipped.
DLKM_OPT := $(shell grep -q ' nop_mnt_idmap$$' $(KDIR)/System.map 2>/dev/null && echo m)
ifeq ($(DLKM_OPT),m)
ccflags-y += -DCONFIG_DROID_LKM_COMPAT
DLC_COMPAT_OBJS := compat/compat_main.o compat/ghost.o
# nested, not a second top level ifeq: the probe reads a self test that calls
# into ghost, so linking it without the rest of compat is a link error
ifeq ($(TEST),1)
ccflags-y += -DCONFIG_DROID_LKM_SELFTEST
DLC_COMPAT_OBJS += compat/selftest.o
endif
endif

DLM_HDR := $(firstword $(wildcard $(KDIR)/source/include/linux/mnt_idmapping.h \
	$(KDIR)/include/linux/mnt_idmapping.h))
DLM_OPT := $(if $(DLM_HDR),m)
ifeq ($(DLM_OPT),m)
ccflags-y += -DCONFIG_DROID_LKM_MISC
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
endif

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
	$(DLC_COMPAT_OBJS) \
	$(DLM_OBJS) \
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
	@mkdir -p $(sort $(dir $(addprefix $(ODIR)/,$(droid_lkm-y))))
	# 6.18 kbuild derives src from srcroot, older kernels ignore it
	make -C $(KDIR) M=$(ODIR) src=$(MDIR) srcroot=$(MDIR) modules
clean:
	make -C $(KDIR) M=$(ODIR) src=$(MDIR) srcroot=$(MDIR) clean

$(obj)/%.o: $(src)/%.c $(recordmcount_source) FORCE
	$(call if_changed_rule,cc_o_c)
	$(call cmd,force_checksrc)

$(obj)/%.o: $(src)/%.S FORCE
	$(call if_changed_rule,as_o_S)

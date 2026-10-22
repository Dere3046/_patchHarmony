obj-m := droid_lkm.o

KDIR := $(KDIR)
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
# a missing VER must not point at out/ itself
# make clean would wipe every target
ODIR := $(MDIR)/out/$(if $(VER),$(VER),unknown)

# KallRecon: enable parallel raana discovery engine and aggressive search strategy
KALLRECON_RAANA := 1

DEPS := Type_info KernCall HooKern vendorcompat
include $(MDIR)/mk/deps.mk

# both ko's link the whole type info reader now
# the exports that used to collide are gated upstream (CONFIG_TI_MODNAME)

# optional HooKern features we do not use
DEPS_OBJS_ALL := $(filter-out deps/HooKern/lib/hk_binder.o deps/HooKern/lib/hk_lsm.o,$(DEPS_OBJS_ALL))

# second ko in the same Kbuild: vendor compatibility fixups
#   ghost task fallback for find_task_by_vpid
#   oppo ddl history table
# the module is built only for the targets it compiles on. the main module is
# the one being ported, so probe the target kernel for a feature that only the
# newer GKI branches carry instead of naming a version.
DLKM_OPT := $(shell grep -qE ' (do_vmi_munmap|do_mas_munmap)$$' $(KDIR)/System.map 2>/dev/null && echo m)
obj-$(DLKM_OPT) += droid_lkm_vendor.o
DLC_VENDOR_OBJS := src/compat/vendor/compat_main.o src/compat/vendor/ghost.o \
	src/compat/vendor/ddl_hit.o src/compat/vendor/vendor_syms.o \
	src/compat/ds_compat.o src/hook/ds_hook.o
droid_lkm_vendor-y := $(DLC_VENDOR_OBJS) $(DEPS_OBJS_ALL)

# third ko in the same Kbuild: container scoped kernel features missing on stock GKI
# (devtmpfs, fake userns, xt_addrtype)
obj-$(DLKM_OPT) += droid_lkm_misc.o
DLM_OBJS := src/misc/misc_main.o \
	src/misc/misc_ksym.o \
	src/misc/misc_scope.o \
	src/misc/misc_proc.o \
	src/misc/xt/xt_reg.o \
	src/misc/xt/xt_addrtype.o \
	src/misc/devtmpfs/devtmpfs.o \
	src/misc/pts_shield/pts_shield.o \
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
MUNMAP_UNIT := $(shell if grep -qE ' [tT] do_vmi_munmap$$' $(KDIR)/System.map 2>/dev/null; then echo src/main/ipcns/munmap_vmi.o; \
	elif grep -qE ' [tT] do_mas_munmap$$' $(KDIR)/System.map 2>/dev/null; then echo src/main/ipcns/munmap_mas.o; \
	else echo src/main/ipcns/munmap_legacy.o; fi)

droid_lkm-y := src/main/core/main.o src/main/core/ds_features.o \
	src/hook/ds_hook.o src/compat/ds_compat.o \
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
ccflags-y += -Wno-unknown-sanitizers
ccflags-y += -I$(src)/src -I$(src)/src/compat/vendor
ccflags-y += -I$(src)/src/hook -I$(src)/src/compat
ccflags-y += -I$(src)/src/main/core -I$(src)/src/main/slot -I$(src)/src/main/pidns
ccflags-y += -I$(src)/src/main/ipcns -I$(src)/src/main/ipcns/sysv
ccflags-y += -I$(src)/src/main/ipcns/mqueue -I$(src)/src/main/ipcns/sysctl
ccflags-y += -I$(src)/src/main/ipcns/compat
ccflags-y += -I$(src)/src/misc -I$(src)/src/misc/xt -I$(src)/src/misc/userns
ccflags-y += -I$(src)/src/misc/devtmpfs -I$(src)/src/misc/pts_shield
ccflags-y += $(addprefix -I$(src)/,$(DEPS_INCS_ALL))

# the target kernel's own tree, which the ddk lays out at $(KDIR)/source. a few
# declarations this module needs are in headers the kernel never publishes, the
# driver core's drivers/base/base.h above all, and a function pointer's type has to
# come from the kernel's declaration rather than a second guess at it: clang's cfi
# hashes the whole type, so a guess that drifts by one qualifier is a fatal cfi
# failure on the device
ccflags-y += -I$(KDIR)/source

# 6.18 turns on -Wmissing-prototypes, and this module both defines kernel
# names the newer kernel stops declaring and exposes entry points through
# headers the shim files do not include; older compilers ignore the flag
ccflags-y += -Wno-missing-prototypes

# KernCall: enable sys_call_table slot patching
ccflags-y += -DCONFIG_KERNSC_PATCH -DCONFIG_KERNSC_DISCOVER

# KallRecon: fast concurrent discovery with aggressive search strategy
ccflags-y += -DKALLRECON_RAANA -DKALLRECON_RAANA_FALLBACK -DKALLRECON_FIRST_HIT \
             -DKALLRECON_RAANA_NUM -DKALLRECON_VARIANT_FAST

# Suppress outbound KCFI indirect-call traps on trampoline branches while keeping inbound hashes
ccflags-y += -include $(src)/src/kcfi_safe.h

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

# droid_lkm

out of tree kernel modules that give a stock GKI 6.12 kernel the namespace and
IPC features container runtimes expect. android16-6.12 ships with
CONFIG_SYSVIPC, CONFIG_POSIX_MQUEUE, CONFIG_PID_NS and CONFIG_IPC_NS turned
off, so a container tool like Droidspaces cannot start anything at all. this
project supplies those features from loadable modules, without recompiling the
kernel and without moving a single struct member, so prebuilt vendor modules
keep working.

two modules are built:

- `droid_lkm.ko` namespace and IPC support
- `droid_lkm_compat.ko` vendor module quick fixups

## what it provides

- fake pid and ipc namespaces: `unshare`, `clone`, `clone3` and `setns` for
  CLONE_NEWPID and CLONE_NEWIPC, with per namespace lifetime handling
- ported SysV IPC: the msg, sem and shm syscalls plus their per namespace
  sysctls
- ported POSIX mqueue: six mq syscalls, mqueuefs and its per namespace sysctls
- `/proc/<pid>/ns/pid` and `/proc/<pid>/ns/ipc`, entries a kernel with those
  namespaces compiled out never creates
- `NSpid`, `NStgid`, `NSpgid` and `NSsid` lines in `/proc/<pid>/status`
- vendor quick fixups in the second module: a ghost task handed out when a
  vendor module asks `find_task_by_vpid` for a pid that only exists inside a
  container namespace, and a bounded pid value on that task for vendor code
  that indexes arrays by `p->pid`

## requirements

- ARM64 device running GKI 6.12
- a way to load out of tree modules, for example a root solution or KernelPatch
- Docker, for the DDK build container
- the library set pinned in `deps.lst`, fetched by the LKM SDK

## build

	scripts/fetch-deps.sh
	scripts/build-ddkk.sh android16-6.12

`fetch-deps.sh` clones the SDK at the revision in `.sdk-version` and vendors
every library listed in `deps.lst` below `deps/`. `build-ddkk.sh <target>` runs
the DDK container and writes both modules plus their build tree into
`out/<target>/`. library sources are never committed here, `deps.lst` pins the
exact revision of each one.

## usage

	insmod droid_lkm.ko
	insmod droid_lkm_compat.ko

load `droid_lkm.ko` before any container starts, then `droid_lkm_compat.ko` for
the vendor fixups. both modules resolve their kernel symbols at load time and
report anything they cannot find; a missing core symbol aborts the load instead
of leaving a half installed hook behind.

module parameters cover the usual cases: `mqueue`, `gate`, `verbose`,
`skip_sysvipc` and `no_fake_ns` on the first module, `skip_do_exit` on its pid
namespace part, `ghost` and `ghost_match` on the second. each one is documented
in its own `MODULE_PARM_DESC`. `ghost_match` selects vendor modules by name
prefix and defaults to `oplus_`, `*` matches every caller.

## vendor compatibility

`droid_lkm.ko` is vendor neutral, everything it provides comes from the kernel
side and behaves the same on any device.

`droid_lkm_compat.ko` exists for OPPO and OnePlus kernels only. their vendor
modules call `find_task_by_vpid` and use the returned task without checking it,
and they index per pid arrays with `p->pid`. a pid that only exists inside a
container namespace breaks both assumptions, which ends in a null dereference or
an out of bounds access. the module answers those callers with a substitute task
that carries a bounded pid, selected by module name prefix (`oplus_` by default,
`*` matches every caller).

Xiaomi devices do not install this module: MIUI and HyperOS vendor modules do not
rely on that pattern, so the compat half is not needed there.

## known limits

- no user namespaces
- no devtmpfs
- no cgroup pids or device controllers
- no nftables match set
- `NSpid` and its siblings are printed at the end of `/proc/<pid>/status`
- namespace lifetime is owned by the module, so unloading while a container runs
  leaves that namespace neutralized
- the regression harness is not part of this repository

## credits

- [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS) by ravindu644,
  the container runtime this work exists for
- [KMSDK](https://github.com/Dere3046/KMSDK), the library SDK that fetches and
  vendors the dependencies
- [KallRecon](https://github.com/Dere3046/KallRecon), kernel symbol resolution
- [Type_info](https://github.com/Dere3046/Type_info), runtime struct layout
- [KernCall](https://github.com/Dere3046/KernCall), syscall table patching
- [HooKern](https://github.com/Dere3046/HooKern), inline hook and kprobe engine

## license

GPL-2.0

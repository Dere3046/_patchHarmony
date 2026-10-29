# droid_lkm

Out-of-tree kernel modules that provide missing namespace and IPC features
(`CONFIG_SYSVIPC`, `CONFIG_POSIX_MQUEUE`, `CONFIG_PID_NS`, `CONFIG_IPC_NS`)
for stock GKI kernels. It enables container runtimes like Droidspaces without
kernel recompilation or struct layout modifications, preserving vendor module
compatibility.

## Support Matrix

| Kernel Version | Android Version | Target |
| :--- | :--- | :--- |
| **5.10** | Android 12 | `android12-5.10` |
| **5.10** | Android 13 | `android13-5.10` |
| **5.15** | Android 13 | `android13-5.15` |
| **5.15** | Android 14 | `android14-5.15` |
| **6.1** | Android 14 | `android14-6.1` |
| **6.6** | Android 15 | `android15-6.6` |
| **6.12** | Android 16 | `android16-6.12` |
| **6.18** | Android 17 | `android17-6.18` |

## Fake user namespace (`droid_lkm_misc`)

Stock GKI builds no `CONFIG_USER_NS`, so the module emulates one.
`unshare(CLONE_NEWUSER)` and `clone(CLONE_NEWUSER)` are intercepted, the flag is
stripped before the kernel sees it, and the task is moved into a namespace of the
module's making with a full capability set; `setns(CLONE_NEWUSER)` on a
`/proc/<pid>/ns/user` descriptor joins one that already exists (the pidfd route
stays closed, because the kernel compiles that branch out without
`CONFIG_USER_NS`). `uid_map`, `gid_map` and `setgroups` are served under
`/proc/<pid>/` and may be written once.

Two properties are structural. Know them before relying on this:

- **There is no id translation.** With `CONFIG_USER_NS` off the kernel compiles
  `from_kuid()` and `make_kuid()` to the identity, so numbers never change: a
  task keeps its host uid inside the namespace. `uid_map` is recorded and used by
  the module's own checks, but `getuid()` still returns the host id. Anything
  that decides "am I root" by comparing `geteuid()` to 0 will answer no — the
  `security_capable` chokepoint only answers capability questions. That is also
  why the capability set is carried across `execve()` through the ambient set:
  `cap_bprm_creds_from_file()` keys the root behaviour off `euid`, which is never
  0 here, and an empty ambient set would drop the grant at the first `exec`.
- **Authority is anchored at creation.** A creator holding `CAP_SETUID` and
  `CAP_SETGID` over the initial namespace gets the identity map — a privileged
  container, where container root is host root. Any other creator gets container
  id 0 mapped to its own host id and nothing else. A creator whose `euid` is 0
  but which holds no capability over the initial namespace is refused rather
  than handed a root mapping.

One thing to know when checking the result: `/proc/<pid>/ns/user` caches its
`ns_ops` in the dentry. The symlink inode keeps the operations that were chosen
when it was first looked up, so a task that reads `/proc/self/ns/user`, calls
`setns(CLONE_NEWUSER)`, and reads the same path again still sees the namespace
it came from. Read `/proc/thread-self/ns/user`, or read the link from another
process, to see the one the task is actually in.

Full user namespace semantics need `CONFIG_USER_NS` in the kernel; the module
reports itself unsupported when it is enabled.

## Contributors

- [XGH2333](https://github.com/XGH2333) (O3 CPU support)

## Credits

- [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS) by ravindu644 — target container runtime
- [KMSDK](https://github.com/Dere3046/KMSDK) — SDK for library fetching and vendoring
- [KallRecon](https://github.com/Dere3046/KallRecon) — runtime kernel symbol resolution
- [Type_info](https://github.com/Dere3046/Type_info) — runtime struct layout reconstruction
- [KernCall](https://github.com/Dere3046/KernCall) — syscall table patching
- [HooKern](https://github.com/Dere3046/HooKern) — inline hook and kprobe engine

## License

GPL-2.0

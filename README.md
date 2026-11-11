# droid_lkm

Out-of-tree kernel modules that provide missing namespace, IPC and container
features (`CONFIG_SYSVIPC`, `CONFIG_POSIX_MQUEUE`, `CONFIG_PID_NS`, `CONFIG_IPC_NS`,
`devtmpfs`, `xt_addrtype`, fake userns) for stock GKI kernels. It enables container
runtimes like Droidspaces without kernel recompilation or struct layout modifications,
preserving vendor module compatibility.

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

## Container helpers & fake userns (`droid_lkm_misc`)

On 5.15+, `droid_lkm_misc` provides additional container requirements:
- **`devtmpfs` & `xt_addrtype`**: required for container `/dev` mounting and iptables routing.
- **Fake user namespace**: emulates `CLONE_NEWUSER`, `/proc/<pid>/ns/user`, and capability scopes so privileged runtimes (like Droidspaces running as root) can initialize without kernel `CONFIG_USER_NS`.

**Limitations:**
- **No UID/GID translation**: host UIDs never change inside the namespace (`make_kuid()` remains identity).
- **No rootless containers**: unprivileged runtimes (e.g. rootless Podman) will not see UID 0 or VFS root authority and are **not supported**.

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

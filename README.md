# droid_lkm

Out-of-tree kernel modules that provide missing namespace and IPC features
(`CONFIG_SYSVIPC`, `CONFIG_POSIX_MQUEUE`, `CONFIG_PID_NS`, `CONFIG_IPC_NS`)
for stock GKI kernels. It enables container runtimes like Droidspaces without
kernel recompilation or struct layout modifications, preserving vendor module
compatibility.

## Support Matrix

| Kernel Version | Android Version | Target |
| :--- | :--- | :--- |
| **5.10** | Android 13 | `android13-5.10` |
| **5.15** | Android 13 | `android13-5.15` |
| **5.15** | Android 14 | `android14-5.15` |
| **6.1** | Android 14 | `android14-6.1` |
| **6.6** | Android 15 | `android15-6.6` |
| **6.12** | Android 16 | `android16-6.12` |
| **6.18** | Android 17 | `android17-6.18` |

## Credits

- [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS) by ravindu644 — target container runtime
- [KMSDK](https://github.com/Dere3046/KMSDK) — SDK for library fetching and vendoring
- [KallRecon](https://github.com/Dere3046/KallRecon) — runtime kernel symbol resolution
- [Type_info](https://github.com/Dere3046/Type_info) — runtime struct layout reconstruction
- [KernCall](https://github.com/Dere3046/KernCall) — syscall table patching
- [HooKern](https://github.com/Dere3046/HooKern) — inline hook and kprobe engine

## License

GPL-2.0

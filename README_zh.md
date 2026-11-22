# droid_lkm

[English](README.md) | 中文

面向原生 GKI 内核的树外内核模块，用于补全缺失的命名空间与 IPC 特性
（`CONFIG_SYSVIPC`、`CONFIG_POSIX_MQUEUE`、`CONFIG_PID_NS`、`CONFIG_IPC_NS`）。
无需重新编译内核或修改结构体布局，即可支持 Droidspaces 等容器运行时，同时保持与厂商模块的兼容性。

## 支持矩阵

| 内核版本 | Android 版本 | 目标 |
| :--- | :--- | :--- |
| **5.10** | Android 12 | `android12-5.10` |
| **5.10** | Android 13 | `android13-5.10` |
| **5.15** | Android 13 | `android13-5.15` |
| **5.15** | Android 14 | `android14-5.15` |
| **6.1** | Android 14 | `android14-6.1` |
| **6.6** | Android 15 | `android15-6.6` |
| **6.12** | Android 16 | `android16-6.12` |
| **6.18** | Android 17 | `android17-6.18` |

## 贡献者

见 [CONTRIBUTORS.md](CONTRIBUTORS.md)。

## 致谢

见 [CREDITS.md](CREDITS.md)。

## 许可证

[GPL-2.0](LICENSE)

#!/usr/bin/env python3
"""确定性 zip 打包器，供 package-module.sh 在没有 zip 命令时兜底。

Windows 上 git checkout 出来的文件没有可执行位，所以权限按文件名写死：
脚本和 update-binary 一律 0755，其余 0644。
"""
import os
import stat
import sys
import zipfile

EXEC_NAMES = {"update-binary"}
EXEC_SUFFIXES = (".sh",)


def mode_for(rel):
    name = os.path.basename(rel)
    if name in EXEC_NAMES or name.endswith(EXEC_SUFFIXES):
        return 0o755
    return 0o644


def collect(src):
    items = []
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for f in sorted(files):
            full = os.path.join(root, f)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            items.append((rel, full))
    items.sort(key=lambda x: x[0])
    return items


def main():
    if len(sys.argv) != 3:
        print("usage: mkzip.py <srcdir> <out.zip>", file=sys.stderr)
        return 2
    src, out = sys.argv[1], sys.argv[2]
    if not os.path.isdir(src):
        print("not a directory: " + src, file=sys.stderr)
        return 2

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel, full in collect(src):
            info = zipfile.ZipInfo(rel, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | mode_for(rel)) << 16
            with open(full, "rb") as fh:
                z.writestr(info, fh.read())
    return 0


if __name__ == "__main__":
    sys.exit(main())

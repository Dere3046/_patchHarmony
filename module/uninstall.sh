#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 dere3046

DATA=${DATA:-/data/adb/phdroid}

rm -rf "$DATA"
echo "phDroid removed  reboot drops the module from kernel memory"

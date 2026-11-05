#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 dere3046

SKIPUNZIP=1

DATA=${DATA:-/data/adb/phdroid}

command -v ui_print >/dev/null || ui_print() { echo "$1"; }
command -v abort >/dev/null || abort() { ui_print "$1"; exit 1; }

require_arm64() {
    [ "$ARCH" = arm64 ] || [ "$(uname -m)" = aarch64 ] || abort "! arm64 only"
}

detect_kmi() {
    local release ver tag
    release=$(uname -r)
    ver=$(printf '%s' "$release" | sed -n 's/^\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
    tag=$(printf '%s' "$release" | sed -n 's/.*\(android[0-9][0-9]*\).*/\1/p')
    [ -n "$ver" ] && [ -n "$tag" ] && echo "$tag-$ver"
}

unpack() {
    unzip -o "$ZIPFILE" module.prop config.conf service.sh uninstall.sh \
        -d "$MODPATH" >&2 || abort "! unpack failed"
}

unpack_one() {
    mkdir -p "$MODPATH/ko"
    unzip -o "$ZIPFILE" "kernels/$1/*.ko" -d "$MODPATH" >&2 || abort "! unpack $1 failed"
    [ -d "$MODPATH/kernels/$1" ] || abort "! no build for $1"
    mv -f "$MODPATH"/kernels/"$1"/*.ko "$MODPATH"/ko/ || abort "! move $1 failed"
    rm -rf "$MODPATH/kernels"
}

chmod_module() {
    chmod 0755 "$MODPATH" "$MODPATH/ko" "$MODPATH"/*.sh
    chmod 0644 "$MODPATH"/module.prop "$MODPATH"/config.conf
    find "$MODPATH"/ko -name '*.ko' -exec chmod 0644 {} \;
}

main() {
    local kmi
    require_arm64
    unpack

    kmi=$(detect_kmi)
    [ -n "$kmi" ] || abort "! cannot detect KMI from $(uname -r)"

    unpack_one "$kmi"

    mkdir -p "$DATA"
    rm -f "$DATA/blocked" "$DATA/insmod"

    chmod_module
    ui_print "- kmi    : $kmi"
    ui_print "- payload: $(ls "$MODPATH/ko" | tr '\n' ' ')"
}

main

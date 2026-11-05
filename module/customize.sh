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

unpack_static() {
    unzip -o "$ZIPFILE" module.prop config.conf service.sh uninstall.sh \
        -d "$MODPATH" >&2 || abort "! unpack failed"
}

unpack_build() {
    mkdir -p "$MODPATH/bin"
    unzip -o "$ZIPFILE" "bin/$1/*" -d "$MODPATH" >&2 || abort "! unpack $1 failed"
    [ -d "$MODPATH/bin/$1" ] || abort "! no build for $1"
}

chmod_module() {
    chmod 0755 "$MODPATH" "$MODPATH/bin" "$MODPATH"/*.sh
    chmod 0644 "$MODPATH"/module.prop "$MODPATH"/config.conf
    find "$MODPATH"/bin -name '*.ko' -exec chmod 0644 {} \;
}

main() {
    local kmi
    require_arm64
    unpack_static

    kmi=$(detect_kmi)
    [ -n "$kmi" ] || abort "! cannot detect KMI from $(uname -r)"
    unpack_build "$kmi"

    mkdir -p "$DATA"
    rm -f "$DATA/blocked" "$DATA/insmod"

    chmod_module
    ui_print "- payload: $(ls "$MODPATH/bin/$kmi" | tr '\n' ' ')"
}

main

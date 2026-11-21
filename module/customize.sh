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

detect_ver() {
    printf '%s' "$(uname -r)" | sed -n 's/^\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p'
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
    if unzip -l "$ZIPFILE" 'webroot/*' >/dev/null 2>&1; then
        unzip -o "$ZIPFILE" 'webroot/*' -d "$MODPATH" >&2 || abort "! unpack webroot failed"
    fi
}

unpack_build() {
    local kmi=$1
    local ver=$2
    local d count=0 kmis=""

    mkdir -p "$MODPATH/bin"
    if [ -n "$kmi" ]; then
        unzip -o "$ZIPFILE" "bin/$kmi/*" -d "$MODPATH" >&2 || abort "! unpack $kmi failed"
        [ -d "$MODPATH/bin/$kmi" ] || abort "! no build for $kmi"
        return 0
    fi

    [ -n "$ver" ] || abort "! cannot detect KMI from $(uname -r)"
    unzip -o "$ZIPFILE" "bin/*-$ver/*" -d "$MODPATH" >&2 || abort "! no build matching *-$ver"
    for d in "$MODPATH"/bin/*-"$ver"; do
        [ -d "$d" ] || continue
        count=$((count + 1))
        kmis="${kmis:+$kmis }${d##*/}"
    done
    [ "$count" -gt 0 ] || abort "! no build for kernel $ver"
    if [ "$count" -gt 1 ]; then
        ui_print "- multiple KMIs for $ver: $kmis (select in WebUI)"
    else
        ui_print "- fallback KMI matched: $kmis"
    fi
}

unpacked_kmis() {
    local d out=""
    for d in "$MODPATH"/bin/*; do
        [ -d "$d" ] || continue
        out="${out:+$out }${d##*/}"
    done
    printf '%s\n' "$out"
}

detect_modules() {
    local dir=$1
    local mods=""
    local f name

    [ -f "$dir/droid_lkm.ko" ] && mods="droid_lkm"
    for f in "$dir"/*.ko; do
        [ -f "$f" ] || continue
        name=${f##*/}
        name=${name%.ko}
        [ "$name" = "droid_lkm" ] && continue
        mods="${mods:+$mods }$name"
    done
    printf '%s\n' "$mods"
}

has_modules() {
    [ -f "$1" ] && grep -q '^modules=[^[:space:]]' "$1" 2>/dev/null
}

normalize_pid_impl() {
    local bad=$1
    local f=$2
    [ -f "$f" ] || return 0
    if grep -q "pid_impl=$bad" "$f" 2>/dev/null; then
        sed "s/[[:space:]]*pid_impl=$bad//g; s/droid_lkm=[[:space:]]*\$/droid_lkm=/g" \
            "$f" > "$f.tmp" && mv -f "$f.tmp" "$f" && chmod 0644 "$f"
    fi
}

# @@PHDROID_CUSTOMIZE_VARIANTS@@

seed_data() {
    local kmis first_kmi mods
    kmis=$(unpacked_kmis)
    first_kmi=${kmis%% *}
    [ -n "$first_kmi" ] || abort "! no KMI unpacked"

    mods=$(detect_modules "$MODPATH/bin/$first_kmi")
    [ -n "$mods" ] || abort "! no .ko for $first_kmi"

    if ! has_modules "$MODPATH/config.conf"; then
        printf 'modules=%s\n' "$mods" > "$MODPATH/config.conf"
    fi

    if [ "$kmis" != "$first_kmi" ]; then
        ui_print "- multi-KMI ready ($kmis): confirm version in WebUI before load"
    fi

    mkdir -p "$DATA"
    if ! has_modules "$DATA/config.conf"; then
        cp -f "$MODPATH/config.conf" "$DATA/config.conf"
        chmod 0644 "$DATA/config.conf"
    fi
    seed_variant_fixup "$DATA/config.conf"
    rm -f "$DATA/blocked" "$DATA/insmod"
}

chmod_module() {
    chmod 0755 "$MODPATH" "$MODPATH/bin" "$MODPATH"/*.sh
    chmod 0644 "$MODPATH"/module.prop "$MODPATH"/config.conf
    find "$MODPATH"/bin -name '*.ko' -exec chmod 0644 {} \;
    if [ -d "$MODPATH/webroot" ]; then
        find "$MODPATH/webroot" -type d -exec chmod 0755 {} \;
        find "$MODPATH/webroot" -type f -exec chmod 0644 {} \;
    fi
}

main() {
    local kmi ver kmis first_kmi
    require_arm64
    unpack_static

    kmi=$(detect_kmi)
    ver=$(detect_ver)
    unpack_build "$kmi" "$ver"

    seed_data
    chmod_module
    kmis=$(unpacked_kmis)
    first_kmi=${kmis%% *}
    ui_print "- payload ($kmis): $(ls "$MODPATH/bin/$first_kmi" | tr '\n' ' ')"
}

main

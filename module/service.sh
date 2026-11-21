#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 dere3046

MODDIR=${MODDIR:-${0%/*}}
DATA=${DATA:-/data/adb/phdroid}
CONF=$DATA/config.conf
DEF_CONF=$MODDIR/config.conf
LOG=$DATA/phdroid.log
AUTO=$DATA/auto
MARK=$DATA/insmod
BLOCK=$DATA/blocked
LOG_MAX=262144

log() {
    local size
    mkdir -p "$DATA"
    if [ -f "$LOG" ]; then
        size=$(wc -c < "$LOG" 2>/dev/null)
        [ "${size:-0}" -gt "$LOG_MAX" ] && rm -f "$LOG"
    fi
    printf '%s %s\n' "$(date '+%m-%d %H:%M:%S')" "$*" >> "$LOG"
}

detect_modules() {
    local mods=""
    local f name

    for f in "$MODDIR"/bin/*/droid_lkm.ko; do
        [ -f "$f" ] && mods="droid_lkm"
        break
    done
    for f in "$MODDIR"/bin/*/*.ko; do
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

strip_param_tok() {
    local drop=$1
    local params=$2
    local out="" tok
    for tok in $params; do
        [ "$tok" = "$drop" ] && continue
        out="${out:+$out }$tok"
    done
    printf '%s\n' "$out"
}

# @@PHDROID_SERVICE_VARIANTS@@

ensure_conf() {
    local mods
    mkdir -p "$DATA"
    if has_modules "$CONF"; then
        return 0
    fi
    if has_modules "$DEF_CONF"; then
        cp -f "$DEF_CONF" "$CONF"
        chmod 0644 "$CONF"
        return 0
    fi
    mods=$(detect_modules)
    if [ -n "$mods" ]; then
        printf 'modules=%s\n' "$mods" > "$CONF"
        chmod 0644 "$CONF"
    fi
}

active_conf() {
    if [ -f "$CONF" ]; then
        printf '%s\n' "$CONF"
    elif [ -f "$DEF_CONF" ]; then
        printf '%s\n' "$DEF_CONF"
    fi
}

conf_get() {
    sed -n "s/^$1=//p" "$2" 2>/dev/null | tail -n 1
}

is_loaded() {
    grep -q "^$1 " /proc/modules 2>/dev/null
}

find_ko() {
    local mod=$1
    local kmi=${2:-}
    local release ver tag

    if [ -n "$kmi" ] && [ -f "$MODDIR/bin/$kmi/$mod.ko" ]; then
        printf '%s\n' "$MODDIR/bin/$kmi/$mod.ko"
        return 0
    fi

    release=$(uname -r)
    ver=$(printf '%s' "$release" | sed -n 's/^\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
    tag=$(printf '%s' "$release" | sed -n 's/.*\(android[0-9][0-9]*\).*/\1/p')
    if [ -n "$tag" ] && [ -n "$ver" ] && [ -f "$MODDIR/bin/$tag-$ver/$mod.ko" ]; then
        printf '%s\n' "$MODDIR/bin/$tag-$ver/$mod.ko"
        return 0
    fi
    if [ -n "$ver" ]; then
        local match_cnt=0 match_p="" f
        for f in "$MODDIR"/bin/*-"$ver"/"$mod".ko; do
            [ -f "$f" ] || continue
            match_cnt=$((match_cnt + 1))
            match_p=$f
        done
        if [ "$match_cnt" -eq 1 ]; then
            printf '%s\n' "$match_p"
            return 0
        fi
        if [ "$match_cnt" -gt 1 ]; then
            return 2
        fi
    fi

    local any_cnt=0 any_p="" f
    for f in "$MODDIR"/bin/*/"$mod".ko; do
        [ -f "$f" ] || continue
        any_cnt=$((any_cnt + 1))
        any_p=$f
    done
    if [ "$any_cnt" -eq 1 ]; then
        printf '%s\n' "$any_p"
        return 0
    fi
    [ "$any_cnt" -gt 1 ] && return 2
    return 1
}

install_with_conf() {
    local cfg=$1
    local mode=$2
    local m ko cfg_kmi params out rc
    local loaded=0
    local skipped=0

    cfg_kmi=$(conf_get kmi "$cfg")
    for m in $(conf_get modules "$cfg"); do
        if is_loaded "$m"; then
            log "[$mode] skip $m already loaded"
            skipped=$((skipped + 1))
            continue
        fi
        ko=$(find_ko "$m" "$cfg_kmi")
        rc=$?
        if [ "$rc" -eq 2 ]; then
            log "[$mode] kmi not selected for multi-KMI package"
            printf 'select kmi in webui first\n' >&2
            return 1
        fi
        if [ "$rc" -ne 0 ]; then
            if [ "$m" = "droid_lkm_vendor" ]; then
                log "[$mode] skip optional $m.ko absent"
                continue
            fi
            log "[$mode] missing $m.ko"
            printf 'missing %s.ko\n' "$m" >&2
            return 1
        fi
        params=$(sanitize_params "$m" "$(conf_get "$m" "$cfg")")
        out=$(insmod "$ko" $params 2>&1)
        rc=$?
        log "[$mode] insmod $m (${ko##*/bin/}) params=[$params] rc=$rc${out:+ out=$out}"
        [ -n "$out" ] && printf '%s\n' "$out"
        [ "$rc" -eq 0 ] || return "$rc"
        loaded=$((loaded + 1))
    done

    if [ "$mode" = "manual" ] && [ "$loaded" -eq 0 ] && [ "$skipped" -gt 0 ]; then
        printf 'already loaded\n'
    fi
    return 0
}

cmd_boot() {
    local cfg rc
    mkdir -p "$DATA"
    ensure_conf

    [ -f "$AUTO" ] || return 0

    if [ -f "$BLOCK" ]; then
        log "[boot] blocked"
        return 0
    fi

    if [ -f "$MARK" ]; then
        mv -f "$MARK" "$BLOCK"
        log "[boot] previous insmod never returned blocked"
        return 0
    fi

    cfg=$(active_conf)
    [ -n "$cfg" ] && [ -f "$cfg" ] || {
        log "[boot] no config.conf"
        return 1
    }
    [ -n "$(conf_get modules "$cfg")" ] || {
        log "[boot] no modules declared"
        return 1
    }

    printf '%s\n' "$(conf_get modules "$cfg")" > "$MARK"
    sync
    install_with_conf "$cfg" boot
    rc=$?
    rm -f "$MARK"
    sync

    if [ "$rc" -eq 0 ]; then
        log "[boot] loaded $(conf_get modules "$cfg")"
    else
        log "[boot] install failed rc=$rc"
    fi
    return "$rc"
}

cmd_manual() {
    local cfg=${1:-}
    mkdir -p "$DATA"
    ensure_conf

    if [ -f "$BLOCK" ]; then
        log "[manual] blocked"
        printf 'blocked\n' >&2
        return 1
    fi

    if [ -z "$cfg" ]; then
        cfg=$(active_conf)
    fi
    [ -n "$cfg" ] && [ -f "$cfg" ] || {
        log "[manual] no config.conf"
        printf 'no config.conf\n' >&2
        return 1
    }
    [ -n "$(conf_get modules "$cfg")" ] || {
        log "[manual] no modules declared"
        printf 'no modules declared\n' >&2
        return 1
    }

    install_with_conf "$cfg" manual
}

case "${1:-boot}" in
    boot)
        cmd_boot
        ;;
    install)
        shift
        cmd_manual "${1:-}"
        ;;
    *)
        printf 'usage: service.sh [boot|install [conf]]\n' >&2
        exit 2
        ;;
esac

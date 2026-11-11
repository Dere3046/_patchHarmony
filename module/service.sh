#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 dere3046

MODDIR=${0%/*}
DATA=${DATA:-/data/adb/phdroid}
CONF=$MODDIR/config.conf
LOG=$DATA/phdroid.log
AUTO=$DATA/auto
MARK=$DATA/insmod
BLOCK=$DATA/blocked
LOG_MAX=262144

log() {
    local size
    if [ -f "$LOG" ]; then
        size=$(wc -c < "$LOG" 2>/dev/null)
        [ "${size:-0}" -gt "$LOG_MAX" ] && rm -f "$LOG"
    fi
    printf '%s %s\n' "$(date '+%m-%d %H:%M:%S')" "$*" >> "$LOG"
}

conf() {
    sed -n "s/^$1=//p" "$CONF" 2>/dev/null | tail -n 1
}

get_modules() {
    local mods=$(conf modules)
    [ -n "$mods" ] || mods="droid_lkm droid_lkm_vendor"
    set -- "$MODDIR"/bin/*/droid_lkm_misc.ko
    if [ -f "$1" ]; then
        case " $mods " in
        *" droid_lkm_misc "*) ;;
        *) mods="$mods droid_lkm_misc" ;;
        esac
    fi
    echo "$mods"
}

install() {
    local m ko out rc mods
    mods=$(get_modules)
    for m in $mods; do
        set -- "$MODDIR"/bin/*/"$m".ko
        ko=$1
        [ -f "$ko" ] || {
            log "missing $m.ko, skipped"
            continue
        }
        out=$(insmod "$ko" $(conf "$m") 2>&1)
        rc=$?
        [ -n "$out" ] && log "insmod $m: $out"
        if [ "$rc" -ne 0 ]; then
            if [ "$m" = "droid_lkm_misc" ]; then
                log "droid_lkm_misc failed (rc=$rc), skipped"
                continue
            fi
            return "$rc"
        fi
    done
    return 0
}

main() {
    local rc mods
    mkdir -p "$DATA"

    [ -f "$AUTO" ] || return 0

    if [ -f "$BLOCK" ]; then
        log "blocked"
        return 0
    fi

    if [ -f "$MARK" ]; then
        mv -f "$MARK" "$BLOCK"
        log "previous insmod never returned, blocked"
        return 0
    fi

    [ -f "$CONF" ] || {
        log "no config.conf"
        return 1
    }

    mods=$(get_modules)
    [ -n "$mods" ] || {
        log "no modules declared"
        return 1
    }

    printf '%s\n' "$mods" > "$MARK"
    sync
    install
    rc=$?
    rm -f "$MARK"
    sync

    if [ "$rc" -eq 0 ]; then
        log "loaded $mods"
    else
        log "install failed rc=$rc"
    fi
    return "$rc"
}

main

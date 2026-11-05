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

install() {
    local m ko out rc
    for m in $(conf modules); do
        set -- "$MODDIR"/bin/*/"$m".ko
        ko=$1
        [ -f "$ko" ] || {
            log "missing $m.ko"
            return 1
        }
        out=$(insmod "$ko" $(conf "$m") 2>&1)
        rc=$?
        [ -n "$out" ] && log "insmod $m: $out"
        [ "$rc" -eq 0 ] || return "$rc"
    done
    return 0
}

main() {
    local rc
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
    [ -n "$(conf modules)" ] || {
        log "no modules declared"
        return 1
    }

    printf '%s\n' "$(conf modules)" > "$MARK"
    sync
    install
    rc=$?
    rm -f "$MARK"
    sync

    if [ "$rc" -eq 0 ]; then
        log "loaded $(conf modules)"
    else
        log "install failed rc=$rc"
    fi
    return "$rc"
}

main

#!/system/bin/sh
#
# late_start 加载 droid_lkm；insmod 期间用标记文件检测内核崩溃。

MODDIR=${0%/*}
[ -d "$MODDIR" ] || MODDIR=$(pwd)

LOG=$MODDIR/phdroid.log
MARK=$MODDIR/insmod.inflight
BLOCK=$MODDIR/blocked
CONF=$MODDIR/config.conf
LOG_MAX=262144

ph_log() {
	if [ -f "$LOG" ]; then
		sz=$(wc -c < "$LOG" 2>/dev/null | tr -d ' ')
		[ -n "$sz" ] && [ "$sz" -gt "$LOG_MAX" ] && mv -f "$LOG" "$LOG.1" 2>/dev/null
	fi
	echo "$(date '+%m-%d %H:%M:%S') $*" >> "$LOG" 2>/dev/null
	if [ -w /dev/kmsg ]; then
		echo "<6>phDroid: $*" > /dev/kmsg 2>/dev/null
	fi
	command -v log >/dev/null 2>&1 && log -t phDroid "$*" 2>/dev/null
	return 0
}

# 读 config 的 key，缺省用第二个参数
conf() {
	v=
	if [ -f "$CONF" ]; then
		v=$(sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$CONF" 2>/dev/null \
			| tail -n 1 | tr -d '\r' | sed 's/[[:space:]]*$//')
	fi
	[ -n "$v" ] && echo "$v" || echo "$2"
}

bool() {
	case "$(conf "$1" "$2")" in
	1 | true | on | yes) echo 1 ;;
	0 | false | off | no) echo 0 ;;
	*) echo "$2" ;;
	esac
}

# 允许负数：pid_max_cap=-1 表示从不封顶
sint() {
	v=$(conf "$1" "$2")
	case "$v" in
	'') echo "$2" ;;
	-*)
		rest=${v#-}
		case "$rest" in
		'' | *[!0-9]*) echo "$2" ;;
		*) echo "$v" ;;
		esac
		;;
	*[!0-9]*) echo "$2" ;;
	*) echo "$v" ;;
	esac
}

loaded() {
	[ -r /proc/modules ] && grep -q "^$1 " /proc/modules
}

insmod_ko() {
	f=$1
	shift
	if command -v insmod >/dev/null 2>&1; then
		insmod "$f" "$@" 2>&1
		return $?
	fi
	for bb in "${MAGISKTMP:-/nonexistent}/busybox" \
		/data/adb/ksu/bin/busybox \
		/data/adb/ap/bin/busybox \
		/data/adb/magisk/busybox; do
		[ -x "$bb" ] || continue
		"$bb" insmod "$f" "$@" 2>&1
		return $?
	done
	echo "insmod not found"
	return 127
}

# 落标记 -> insmod -> 立刻删标记；结果在 INS_RC / INS_OUT
try_insmod() {
	f=$1
	shift
	: > "$MARK" 2>/dev/null
	command -v sync >/dev/null 2>&1 && sync
	INS_OUT=$(insmod_ko "$f" "$@")
	INS_RC=$?
	rm -f "$MARK" 2>/dev/null
	command -v sync >/dev/null 2>&1 && sync
	return $INS_RC
}

# EPERM 时用真实域/上下文补一条最小 module_load 规则
selinux_allow() {
	ko=$1
	mp=
	for c in magiskpolicy "${MAGISKTMP:-/nonexistent}/magiskpolicy" \
		/data/adb/magisk/magiskpolicy; do
		if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then
			mp=$c
			break
		fi
	done
	[ -n "$mp" ] || return 1

	sctx=$(cat /proc/self/attr/current 2>/dev/null | tr -d '\0')
	sdom=${sctx#u:r:}
	sdom=${sdom%%:*}
	tctx=$(ls -Z "$ko" 2>/dev/null | awk '{print $1}')
	ttype=${tctx#u:object_r:}
	ttype=${ttype%%:*}
	case "$sdom" in '' | *[!a-zA-Z0-9_]*) return 1 ;; esac
	case "$ttype" in '' | *[!a-zA-Z0-9_]*) return 1 ;; esac

	"$mp" --live "allow $sdom $ttype:system module_load" >/dev/null 2>&1 || return 1
	ph_log "selinux: allow $sdom $ttype:system module_load"
	return 0
}

guarded_insmod() {
	try_insmod "$@"
	rc=$?
	if [ "$rc" -ne 0 ] && [ "$(bool selinux_fix 0)" = 1 ]; then
		case "$INS_OUT" in
		*"not permitted"* | *"Permission denied"* | *EPERM*)
			if selinux_allow "$1"; then
				try_insmod "$@"
				rc=$?
			fi
			;;
		esac
	fi
	[ -n "$INS_OUT" ] && ph_log "insmod: $INS_OUT"
	return $rc
}

runtime() {
	if [ -d /data/adb/ksu ]; then
		echo KernelSU
	elif [ -d /data/adb/ap ]; then
		echo APatch
	elif [ -d /data/adb/magisk ]; then
		echo Magisk
	else
		echo unknown
	fi
}

# 只读状态，供 WebUI 调用
if [ "$1" = "status" ]; then
	echo "version=$(sed -n 's/^version=//p' "$MODDIR/module.prop" 2>/dev/null)"
	echo "manager=$(runtime)"
	echo "kernel=$(uname -r)"
	echo "kmi=$(conf kmi -)"
	if loaded droid_lkm; then echo "loaded=1"; else echo "loaded=0"; fi
	if loaded droid_lkm_vendor; then echo "compat=1"; else echo "compat=0"; fi
	case "$(conf compat off)" in
	on | 1 | yes | true) cw=on ;;
	auto) [ "$(conf oplus 0)" = 1 ] && cw=on || cw=off ;;
	*) cw=off ;;
	esac
	if [ "$cw" = on ] && [ ! -f "$MODDIR/ko/droid_lkm_vendor.ko" ]; then cw=missing; fi
	echo "compat_wanted=$cw"
	if [ -f "$BLOCK" ]; then echo "blocked=1"; else echo "blocked=0"; fi
	if [ -f "$MODDIR/insmod.crash" ]; then echo "crash=1"; else echo "crash=0"; fi
	echo "last=$(cat "$MODDIR/last_result" 2>/dev/null)"
	echo "oplus=$(conf oplus 0)"
	echo "brand=$(conf device_brand -)"
	echo "model=$(conf device_model -)"
	for p in /sys/module/droid_lkm/parameters/*; do
		[ -f "$p" ] || continue
		echo "param.${p##*/}=$(cat "$p" 2>/dev/null)"
	done
	for p in /sys/module/droid_lkm_vendor/parameters/*; do
		[ -f "$p" ] || continue
		echo "cparam.${p##*/}=$(cat "$p" 2>/dev/null)"
	done
	echo "---log---"
	tail -n 25 "$LOG" 2>/dev/null
	exit 0
fi

# 上次停在 insmod 里：置 blocked，本次不再加载
if [ -f "$MARK" ]; then
	mv -f "$MARK" "$MODDIR/insmod.crash" 2>/dev/null
	: > "$BLOCK" 2>/dev/null
	ph_log "previous insmod never returned, blocked"
	exit 0
fi
if [ -f "$BLOCK" ]; then
	ph_log "blocked, skip loading"
	exit 0
fi

# 核心模块
KMI=$(conf kmi -)
ph_log "start: kernel=$(uname -r) kmi=$KMI oplus=$(conf oplus 0)"

core_ok=0
if loaded droid_lkm; then
	ph_log "droid_lkm already loaded"
	core_ok=1
else
	KO=$MODDIR/ko/droid_lkm.ko
	if [ ! -f "$KO" ]; then
		ph_log "ko missing: $KO"
		echo "fail: core ko missing" > "$MODDIR/last_result"
		exit 1
	fi

	ph_log "loading droid_lkm.ko"
	guarded_insmod "$KO" \
		"verbose=$(bool verbose 0)" \
		"gate=$(bool gate 0)" \
		"mqueue=$(bool mqueue 1)" \
		"nspid=$(bool nspid 1)" \
		"nspid_append=$(bool nspid_append 1)" \
		"skip_sysvipc=$(bool skip_sysvipc 0)" \
		"no_fake_ns=$(bool no_fake_ns 0)" \
		"skip_do_exit=$(bool skip_do_exit 0)" \
		"inline_hook=$(bool inline_hook 1)" \
		"write_path=$(bool write_path 0)" \
		"degrade=$(bool degrade 0)"
	rc=$?

	if [ "$rc" -eq 0 ] && loaded droid_lkm; then
		core_ok=1
		ph_log "droid_lkm loaded"
		echo "ok: core" > "$MODDIR/last_result"
	else
		ph_log "droid_lkm failed (rc=$rc)"
		echo "fail: core rc=$rc" > "$MODDIR/last_result"
		dmesg 2>/dev/null | tail -n 15 >> "$LOG"
	fi
fi

[ "$core_ok" = 1 ] || exit 1

# 兼容模块：安装时已按指纹把 compat 解析成 on/off
COMPAT=$MODDIR/ko/droid_lkm_vendor.ko
want_compat=0
case "$(conf compat off)" in
on | 1 | yes | true) want_compat=1 ;;
auto) [ "$(conf oplus 0)" = 1 ] && want_compat=1 ;;
esac

if [ "$want_compat" != 1 ]; then
	ph_log "compat: off (oplus=$(conf oplus 0))"
elif [ ! -f "$COMPAT" ]; then
	ph_log "compat: no build in this package"
elif loaded droid_lkm_vendor; then
	ph_log "droid_lkm_vendor already loaded"
else
	ph_log "loading droid_lkm_vendor.ko"
	guarded_insmod "$COMPAT" \
		"ghost=$(bool ghost 1)" \
		"ghost_match=$(conf ghost_match oplus_)" \
		"pid_max_cap=$(sint pid_max_cap 0)" \
		"inline_hook=$(bool inline_hook 1)"
	rc=$?
	if [ "$rc" -eq 0 ] && loaded droid_lkm_vendor; then
		ph_log "droid_lkm_vendor loaded"
		echo "ok: core+compat" > "$MODDIR/last_result"
	else
		ph_log "droid_lkm_vendor failed (rc=$rc)"
		echo "fail: compat rc=$rc" > "$MODDIR/last_result"
		exit 1
	fi
fi

exit 0

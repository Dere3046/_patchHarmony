#!/system/bin/sh
#
# 安装时：选 KMI、按设备指纹决定 compat、把结果写进 config.conf、只保留对应的 ko

command -v ui_print >/dev/null 2>&1 || ui_print() { echo "$1"; }
command -v abort >/dev/null 2>&1 || abort() {
	ui_print "$1"
	exit 1
}

[ -n "$MODPATH" ] || MODPATH=$(cd "$(dirname "$0")" && pwd)
[ -d "$MODPATH/kernels" ] || abort "! kernels/ missing in this zip"

[ "$ARCH" = "arm64" ] || [ "$(uname -m)" = "aarch64" ] || \
	abort "! arm64 device required (ARCH=${ARCH:-unknown})"

RELEASE=$(uname -r)
MAJOR_MINOR=$(echo "$RELEASE" | sed -n 's/^\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')
ANDROID_TAG=$(echo "$RELEASE" | sed -n 's/.*\(android[0-9][0-9]*\).*/\1/p')

# 设备指纹，结果写进 config
BRAND=$(getprop ro.product.vendor.brand 2>/dev/null)
[ -n "$BRAND" ] || BRAND=$(getprop ro.product.brand 2>/dev/null)
MODEL=$(getprop ro.product.vendor.model 2>/dev/null)
[ -n "$MODEL" ] || MODEL=$(getprop ro.product.model 2>/dev/null)
MANUF=$(getprop ro.product.vendor.manufacturer 2>/dev/null)
[ -n "$MANUF" ] || MANUF=$(getprop ro.product.manufacturer 2>/dev/null)
FP=$(getprop ro.build.fingerprint 2>/dev/null)
[ -n "$FP" ] || FP=$(getprop ro.vendor.build.fingerprint 2>/dev/null)
DEV=$(getprop ro.product.vendor.device 2>/dev/null)
[ -n "$DEV" ] || DEV=$(getprop ro.product.device 2>/dev/null)

OPLUS=0
case "$(printf '%s %s %s %s %s' "$BRAND" "$MANUF" "$MODEL" "$FP" "$DEV" | tr 'A-Z' 'a-z')" in
*oppo* | *oneplus* | *realme* | *oplus*) OPLUS=1 ;;
esac

# 首发 API 等级 -> androidNN；15 起板级等级可能是 202404 这种年月格式
api_tag() {
	case $1 in
	31 | 32) echo android12 ;;
	3[3-9] | 4[0-9]) echo "android$(($1 - 20))" ;;
	20[2-9][0-9][0-9][0-9]) echo "android$((${1%??} - 2009))" ;;
	esac
}

# 内核没带 androidNN 时用首发等级兜底：KMI 跟首发走，系统版本会随 OTA 升上去
TAG_SRC=kernel
if [ -z "$ANDROID_TAG" ]; then
	TAG_SRC=none
	for p in ro.board.first_api_level ro.product.first_api_level; do
		ANDROID_TAG=$(api_tag "$(getprop "$p" 2>/dev/null)")
		[ -n "$ANDROID_TAG" ] && { TAG_SRC=$p; break; }
	done
fi

# 必须 androidNN + 主次版本同时匹配，不跨标签猜
BEST=
TIE=
ALL=

for d in "$MODPATH"/kernels/*; do
	[ -d "$d" ] || continue
	K=${d##*/}
	ALL="$ALL $K"
	K_ANDROID=${K%%-*}
	K_KVER=${K#*-}

	if [ -n "$ANDROID_TAG" ]; then
		[ "$K_ANDROID" = "$ANDROID_TAG" ] || continue
		[ -z "$MAJOR_MINOR" ] || [ "$K_KVER" = "$MAJOR_MINOR" ] || continue
	else
		[ -n "$MAJOR_MINOR" ] && [ "$K_KVER" = "$MAJOR_MINOR" ] || continue
	fi

	if [ -z "$BEST" ]; then
		BEST=$K
	else
		TIE="$TIE $K"
	fi
done

OVERRIDE=$(sed -n 's/^[[:space:]]*kmi_override[[:space:]]*=[[:space:]]*//p' "$MODPATH/config.conf" 2>/dev/null \
	| tail -n 1 | tr -d '\r' | sed 's/[[:space:]]*$//')
if [ -n "$OVERRIDE" ]; then
	[ -d "$MODPATH/kernels/$OVERRIDE" ] || abort "! kmi_override=$OVERRIDE not in this zip"
	BEST=$OVERRIDE
	TIE=
	ui_print "- override : $BEST"
fi

if [ -z "$BEST" ]; then
	abort "! no build matches kernel $RELEASE (android=${ANDROID_TAG:-?} from $TAG_SRC, kver=${MAJOR_MINOR:-?}); this zip has:$ALL
! if you know the right one, set kmi_override in config.conf and repack"
fi
if [ -n "$TIE" ]; then
	abort "! $RELEASE matches multiple builds:$BEST $TIE
! set kmi_override in config.conf to one of them and repack"
fi
if [ "$TAG_SRC" = none ]; then
	ui_print "! no androidNN in kernel string or build props"
	ui_print "  matched by kernel $MAJOR_MINOR only; set kmi_override if load fails"
fi

# 改写 config.conf 的 key，不存在就追加
conf_set() {
	awk -F= -v k="$2" -v v="$3" '
		BEGIN { done = 0 }
		$1 == k { print k "=" v; done = 1; next }
		{ print }
		END { if (!done) print k "=" v }
	' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
}

conf_get() {
	sed -n "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*//p" "$1" 2>/dev/null \
		| tail -n 1 | tr -d '\r' | sed 's/[[:space:]]*$//'
}

conf_set "$MODPATH/config.conf" kmi "$BEST"
conf_set "$MODPATH/config.conf" oplus "$OPLUS"
conf_set "$MODPATH/config.conf" device_brand "$BRAND"
conf_set "$MODPATH/config.conf" device_model "$MODEL"

# auto 在安装时就解析掉，开机只需要读 on/off
if [ "$(conf_get "$MODPATH/config.conf" compat)" = "auto" ]; then
	if [ "$OPLUS" = 1 ]; then
		conf_set "$MODPATH/config.conf" compat on
	else
		conf_set "$MODPATH/config.conf" compat off
	fi
fi

mkdir -p "$MODPATH/ko"
cp "$MODPATH/kernels/$BEST/droid_lkm.ko" "$MODPATH/ko/" || abort "! cannot copy droid_lkm.ko"
if [ -f "$MODPATH/kernels/$BEST/droid_lkm_vendor.ko" ]; then
	cp "$MODPATH/kernels/$BEST/droid_lkm_vendor.ko" "$MODPATH/ko/"
fi
rm -rf "$MODPATH/kernels"

# 不带旧版本的状态
rm -f "$MODPATH/insmod.inflight" "$MODPATH/insmod.crash" "$MODPATH/blocked"

# 只补可执行位，不动 secontext
chmod 0755 "$MODPATH"/*.sh "$MODPATH"/META-INF/com/google/android/update-binary 2>/dev/null
chmod 0644 "$MODPATH"/module.prop "$MODPATH"/config.conf 2>/dev/null
chmod 0755 "$MODPATH"/ko "$MODPATH"/webroot 2>/dev/null
chmod 0644 "$MODPATH"/ko/*.ko 2>/dev/null

ui_print "- device : ${BRAND:-?} ${MODEL:-} (oplus=$OPLUS)"
ui_print "- kernel : $RELEASE"
ui_print "- kmi    : $BEST"
ui_print "- compat : $(conf_get "$MODPATH/config.conf" compat)"
ui_print "- config : \$MODDIR/config.conf"

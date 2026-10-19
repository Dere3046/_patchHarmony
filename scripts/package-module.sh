#!/bin/sh
# 把 out/<kmi>/*.ko 打成单个整包（所有 KMI 都在同一个 zip 里）
#
# 用法:
#   scripts/package-module.sh                       # 自动发现 out/ 下已构建的 KMI
#   scripts/package-module.sh android16-6.12        # 只放指定 KMI
#   scripts/package-module.sh --version v1.0.1 --code 2
#
# 产物:
#   dist/phDroid-<ver>.zip
#
# 安装时 customize.sh 用 uname -r 挑出本机那一份，其余构建直接删除。
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUTDIR=$ROOT/dist
# 暂存目录放在工程内：Windows 上 mktemp 给出的是 C:\... 形式，
# 清理时会被删除守卫当成相对路径处理而失败
STAGE=$ROOT/build/stage
VERSION=
CODE=
KMIS=

usage() {
	sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
	case $1 in
	--version) VERSION=$2; shift 2 ;;
	--version=*) VERSION=${1#*=}; shift ;;
	--code) CODE=$2; shift 2 ;;
	--code=*) CODE=${1#*=}; shift ;;
	--out) OUTDIR=$2; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	-*) echo "unknown option: $1" >&2; exit 1 ;;
	*) KMIS="$KMIS $1"; shift ;;
	esac
done

prop_get() {
	awk -F= -v k="$1" '$1==k {sub(/^[^=]*=/, ""); print; exit}' "$ROOT/module/module.prop"
}
[ -n "$VERSION" ] || VERSION=$(prop_get version)
[ -n "$CODE" ] || CODE=$(prop_get versionCode)
[ -n "$VERSION" ] || { echo "no version" >&2; exit 1; }
[ -n "$CODE" ] || CODE=1

prop_set() {
	awk -F= -v k="$2" -v v="$3" '
		BEGIN { done = 0 }
		$1 == k { print k "=" v; done = 1; next }
		{ print }
		END { if (!done) print k "=" v }
	' "$1" > "$1.tmp"
	mv "$1.tmp" "$1"
}

# 没指定就用 out/ 里真正产出了 droid_lkm.ko 的目录
if [ -z "${KMIS# }" ]; then
	for d in "$ROOT"/out/*/; do
		[ -f "$d/droid_lkm.ko" ] || continue
		KMIS="$KMIS $(basename "$d")"
	done
fi
KMIS=$(echo $KMIS | tr ' ' '\n' | grep -v '^$' | sort | tr '\n' ' ')
[ -n "$KMIS" ] || {
	echo "no built module found under $ROOT/out" >&2
	exit 1
}

# 清空暂存目录：用 find -delete 而不是 rm -rf，
# 后者在部分环境（Windows 回收站守卫）会直接失败
clean_dir() {
	[ -d "$1" ] || return 0
	find "$1" -mindepth 1 -delete 2>/dev/null || rm -rf "$1"
}

stage() { # <kmi>...
	clean_dir "$STAGE"
	mkdir -p "$STAGE"
	cp -a "$ROOT/module/." "$STAGE/"
	mkdir -p "$STAGE/kernels"

	for k in "$@"; do
		[ -f "$ROOT/out/$k/droid_lkm.ko" ] || {
			echo "missing $ROOT/out/$k/droid_lkm.ko" >&2
			exit 1
		}
		mkdir -p "$STAGE/kernels/$k"
		cp "$ROOT/out/$k/droid_lkm.ko" "$STAGE/kernels/$k/"
		# compat 只有部分 KMI 会构建出来（取决于目标内核有没有 nop_mnt_idmap）
		if [ -f "$ROOT/out/$k/droid_lkm_compat.ko" ]; then
			cp "$ROOT/out/$k/droid_lkm_compat.ko" "$STAGE/kernels/$k/"
		fi
	done

	prop_set "$STAGE/module.prop" version "$VERSION"
	prop_set "$STAGE/module.prop" versionCode "$CODE"

	chmod 0755 "$STAGE"/*.sh "$STAGE"/META-INF/com/google/android/update-binary
}

make_zip() { # <zip path>
	mkdir -p "$(dirname "$1")"
	# 不先 rm：部分环境的删除守卫会把“删除不存在的文件”当失败处理；
	# zip 用 -FS 同步，python 写入本身就是截断
	if command -v zip >/dev/null 2>&1; then
		(cd "$STAGE" && zip -qr9 -FS "$1" .)
	else
		# Windows 上的 python 不认 /c/... 形式的路径，先转成 C:\...
		winpath() {
			if command -v cygpath >/dev/null 2>&1; then
				cygpath -w "$1"
			else
				echo "$1"
			fi
		}
		for py in python3 python py; do
			command -v "$py" >/dev/null 2>&1 || continue
			"$py" "$(winpath "$ROOT/scripts/mkzip.py")" \
				"$(winpath "$STAGE")" "$(winpath "$1")"
			break
		done
	fi
	[ -f "$1" ] || { echo "failed to create $1" >&2; exit 1; }
}

echo "version: $VERSION ($CODE)"
echo "kmis   : $KMIS"

stage $KMIS
z="$OUTDIR/phDroid-$VERSION.zip"
make_zip "$z"
echo "  $z"

clean_dir "$STAGE"
rmdir "$STAGE" 2>/dev/null || true
rmdir "$ROOT/build" 2>/dev/null || true
echo "done, see $OUTDIR"

#!/bin/sh
# local DDK build
# usage: build-ddkk.sh <target>
# targets: android12-5.10 android13-5.10 android13-5.15 android14-5.15
#          android14-6.1 android15-6.6 android16-6.12
# VER carries the target into ODIR, ko lands in out/<target>/
set -e

TARGET=${1:-android16-6.12}
IMAGE=docker.cnb.cool/ylarod/ddk/ddk-min:${TARGET}
SRCDIR=$(cd "$(dirname "$0")/.." && pwd)

# TEST=1 builds the selftest probes the QEMU harness reads back, so the variable
# has to reach the container: the compat Ko has no /proc node without it and two
# assertions are skipped instead of run.
TESTARG=""
[ "${TEST:-0}" = "1" ] && TESTARG="-e TEST=1"

docker run --rm ${TESTARG} \
	-e KDIR=/opt/ddk/kdir/${TARGET} \
	-v "$SRCDIR":/src \
	-w /src \
	"$IMAGE" \
	sh -c 'make VER="$1" clean 2>/dev/null; make VER="$1"' sh "$TARGET"

echo "-> ${SRCDIR}/out/${TARGET}"
find "$SRCDIR/out/${TARGET}" -name "*.ko"

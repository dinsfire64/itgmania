#!/bin/sh
set -eu

repo=${1:-/work}
image=${2:-$repo/references/ddrmini-emmc.img}
sysroot=${3:-$repo/Build/ddrmini-sysroot}
build=${4:-$repo/Build/ddrmini-itgmania}
jobs=${DDRMINI_BUILD_JOBS:-2}

[ -f "$image" ] || { echo "DDR Mini image not found: $image" >&2; exit 1; }

if [ ! -d "$sysroot" ]; then
  "$repo/Tools/ddrmini/extract-sysroot.sh" "$image" "$sysroot"
fi

[ -f /opt/ddrmini-dev/usr/include/libudev.h ] || {
  echo "ARM64 libudev development overlay is missing; rebuild the Docker image." >&2
  exit 1
}
[ -e /opt/ddrmini-dev/usr/lib/aarch64-linux-gnu/libudev.so ] || {
  echo "Bullseye ARM64 libudev SDK library is missing; rebuild the Docker image." >&2
  exit 1
}

cmake -S "$repo" -B "$build" \
  -UDDRMINI_UDEV_LIBRARY \
  -DCMAKE_TOOLCHAIN_FILE="$repo/CMake/toolchains/ddrmini-aarch64.cmake" \
  -DDDRMINI_SYSROOT="$sysroot" \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_DDRMINI=ON \
  -DWITH_SSE2=OFF \
  -DWITH_GTK3=OFF \
  -DWITH_XINERAMA=OFF \
  -DWITH_ALSA=OFF \
  -DWITH_PULSEAUDIO=OFF \
  -DWITH_JACK=OFF \
  -DWITH_CRASH_HANDLER=OFF

echo "Building with $jobs parallel job(s)..."
cmake --build "$build" --parallel "$jobs"

binary=$repo/itgmania
echo
echo "DDR Mini bootstrap build complete: $binary"
file "$binary"
aarch64-linux-gnu-readelf -d "$binary" | grep NEEDED || true


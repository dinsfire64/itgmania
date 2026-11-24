#!/bin/sh
set -eu

repo=${1:-/work}
build=${2:-$repo/Build/ddrmini-smoke}
jobs=${DDRMINI_BUILD_JOBS:-2}

case "$repo" in
  /*) ;;
  *) echo "repository path must be absolute: $repo" >&2; exit 2 ;;
esac

cmake -S "$repo/Tools/ddrmini/smoke-test" -B "$build" \
  -DCMAKE_TOOLCHAIN_FILE="$repo/CMake/toolchains/ddrmini-aarch64.cmake" \
  -DDDRMINI_SDL_INCLUDE_DIR=/opt/sdl2-arm64/usr/include/SDL2 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$build" --parallel "$jobs"

binary=$build/ddrmini-smoke

echo
echo "Built: $binary"
file "$binary"

echo
echo "Direct shared-library requirements:"
aarch64-linux-gnu-readelf -d "$binary" | grep NEEDED || true

echo
echo "Required glibc symbol versions:"
aarch64-linux-gnu-readelf --version-info "$binary" \
  | grep -o 'GLIBC_[0-9.]*' \
  | sort -Vu || true

echo
echo "Copy these three files to the USB drive root:"
echo "  $build/DDRmenu"
echo "  $build/monitor_audio.sh"
echo "  $build/ddrmini-smoke"


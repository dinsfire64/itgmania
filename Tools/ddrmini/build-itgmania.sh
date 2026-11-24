#!/bin/sh
set -eu

repo=${1:-/work}
build=${2:-$repo/Build/ddrmini-itgmania}
jobs=${DDRMINI_BUILD_JOBS:-2}

[ -f /opt/ddrmini-dev/usr/include/libudev.h ] || {
  echo "ARM64 libudev development overlay is missing; rebuild the Docker image." >&2
  exit 1
}
[ -e /opt/ddrmini-dev/usr/lib/aarch64-linux-gnu/libudev.so ] || {
  echo "Bullseye ARM64 libudev SDK library is missing; rebuild the Docker image." >&2
  exit 1
}
[ -f /opt/sdl2-arm64/usr/include/SDL2/SDL.h ] || {
  echo "ARM64 SDL2 headers are missing; rebuild the Docker image." >&2
  exit 1
}
[ -f /opt/gles-arm64/usr/include/GLES/gl.h ] || {
  echo "ARM64 OpenGL ES headers are missing; rebuild the Docker image." >&2
  exit 1
}
[ -f /opt/gles-arm64/usr/include/KHR/khrplatform.h ] || {
  echo "Khronos platform header missing; rebuild Docker image." >&2
  exit 1
}
[ -e /opt/sdl2-arm64/usr/lib/aarch64-linux-gnu/libSDL2.so ] || {
  echo "ARM64 SDL2 library is missing; rebuild the Docker image." >&2
  exit 1
}
[ -e /opt/gles-arm64/usr/lib/aarch64-linux-gnu/libGLESv1_CM.so ] || {
  echo "ARM64 OpenGL ES 1 library is missing; rebuild the Docker image." >&2
  exit 1
}
[ -e /opt/gles-arm64/usr/lib/aarch64-linux-gnu/libEGL.so ] || {
  echo "ARM64 EGL library is missing; rebuild the Docker image." >&2
  exit 1
}

cmake -S "$repo" -B "$build" \
  -UDDRMINI_UDEV_LIBRARY \
  -UDDRMINI_SDL_LIBRARY \
  -UDDRMINI_GLES1_LIBRARY \
  -UDDRMINI_EGL_LIBRARY \
  -DCMAKE_TOOLCHAIN_FILE="$repo/CMake/toolchains/ddrmini-aarch64.cmake" \
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


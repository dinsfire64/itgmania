#!/bin/sh
set -eu

usage() {
  echo "usage: $0 DDRMINI-EMMC.IMG OUTPUT-DIRECTORY" >&2
  exit 2
}

[ "$#" -eq 2 ] || usage

image=$1
output=$2

[ -f "$image" ] || {
  echo "image not found: $image" >&2
  exit 1
}

command -v 7z >/dev/null 2>&1 || {
  echo "7z is required" >&2
  exit 1
}

if [ -e "$output" ]; then
  echo "refusing to overwrite existing path: $output" >&2
  exit 1
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/ddrmini-sysroot.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

echo "Extracting the rootfs partition (the source image remains read-only)..."
7z e "$image" 3.rootfs.img "-o$work" -y >/dev/null

mkdir -p "$output"

# The production rootfs has runtime libraries but no development headers.  Keep
# its directory layout so CMake and the dynamic linker see the exact target ABI.
7z x "$work/3.rootfs.img" "-o$output" -y \
  'lib/*' \
  'usr/lib/*' \
  'usr/include/*' \
  'etc/ld.so.conf' \
  'etc/ld.so.conf.d/*' \
  >/dev/null

echo "DDR Mini runtime sysroot extracted to: $output"
echo "Note: the firmware contains no development headers; install a compatible"
echo "AArch64 libc/SDL2 development package or use the matching Yocto SDK."


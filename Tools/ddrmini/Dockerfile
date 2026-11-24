# Debian 11 is deliberately older than the DDR Mini's glibc 2.35 userspace.
# Pin the final official Bullseye image instead of following a mutable tag.
FROM debian:bullseye-20260824-slim

ENV DEBIAN_FRONTEND=noninteractive

# Bullseye reached EOL in 2026. Its final packages live on archive.debian.org,
# whose frozen Release metadata is intentionally past its Valid-Until date.
RUN dpkg --add-architecture arm64 \
 && printf '%s\n' \
      'deb http://archive.debian.org/debian bullseye main' \
      'deb http://archive.debian.org/debian bullseye-updates main' \
      'deb http://archive.debian.org/debian-security bullseye-security main' \
      > /etc/apt/sources.list \
 && printf '%s\n' \
      'Acquire::Check-Valid-Until "false";' \
      'Acquire::Check-Date "false";' \
      > /etc/apt/apt.conf.d/99debian-archive \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      binutils-aarch64-linux-gnu \
      build-essential \
      ca-certificates \
      file \
      git \
      gcc-aarch64-linux-gnu \
      g++-aarch64-linux-gnu \
      libc6-dev-arm64-cross \
      make \
      pkg-config \
      p7zip-full \
      python3-pip \
 && mkdir -p /tmp/sdl2-package /opt/sdl2-arm64 /opt/gles-arm64 /opt/ddrmini-dev \
 && cd /tmp/sdl2-package \
 && apt-get download libsdl2-dev:arm64 \
 && apt-get download libsdl2-2.0-0:arm64 \
 && apt-get download libgles-dev:arm64 \
 && apt-get download libgles1:arm64 \
 && apt-get download libegl-dev:arm64 \
 && apt-get download libegl1:arm64 \
 && apt-get download libglvnd-dev:arm64 \
 && apt-get download libglvnd-core-dev:arm64 \
 && apt-get download libgl-dev:arm64 \
 && apt-get download libudev-dev:arm64 \
 && apt-get download libudev1:arm64 \
 && dpkg-deb --extract ./libsdl2-dev_*_arm64.deb /opt/sdl2-arm64 \
 && dpkg-deb --extract ./libsdl2-2.0-0_*_arm64.deb /opt/sdl2-arm64 \
 && dpkg-deb --extract ./libgles-dev_*_arm64.deb /opt/gles-arm64 \
 && dpkg-deb --extract ./libgles1_*_arm64.deb /opt/gles-arm64 \
 && dpkg-deb --extract ./libegl-dev_*_arm64.deb /opt/gles-arm64 \
 && dpkg-deb --extract ./libegl1_*_arm64.deb /opt/gles-arm64 \
 && dpkg-deb --extract ./libglvnd-dev_*_arm64.deb /opt/gles-arm64 \
 && dpkg-deb --extract ./libglvnd-core-dev_*_arm64.deb /opt/gles-arm64 \
 && dpkg-deb --extract ./libgl-dev_*_arm64.deb /opt/gles-arm64 \
 && dpkg-deb --extract ./libudev-dev_*_arm64.deb /opt/ddrmini-dev \
 && dpkg-deb --extract ./libudev1_*_arm64.deb /opt/ddrmini-dev \
 && cd / \
 && rm -rf /tmp/sdl2-package \
 && python3 -m pip install --no-cache-dir cmake==3.27.9 \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["/bin/bash"]


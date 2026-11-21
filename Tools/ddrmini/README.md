# DDR Mini bring-up tools

These tools build a small SDL/EGL test before changing ITGmania's renderer.
The test uses the same custom SDL2 Mali video path as the stock DDR Mini menu.

## Recommended: build with Docker

Docker keeps the cross compiler and build packages reproducible. From the
repository root, run:

```sh
Tools/ddrmini/docker-build.sh
```

The first run builds the `itgmania-ddrmini-cross:bullseye` image, extracts the
runtime sysroot, compiles the smoke test, and prints its architecture, shared
library dependencies, and required glibc versions. Later runs reuse Docker's
image layers and the extracted sysroot.

The repository is bind-mounted at `/work`. The container runs as your current
UID and GID, so files under `Build/` remain owned by you. The 7.3 GB eMMC image
is never copied into the Docker image or build context.

The USB payload consists of:

```text
Build/ddrmini-smoke/DDRmenu
Build/ddrmini-smoke/monitor_audio.sh
Build/ddrmini-smoke/ddrmini-smoke
```

If Docker reports permission denied for `/var/run/docker.sock`, either use a
rootless Docker installation or add your account to the host's `docker` group:

```sh
sudo usermod -aG docker "$USER"
```

Log out and back in after changing group membership, then confirm access with
`docker info`. Membership in the `docker` group is effectively root-level
access to the host, so only do this on a machine where that is appropriate.

To open an interactive shell in the same environment:

```sh
docker run --rm -it \
  --user "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -v "$PWD:/work" \
  -w /work \
  itgmania-ddrmini-cross:bullseye
```

Inside that shell, rebuild with:

```sh
Tools/ddrmini/build-smoke-test.sh
```

## Manual build without Docker

### 1. Extract the runtime sysroot

The source image is only read.  The output directory must not already exist.

```sh
Tools/ddrmini/extract-sysroot.sh \
  references/ddrmini-emmc.img \
  Build/ddrmini-sysroot
```

The firmware rootfs has runtime libraries but no development headers. On a
Debian/Ubuntu build host, install an AArch64 compiler and compatible headers
(this command needs to be run by an administrator):

```sh
sudo apt install gcc-aarch64-linux-gnu libc6-dev-arm64-cross libsdl2-dev
```

The host `libsdl2-dev` headers are usable for this test because it only calls
APIs present in the target's SDL 2.0.8.  Linking still uses the target SDL
library from the extracted runtime sysroot. The Debian cross compiler uses its
own complete compile sysroot; its older glibc ABI runs on the DDR Mini's glibc
2.35. A matching Yocto SDK is preferable for the eventual ITGmania build and
can be selected with `-DDDRMINI_COMPILER_SYSROOT=/path/to/sdk/sysroot`.

### 2. Build the smoke test

```sh
cmake -S Tools/ddrmini/smoke-test -B Build/ddrmini-smoke \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/CMake/toolchains/ddrmini-aarch64.cmake" \
  -DDDRMINI_SYSROOT="$PWD/Build/ddrmini-sysroot" \
  -DDDRMINI_SDL_INCLUDE_DIR=/usr/include/SDL2 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build Build/ddrmini-smoke --parallel
```

Verify the result before copying it:

```sh
file Build/ddrmini-smoke/ddrmini-smoke
aarch64-linux-gnu-readelf -d Build/ddrmini-smoke/ddrmini-smoke | grep NEEDED
```

It must report an AArch64 executable and a dependency on
`libSDL2-2.0.so.0`.

## Test from USB

Back up any existing `DDRmenu` on the flash drive, then copy all three files
to its root. Preserve the executable permissions. The installed firmware hook
mounts the drive at `/var/run/media/game` and runs `DDRmenu` as root.

`DDRmenu` starts `monitor_audio.sh`, which enables the headphone output,
selects speaker or headphones from the jack state, and maps the physical
volume knob to the DAC level. It then starts `ddrmini-smoke` and stops the
monitor when the application exits. This launcher can later be reused for
ITGmania.

The screen shows a live dashboard with the SDL video driver, renderer, audio
status, joystick count, and latest input event while a quiet 440 Hz tone
plays. A persistent log is written directly to the flash drive as:

```text
ddrmini-smoke.log
```

The firmware also captures standard output and error in:

```text
/var/log/initapp/usbloader/log
```

The program deliberately exits with status 1 because firmware interprets
status 0 or 42 as a reboot/shutdown request.

## ITGmania ARM64 bootstrap build

After the smoke test succeeds, compile the full application core with the
DDR Mini bootstrap target:

```sh
Tools/ddrmini/docker-build-itgmania.sh
```

The build defaults to two parallel compiler jobs to limit peak memory use.
Override it from the host when needed:

```sh
DDRMINI_BUILD_JOBS=1 Tools/ddrmini/docker-build-itgmania.sh
DDRMINI_BUILD_JOBS=4 Tools/ddrmini/docker-build-itgmania.sh
```

This stage deliberately builds ITGmania with its null display and null sound
drivers. Its purpose is to resolve all general AArch64 and bundled-dependency
build failures before introducing the SDL/EGL/GLES renderer and ALSA driver.
The output is `itgmania` in the repository root; it is not yet the playable
USB payload.

### USB directory layout

Use the production launcher in `Tools/ddrmini/runtime/DDRmenu` with this flash
drive layout:

```text
USB root/
├── DDRmenu
└── itgmania/
    ├── itgmania
    ├── Announcers/
    ├── BGAnimations/
    ├── Data/
    ├── NoteSkins/
    ├── Program/
    ├── Scripts/
    ├── Songs/
    ├── Themes/
    └── Logs/
```

The launcher controls speaker/headphone routing and the physical volume knob,
then runs `USB root/itgmania/itgmania` with that directory as its working
directory. Launcher and audio logs are written under `itgmania/Logs/`.


# DDR Mini bring-up tools

These tools build a small SDL/EGL test before changing ITGmania's renderer.
The test uses the same custom SDL2 Mali video path as the stock DDR Mini menu.

## Recommended: build with Docker

Docker keeps the cross compiler and build packages reproducible. From the
repository root, run:

```sh
Tools/ddrmini/docker-build.sh
```

The first run builds the `itgmania-ddrmini-cross:bullseye` image, compiles the
smoke test, and prints its architecture, shared-library dependencies, and
required glibc versions. Later runs reuse Docker's image layers.

The repository is bind-mounted at `/work`. The container runs as your current
UID and GID, so files under `Build/` remain owned by you. The build does not
need a firmware image or files extracted from the console.

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

The DDR Mini build links against the ARM64 SDL2, EGL, and OpenGL ES 1
development libraries packaged in the Docker image.
After changes to the Dockerfile (including the GLES renderer addition), rebuild
the image and the application with:

```sh
docker build --no-cache -t itgmania-ddrmini-cross:bullseye Tools/ddrmini
rm -rf Build/ddrmini-itgmania
DDRMINI_BUILD_JOBS=1 Tools/ddrmini/docker-build-itgmania.sh
```

The renderer tries `ddrmini` first and retains `null` as a fallback.  A working
GLES initialization is recorded in `Logs/log.txt` as `Current renderer: DDR
Mini GLES`, followed by the SDL video driver and Mali GLES vendor/version.

The DDR Mini target uses the firmware SDL2 audio backend at 48 kHz stereo and
keeps the null driver as a fallback.  The runtime launcher must continue to run
its audio monitor so the codec, speaker/headphone route, and DAC volume are
configured.  Successful initialization is logged as `Sound driver:
DDRMiniSDL`. The output is `itgmania` in the repository root and can replace
`itgmania/itgmania` in the USB payload.

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


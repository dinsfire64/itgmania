# DDR Mini build tools

## Build with Docker

Docker keeps the cross compiler and build packages reproducible. From the
repository root, run:

```sh
Tools/ddrmini/docker-build-itgmania.sh
```

The first run builds the `itgmania-ddrmini-cross:bullseye` image and compiles
ITGmania. Later runs reuse Docker's image layers.

The repository is bind-mounted at `/work`. The container runs as your current
UID and GID, so files under `Build/` remain owned by you. The build does not
need a firmware image or files extracted from the console.

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

Inside that shell, rebuild with `Tools/ddrmini/build-itgmania.sh`.

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


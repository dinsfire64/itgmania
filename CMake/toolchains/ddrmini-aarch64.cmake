set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(DDRMINI_DEV_ROOT "/opt/ddrmini-dev" CACHE PATH
    "Cross-development header and linker-stub overlay")
set(DDRMINI_SDL_ROOT "/opt/sdl2-arm64" CACHE PATH "ARM64 SDL2 overlay")
set(DDRMINI_GLES_ROOT "/opt/gles-arm64" CACHE PATH "ARM64 GLES overlay")
set(CMAKE_FIND_ROOT_PATH
  "${DDRMINI_DEV_ROOT}"
  "${DDRMINI_SDL_ROOT}"
  "${DDRMINI_GLES_ROOT}")

# External projects such as libusb are configured independently and receive
# these flags through sm_get_forwarded_toolchain_args.
set(CMAKE_C_FLAGS_INIT "-I${DDRMINI_DEV_ROOT}/usr/include")
set(CMAKE_CXX_FLAGS_INIT "-I${DDRMINI_DEV_ROOT}/usr/include")

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc CACHE FILEPATH "AArch64 C compiler")
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++ CACHE FILEPATH "AArch64 C++ compiler")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Do not try to execute an AArch64 test program while configuring on x86_64.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)


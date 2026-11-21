set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Preserve custom toolchain inputs inside CMake's compiler test projects.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  DDRMINI_SYSROOT
  DDRMINI_COMPILER_SYSROOT)

set(DDRMINI_SYSROOT "" CACHE PATH "Extracted DDR Mini runtime sysroot")
if(NOT DDRMINI_SYSROOT)
  message(FATAL_ERROR "Set DDRMINI_SYSROOT to the extracted DDR Mini sysroot")
endif()

set(DDRMINI_DEV_ROOT "/opt/ddrmini-dev" CACHE PATH
    "Cross-development header and linker-stub overlay")
set(CMAKE_FIND_ROOT_PATH "${DDRMINI_SYSROOT}" "${DDRMINI_DEV_ROOT}")

# External projects such as libusb are configured independently and receive
# these flags through sm_get_forwarded_toolchain_args.
set(CMAKE_C_FLAGS_INIT "-I${DDRMINI_DEV_ROOT}/usr/include")
set(CMAKE_CXX_FLAGS_INIT "-I${DDRMINI_DEV_ROOT}/usr/include")

# DDRMINI_SYSROOT is a runtime-library root extracted from the firmware; it
# intentionally has no headers.  Debian's cross compiler uses its own complete,
# backward-compatible build sysroot by default.  A matching Yocto SDK may set
# this separately when one is available.
set(DDRMINI_COMPILER_SYSROOT "" CACHE PATH "Optional complete Yocto SDK sysroot")
if(DDRMINI_COMPILER_SYSROOT)
  set(CMAKE_SYSROOT "${DDRMINI_COMPILER_SYSROOT}")
endif()

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc CACHE FILEPATH "AArch64 C compiler")
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++ CACHE FILEPATH "AArch64 C++ compiler")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Do not try to execute an AArch64 test program while configuring on x86_64.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)


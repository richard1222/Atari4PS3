# CMake toolchain file: cross-compile Hatari's libretro core for PS3
# (PPU, PSL1GHT). Hatari's own CMakeLists already handles the UAE CPU
# table generators when CMAKE_CROSSCOMPILING is set (build68k/gencpu
# are built with the host `cc` in custom commands), so a plain
# toolchain file is enough -- no host staging build needed.
#
# Configure with (from the project root, build dir outside the tree):
#   cmake -S cores/hatari -B build/hatari \
#     -DCMAKE_TOOLCHAIN_FILE=shim/hatari-ps3-toolchain.cmake \
#     -DENABLE_HATARI=OFF -DENABLE_LIBRETRO=ON -DLIBRETRO_STATIC=ON \
#     -DENABLE_TOOLS=OFF -DENABLE_TRACING=OFF \
#     -DLIBRETRO_INCLUDE_DIR=<abs path>/cores/hatari/src/retro/libretro-common/include \
#     -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/hatari -j8
# Output: build/hatari/src/hatari_libretro.a (name per Hatari's rules;
# the Makefile copies whatever .a the build produces -- see below).
#
# NOTE: verify the produced archive name on the first build; Hatari's
# libretro target name has changed across versions. The project
# Makefile globs build/hatari/src/*.a and fails loudly if the count
# is not exactly one.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc64)

set(PS3DEV "/opt/ps3sdk" CACHE PATH "PSL1GHT install")

set(CMAKE_C_COMPILER "${PS3DEV}/ppu/bin/ppu-gcc")
set(CMAKE_CXX_COMPILER "${PS3DEV}/ppu/bin/ppu-g++")
set(CMAKE_AR "${PS3DEV}/ppu/bin/ppu-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${PS3DEV}/ppu/bin/ppu-ranlib" CACHE FILEPATH "Ranlib")

# -U__STRICT_ANSI__: newlib hides C99 declarations under strict ANSI
# (same fix as the rest of the project). -Wno-cpp silences the warning
# the -U itself triggers. Cell CPU + hard float, matching the other
# builds. No -include shim: Hatari is C and the gthreads/C99 shims are
# C++-only; with __STRICT_ANSI__ undefined, newlib exposes the C99
# declarations natively.
set(CMAKE_C_FLAGS_INIT
    "-mcpu=cell -mhard-float -U__STRICT_ANSI__ -Wno-cpp -I${PS3DEV}/ppu/include -D__PS3__ -D__PSL1GHT__ -I${CMAKE_CURRENT_LIST_DIR}/fenv")
set(CMAKE_CXX_FLAGS_INIT
    "-mcpu=cell -mhard-float -U__STRICT_ANSI__ -Wno-cpp -I${PS3DEV}/ppu/include -D__PS3__ -D__PSL1GHT__ -I${CMAKE_CURRENT_LIST_DIR}/fenv")

# Never search the host for programs (the host `cc` used by the UAE
# generator commands runs as a plain shell command, unaffected by
# this); search the SDK root for libraries/headers.
set(CMAKE_FIND_ROOT_PATH "${PS3DEV}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# try_compile() links but never runs: a PS3 ELF links fine under
# ppu-gcc, and CMAKE_CROSSCOMPILING already stops CMake from trying
# to execute the test binaries.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Atari4PS3 M2 — blades launcher + one statically-linked runner SELF
# per system in a single PKG.
# Build under WSL CodexPS3Build with /opt/ps3sdk, e.g.:
#   wsl -d CodexPS3Build env PATH=/opt/ps3sdk/bin:/opt/ps3sdk/ppu/bin:/opt/ps3sdk/spu/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
#     PSL1GHT=/opt/ps3sdk PS3DEV=/opt/ps3sdk make -C <this dir> pkg
#
# Core builds: vendored trees are NEVER modified. The template
# libretro Makefiles (atari800/prosystem/handy/virtualjaguar) build
# their flags with `+=`, and a command-line CFLAGS would REPLACE them
# (dropping -D__PS3__ etc.), so the PS3 fixes ride along via compiler
# wrapper scripts (shim/wrap/) passed as CC/CXX. Stella keeps its M0
# CPPFLAGS injection (its Makefile honors CPPFLAGS). Hatari builds
# via CMake + the PS3 toolchain file. DOSBox Pure has no PS3 platform
# block, so its full CFLAGS set is passed on the command line
# (replicating its own defaults + the PS3 fixes).

PS3DEV ?= /opt/ps3sdk
PSL1GHT ?= $(PS3DEV)
PORTLIBS := $(PSL1GHT)/portlibs/ppu

APPID := ATAR00001
TITLE := Atari4PS3
CONTENTID := UP0001-$(APPID)_00-0000000000000000

ROOT := $(CURDIR)
OUT := $(ROOT)/build

CXX := $(PS3DEV)/ppu/bin/ppu-g++
PPU_AR := $(PS3DEV)/ppu/bin/ppu-ar
WRAP_CC := $(ROOT)/shim/wrap/ppu-gcc-wrap
WRAP_CXX := $(ROOT)/shim/wrap/ppu-gxx-wrap

CXXFLAGS := -std=c++17 -U__STRICT_ANSI__ -Wno-cpp -O2 -Wall -Wextra -mcpu=cell -mhard-float \
  -I$(PSL1GHT)/ppu/include -I$(PORTLIBS)/include/SDL2 -I$(PORTLIBS)/include \
  -I$(ROOT)/src \
  -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS := -L$(PSL1GHT)/ppu/lib -L$(PORTLIBS)/lib -Wl,--gc-sections
LDLIBS := -lSDL2 -lz -lgcm_sys -lrsx -lsysutil -lio -laudio -lrt -llv2 -lsysmodule -lsysfs -lm

# Runners: directory name == SELF base name == core key in the
# launcher's blade table, except dosbox-pure (SELF dosbox-pure.self).
# If a core will not build, remove its name from RUNNERS (and its lib
# from ALL_CORE_LIBS) explicitly, with a comment recording the
# evidence -- the launcher shows "Core not built" for that blade.
# Never fake a SELF.
RUNNERS := stella atari800 prosystem handy hatari virtualjaguar dosbox-pure

# --- core libraries (vendored, unmodified) ---
STELLA_LIB := $(ROOT)/cores/stella/src/os/libretro/stella2023_libretro_psl1ght.a
ATARI800_LIB := $(ROOT)/cores/atari800/atari800_libretro_psl1ght.a
PROSYSTEM_LIB := $(ROOT)/cores/prosystem/prosystem_libretro_psl1ght.a
HANDY_LIB := $(ROOT)/cores/handy/handy_libretro_psl1ght.a
VJ_LIB := $(ROOT)/cores/virtualjaguar/virtualjaguar_libretro_psl1ght.a
HATARI_LIB := $(OUT)/hatari/src/hatari_libretro.a
DOSBOX_LIB := $(ROOT)/cores/dosbox-pure/dosbox_pure_libretro_psl1ght.a
ALL_CORE_LIBS := $(STELLA_LIB) $(ATARI800_LIB) $(PROSYSTEM_LIB) $(HANDY_LIB) $(HATARI_LIB) $(VJ_LIB) $(DOSBOX_LIB)

# The ps3toolchain libstdc++ has no gthreads: C++ core builds need the
# shim/std shadow headers (<mutex>/<thread>/<condition_variable>) plus
# the C99 forced-include, and the PSL1GHT include dir for <sys/*.h>.
# See shim/README.md. The launcher/runner builds deliberately do NOT
# get these (except the dosbox-pure pthread shim object, below).
$(STELLA_LIB):
	$(MAKE) -C $(ROOT)/cores/stella/src/os/libretro platform=psl1ght PS3DEV=$(PS3DEV) \
	  CPPFLAGS="-mcpu=cell -mhard-float -U__STRICT_ANSI__ -Wno-cpp -I$(ROOT)/shim/std -I$(PS3DEV)/ppu/include -include $(ROOT)/shim/ps3_c99_shim.h" -j8

$(ATARI800_LIB):
	$(MAKE) -C $(ROOT)/cores/atari800 platform=psl1ght PS3DEV=$(PS3DEV) \
	  CC=$(WRAP_CC) CC_AS=$(WRAP_CC) CXX=$(WRAP_CXX) AR=$(PPU_AR) -j8

$(PROSYSTEM_LIB):
	$(MAKE) -C $(ROOT)/cores/prosystem platform=psl1ght PS3DEV=$(PS3DEV) \
	  CC=$(WRAP_CC) CC_AS=$(WRAP_CC) CXX=$(WRAP_CXX) AR=$(PPU_AR) -j8

$(HANDY_LIB):
	$(MAKE) -C $(ROOT)/cores/handy platform=psl1ght PS3DEV=$(PS3DEV) \
	  CC=$(WRAP_CC) CC_AS=$(WRAP_CC) CXX=$(WRAP_CXX) AR=$(PPU_AR) \
	  FRONTEND_SUPPORTS_XRGB8888=1 FRONTEND_SUPPORTS_RGB565=1 -j8

$(VJ_LIB):
	$(MAKE) -C $(ROOT)/cores/virtualjaguar platform=psl1ght PS3DEV=$(PS3DEV) \
	  CC=$(WRAP_CC) CC_AS=$(WRAP_CC) CXX=$(WRAP_CXX) AR=$(PPU_AR) -j8

# Hatari: CMake cross-build. The UAE CPU table generators are built
# with the host `cc` by Hatari's own CMake rules when cross-compiling.
# The archive lands at build/hatari/src/hatari_libretro.a (PREFIX ""
# + OUTPUT_NAME hatari_libretro). Re-running configure is cheap and
# keeps the build dir consistent if the toolchain file changes.
$(HATARI_LIB):
	cmake -S $(ROOT)/cores/hatari -B $(OUT)/hatari \
	  -DCMAKE_TOOLCHAIN_FILE=$(ROOT)/shim/hatari-ps3-toolchain.cmake \
	  -DCMAKE_C_FLAGS="-frounding-math -mcpu=cell -mhard-float -U__STRICT_ANSI__ -Wno-cpp -I$(PS3DEV)/ppu/include -I$(ROOT)/shim/fenv -D__PS3__ -D__PSL1GHT__" \
	  -DENABLE_HATARI=OFF -DENABLE_LIBRETRO=ON -DLIBRETRO_STATIC=ON \
	  -DENABLE_TOOLS=OFF -DENABLE_TRACING=OFF \
	  -DLIBRETRO_INCLUDE_DIR=$(ROOT)/cores/hatari/src/retro/libretro-common/include \
	  -DCMAKE_BUILD_TYPE=Release
	cmake --build $(OUT)/hatari -j8

# DOSBox Pure: no ps3/psl1ght platform block. The generic Makefile
# builds CFLAGS with := then +=, so a command-line CFLAGS replaces
# the whole set -- replicate its RELEASE defaults here, plus the PS3
# fixes, the pthread shadow header (dbp_threads.h uses real
# pthread_* calls; the sysroot pthread.h is a non-working stub), and
# the legacy <memory.h> shim (newlib has none).
DOSBOX_CFLAGS := -DNDEBUG -O2 -fno-ident -std=gnu++11 -fomit-frame-pointer -fexceptions \
  -Wno-address-of-packed-member -Wno-format -Wno-switch -Wno-psabi \
  -DSTATIC_LINKING -fvisibility=hidden -ffunction-sections \
  -D__LIBRETRO__ -Iinclude -D_FILE_OFFSET_BITS=64 \
  -mcpu=cell -mhard-float -U__STRICT_ANSI__ -Wno-cpp -D__PS3__ -D__PSL1GHT__ \
  -I$(ROOT)/shim/std -I$(ROOT)/shim/pthread -I$(ROOT)/shim/compat -I$(PS3DEV)/ppu/include \
  -include $(ROOT)/shim/ps3_c99_shim.h
$(DOSBOX_LIB):
	$(MAKE) -C $(ROOT)/cores/dosbox-pure \
	  CXX=$(PS3DEV)/ppu/bin/ppu-g++ AR=$(PPU_AR) STATIC_LINKING=1 \
	  OUTNAME=dosbox_pure_libretro_psl1ght.a CFLAGS="$(DOSBOX_CFLAGS)" -j8

# --- launcher / runner objects ---
# Common objects are core-independent (they never use the per-runner
# -D defines), so one set is shared by the launcher and all runners.
# font_text.cxx is shared too: the runner uses the atlas for its
# BIOS-missing / performance-warning message screens.
COMMON_SRCS := src/common/log.cxx src/common/fsutil.cxx src/common/pad.cxx src/common/spawn.cxx src/common/sysutil.cxx src/common/watchdog.cxx src/launcher/font_text.cxx
LAUNCHER_SRCS := src/launcher/main.cxx

COMMON_OBJS := $(COMMON_SRCS:%.cxx=$(OUT)/%.o)
LAUNCHER_OBJS := $(LAUNCHER_SRCS:%.cxx=$(OUT)/%.o) $(COMMON_OBJS)

# Per-runner -D overrides (see common/paths.h for what each means).
# String values need shell double quotes inside the make recipe.
RUNNER_DEFS_stella := -DA4_CORE_NAME='"stella"' -DA4_SAVE_DIR=A4_SAVES_2600
RUNNER_DEFS_atari800 := -DA4_CORE_NAME='"atari800"' -DA4_SAVE_DIR=A4_SAVES_5200 -DA4_HINT_KEY='"atari800_system"'
RUNNER_DEFS_prosystem := -DA4_CORE_NAME='"prosystem"' -DA4_SAVE_DIR=A4_SAVES_7800
RUNNER_DEFS_handy := -DA4_CORE_NAME='"handy"' -DA4_SAVE_DIR=A4_SAVES_LYNX
RUNNER_DEFS_hatari := -DA4_CORE_NAME='"hatari"' -DA4_SAVE_DIR=A4_SAVES_ST -DA4_BIOS_FILE='"tos.img"' -DA4_BIOS_REQUIRED=1 -DA4_CORE_VARS='"hatari_timerd=enabled;hatari_fastboot=enabled;hatari_ttram_size=0"'
RUNNER_DEFS_virtualjaguar := -DA4_CORE_NAME='"virtualjaguar"' -DA4_SAVE_DIR=A4_SAVES_JAGUAR -DA4_WARN_SLOW=1
RUNNER_DEFS_dosbox-pure := -DA4_CORE_NAME='"dosbox-pure"' -DA4_SAVE_DIR=A4_SAVES_PC

# Per-runner libretro.h include dirs (each core vendors its own copy).
RUNNER_INC_stella := $(ROOT)/cores/stella/src/os/libretro
RUNNER_INC_atari800 := $(ROOT)/cores/atari800/libretro/libretro-common/include
RUNNER_INC_prosystem := $(ROOT)/cores/prosystem/libretro-common/include
RUNNER_INC_handy := $(ROOT)/cores/handy/libretro-common/include
RUNNER_INC_hatari := $(ROOT)/cores/hatari/src/retro/libretro-common/include
RUNNER_INC_virtualjaguar := $(ROOT)/cores/virtualjaguar/libretro-common/include
RUNNER_INC_dosbox-pure := $(ROOT)/cores/dosbox-pure/libretro-common/include

RUNNER_LIB_stella := $(STELLA_LIB)
RUNNER_LIB_atari800 := $(ATARI800_LIB)
RUNNER_LIB_prosystem := $(PROSYSTEM_LIB)
RUNNER_LIB_handy := $(HANDY_LIB)
RUNNER_LIB_hatari := $(HATARI_LIB)
RUNNER_LIB_virtualjaguar := $(VJ_LIB)
RUNNER_LIB_dosbox-pure := $(DOSBOX_LIB)

# pthread shim object: linked ONLY into the dosbox-pure runner.
PTHREAD_OBJ := $(OUT)/shim/pthread_ps3.o
$(PTHREAD_OBJ): $(ROOT)/shim/pthread/pthread_ps3.cxx $(ROOT)/shim/pthread/pthread.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(ROOT)/shim/pthread -c $< -o $@

# ftime shim object: linked ONLY into the dosbox-pure runner.
FTIME_OBJ := $(OUT)/shim/ftime_ps3.o
$(FTIME_OBJ): $(ROOT)/shim/compat/ftime_ps3.cxx
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# fenv shim object: linked ONLY into the hatari runner.
FENV_OBJ := $(OUT)/shim/fenv_ps3.o
$(FENV_OBJ): $(ROOT)/shim/fenv/fenv_ps3.cxx $(ROOT)/shim/fenv/fenv.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(ROOT)/shim/fenv -c $< -o $@

# libretro-common helpers: atari800, handy and virtualjaguar exclude
# these from their archives under STATIC_LINKING=1, assuming the
# frontend (RetroArch) provides them. This standalone runner does
# not, so the helpers are compiled here, from each core's OWN
# libretro-common tree (versions differ between cores), as extra
# runner objects. The vendored trees are read, never modified. The
# lists below are exactly the lists each core's Makefile.common
# builds for the shared-core case. ProSystem needs none of this: its
# sources are self-contained (verified: no libretro-common sources
# in its Makefile.common, and its runner links without them).
A800_HELPER_DIR := $(ROOT)/cores/atari800/libretro/libretro-common
A800_HELPERS := compat/compat_strl.c compat/compat_strcasestr.c compat/fopen_utf8.c encodings/encoding_utf.c file/file_path.c file/file_path_io.c string/stdstring.c time/rtime.c vfs/vfs_implementation.c
A800_HELPER_OBJS := $(A800_HELPERS:%.c=$(OUT)/compat/atari800/%.o)
$(OUT)/compat/atari800/%.o: $(A800_HELPER_DIR)/%.c
	@mkdir -p $(@D)
	$(WRAP_CC) -c -O2 -D__LIBRETRO__ -D__PS3__ -D__PSL1GHT__ -I$(A800_HELPER_DIR)/include -I$(ROOT)/cores/atari800 -o $@ $<
$(OUT)/runners/atari800/runner.elf: $(A800_HELPER_OBJS)

HANDY_HELPER_DIR := $(ROOT)/cores/handy/libretro-common
HANDY_HELPERS := compat/compat_posix_string.c compat/compat_snprintf.c compat/compat_strcasestr.c compat/compat_strl.c compat/fopen_utf8.c encodings/encoding_utf.c file/file_path.c file/file_path_io.c streams/file_stream.c streams/file_stream_transforms.c string/stdstring.c time/rtime.c vfs/vfs_implementation.c
HANDY_HELPER_OBJS := $(HANDY_HELPERS:%.c=$(OUT)/compat/handy/%.o)
# Handy's libretro-common tree is older and lacks
# defines/ps3_defines.h, which its vfs_implementation.c includes
# under __PS3__. ProSystem's newer tree has it; that include dir is
# listed SECOND so only headers missing from Handy's tree resolve
# from it (in practice, just ps3_defines.h).
$(OUT)/compat/handy/%.o: $(HANDY_HELPER_DIR)/%.c
	@mkdir -p $(@D)
	$(WRAP_CC) -c -O2 -D__LIBRETRO__ -D__PS3__ -D__PSL1GHT__ -I$(HANDY_HELPER_DIR)/include -I$(ROOT)/cores/prosystem/libretro-common/include -I$(ROOT)/cores/handy -o $@ $<
$(OUT)/runners/handy/runner.elf: $(HANDY_HELPER_OBJS)

VJ_HELPER_DIR := $(ROOT)/cores/virtualjaguar/libretro-common
VJ_HELPERS := compat/compat_strcasestr.c encodings/encoding_utf.c compat/compat_snprintf.c compat/compat_strl.c compat/compat_posix_string.c compat/fopen_utf8.c streams/file_stream.c streams/file_stream_transforms.c string/stdstring.c vfs/vfs_implementation.c file/file_path.c file/file_path_io.c time/rtime.c
VJ_HELPER_OBJS := $(VJ_HELPERS:%.c=$(OUT)/compat/virtualjaguar/%.o)
$(OUT)/compat/virtualjaguar/%.o: $(VJ_HELPER_DIR)/%.c
	@mkdir -p $(@D)
	$(WRAP_CC) -c -O2 -D__LIBRETRO__ -D__PS3__ -D__PSL1GHT__ -I$(VJ_HELPER_DIR)/include -I$(ROOT)/cores/virtualjaguar -o $@ $<
$(OUT)/runners/virtualjaguar/runner.elf: $(VJ_HELPER_OBJS)

$(OUT)/%.o: %.cxx
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Per-runner main.o: target-specific variables select the defines +
# include dir. $* is the runner name (stella, atari800, ...).
$(OUT)/runners/%/main.o: src/runner/main.cxx
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(RUNNER_DEFS_$*) -I$(RUNNER_INC_$*) -c $< -o $@

RUNNER_ELFS := $(RUNNERS:%=$(OUT)/runners/%/runner.elf)

# Each runner elf depends on its own core lib only (the stem $* is
# available in pattern-rule prerequisites), so one runner can be
# built and checked without waiting for every core. The link line
# filters the .a file out of $^ and adds it back inside the group.
.SECONDEXPANSION:
$(OUT)/runners/%/runner.elf: $(OUT)/runners/%/main.o $(COMMON_OBJS) $$(RUNNER_LIB_$$*)
	$(CXX) $(LDFLAGS) $(filter-out %.a,$^) -Wl,--start-group $(RUNNER_LIB_$*) -Wl,--end-group $(LDLIBS) -o $@

# The dosbox-pure runner additionally links the pthread and ftime
# shim objects, and the hatari runner the fenv shim object.
$(OUT)/runners/dosbox-pure/runner.elf: $(PTHREAD_OBJ) $(FTIME_OBJ)
$(OUT)/runners/hatari/runner.elf: $(FENV_OBJ)

$(OUT)/launcher.elf: $(LAUNCHER_OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

PKGDIR := $(OUT)/pkgdir

# Keep object files: without this, make deletes pattern-rule
# intermediates (notably runners/*/main.o) after linking, forcing a
# full recompile on every build.
.SECONDARY: $(COMMON_OBJS) $(LAUNCHER_OBJS) $(RUNNERS:%=$(OUT)/runners/%/main.o) $(PTHREAD_OBJ) $(FTIME_OBJ) $(FENV_OBJ) $(A800_HELPER_OBJS) $(HANDY_HELPER_OBJS) $(VJ_HELPER_OBJS)

.PHONY: all pkg clean core-libs
all: $(OUT)/launcher.elf $(RUNNER_ELFS)
pkg: $(OUT)/atari4ps3-m0.pkg
core-libs: $(ALL_CORE_LIBS)

$(OUT)/atari4ps3-m0.pkg: $(OUT)/launcher.elf $(RUNNER_ELFS)
	rm -rf $(PKGDIR) && mkdir -p $(PKGDIR)/USRDIR/cores
	cp $(ROOT)/assets/ICON0.PNG $(PKGDIR)/ICON0.PNG
	cp $(ROOT)/assets/PIC1.PNG $(PKGDIR)/PIC1.PNG
	ppu-strip $(OUT)/launcher.elf -o $(OUT)/launcher.strip.elf
	sprxlinker $(OUT)/launcher.strip.elf
	make_self_npdrm $(OUT)/launcher.strip.elf $(PKGDIR)/USRDIR/EBOOT.BIN $(CONTENTID) >/dev/null
	set -e; for c in $(RUNNERS); do \
	  ppu-strip $(OUT)/runners/$$c/runner.elf -o $(OUT)/runners/$$c/runner.strip.elf; \
	  sprxlinker $(OUT)/runners/$$c/runner.strip.elf; \
	  make_self $(OUT)/runners/$$c/runner.strip.elf $(PKGDIR)/USRDIR/cores/$$c.self >/dev/null; \
	done
	sfo --title "$(TITLE)" --appid "$(APPID)" -f $(PS3DEV)/bin/sfo.xml $(PKGDIR)/PARAM.SFO
	pkg --contentid $(CONTENTID) $(PKGDIR)/ $@ >/dev/null
	cp $@ $(basename $@).gnpdrm.pkg
	package_finalize $(basename $@).gnpdrm.pkg

clean:
	rm -rf $(OUT)

-include $(LAUNCHER_OBJS:.o=.d) $(COMMON_OBJS:.o=.d) $(RUNNERS:%=$(OUT)/runners/%/main.d) $(PTHREAD_OBJ:.o=.d)

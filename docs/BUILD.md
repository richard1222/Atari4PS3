# Build notes

The build expects a PSL1GHT toolchain, PPU GCC/G++, SDL2 and zlib portlibs. Set `PS3DEV` and `PSL1GHT` to the SDK prefix; put its `bin`, `ppu/bin` and `spu/bin` on PATH.

Initialize submodules with `git submodule update --init --recursive`. Exact top-level core revisions are pinned in `cores.lock.json`.

`make core-libs` builds upstream static archives. `make all` links the launcher and seven runners. `make pkg` strips and converts the ELFs to SELFs, constructs the package and finalizes its GN-PDRM copy. The package loop stops on a failed tool command.

## Compatibility layers

- `shim/std/`: the threading support needed by the PS3 libstdc++ build.
- `shim/pthread/`: LV2-backed pthread operations used by DOSBox Pure.
- `shim/compat/memory.h`: legacy memory declarations from the SDK�s real `string.h`.
- `shim/compat/ftime_ps3.cxx`: DOSBox wall-clock milliseconds from `gettimeofday`.
- `shim/fenv/`: real PPU rounding control. The missing `log2l` delegates to `log2` only with a compile-time check that this SDK�s double and long double formats have equal size and precision. Unsupported exception APIs are not faked.
- `shim/wrap/`: compiler flags without editing the upstream core trees.

Some static cores omit libretro-common frontend helpers; the Makefile compiles the matching helper sources from each core�s own tree. Handy�s missing PS3 definitions are supplied by the newer ProSystem include tree. Runner archive prerequisites use GNU make secondary expansion.

The software SDL renderer is preferred, following earlier hardware observations. Timer callbacks and input polling continue while an idle launcher avoids unnecessary presents. Current hardware verification is still required.

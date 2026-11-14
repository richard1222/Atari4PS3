# Atari4PS3

An Atari launcher and emulator frontend for PS3 homebrew, with a blades-style dashboard and one statically linked libretro runner per core.

![Atari4PS3 package artwork](assets/PIC1.PNG)

**In development.** The launcher, seven runners and a test PKG build locally. The latest changes still need testing on a PS3; this is not a finished release.

## Systems

| System | Core | ROM folder under `/dev_hdd0/atari/` |
| --- | --- | --- |
| Atari 2600 | Stella | `roms/2600/` |
| Atari 5200 | Atari800 | `roms/5200/` |
| Atari 7800 | ProSystem | `roms/7800/` |
| Atari Lynx | Handy | `roms/lynx/` |
| Atari 8-bit computers | Atari800 | `roms/8bit/` |
| Atari ST | Hatari | `roms/st/` |
| Atari Jaguar | Virtual Jaguar | `roms/jaguar/` |
| Atari PC / DOS | DOSBox Pure | `roms/pc/` |

Supply your own game files. Atari ST requires `tos.img` in `/dev_hdd0/atari/bios/`. Jaguar performance is experimental. No ROMs or BIOS files are bundled or downloaded.

## Controls

- Left/Right: change blades. Up/Down: select a game.
- X: launch. Triangle: rescan. Circle: return to XMB.
- In a game, hold Select + Start for one second to return to the launcher.
- The system menu’s quit action returns to XMB.

User data lives in `/dev_hdd0/atari/`, outside the installed application folder.

## Build

Clone the pinned emulator submodules:

```sh
git clone --recurse-submodules https://github.com/richard1222/Atari4PS3.git
cd Atari4PS3
```

Use a working PSL1GHT PS3 toolchain with SDL2 and zlib portlibs. The current local build uses `/opt/ps3sdk` under WSL:

```sh
export PS3DEV=/opt/ps3sdk
export PSL1GHT="$PS3DEV"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH"
make core-libs
make pkg
```

The finalized output is `build/atari4ps3-m0.gnpdrm.pkg`; the filename is retained from the initial prototype. Current test builds contain the launcher and all seven runners. See [build notes](docs/BUILD.md).

## Checks and current work

```sh
python3 tests/run_host_tests.py
```

Host checks cover audio queue ordering and wraparound, save-folder separation and watchdog shutdown/stall handling. They do not prove gameplay or sound on real hardware.

Recent fixes include synchronized audio buffering, separate 8-bit/5200 saves, respecting quit requests on warning screens, stopping the watchdog before SDL shutdown, and PS3 SDK compatibility fixes. See [status](docs/STATUS.md) and [PS3 test checklist](docs/HARDWARE_TEST.md).

Core sources remain upstream submodules. PS3 compatibility code lives in `shim/`; licenses and notices are in [LICENSES](LICENSES/README.md).

#ifndef A4_PATHS_H
#define A4_PATHS_H

/* Shared paths for Atari4PS3. User data lives outside the game dir so
 * it survives reinstalls; the game dir holds only the launcher +
 * runner SELFs.
 *
 * Runner parametrization (M2): each runner SELF is built from the
 * same sources with -D overrides. The #ifndef defaults below are the
 * Stella (2600) runner; the Makefile overrides them per core. */

#define A4_APPID "ATAR00001"

#define A4_ROOT "/dev_hdd0/atari"
#define A4_ROMS_2600 A4_ROOT "/roms/2600"
#define A4_ROMS_5200 A4_ROOT "/roms/5200"
#define A4_ROMS_7800 A4_ROOT "/roms/7800"
#define A4_ROMS_LYNX A4_ROOT "/roms/lynx"
#define A4_ROMS_8BIT A4_ROOT "/roms/8bit"
#define A4_ROMS_ST A4_ROOT "/roms/st"
#define A4_ROMS_JAGUAR A4_ROOT "/roms/jaguar"
#define A4_ROMS_PC A4_ROOT "/roms/pc"
#define A4_BIOS_DIR A4_ROOT "/bios"
#define A4_SAVES_2600 A4_ROOT "/saves/2600"
#define A4_SAVES_5200 A4_ROOT "/saves/5200"
#define A4_SAVES_7800 A4_ROOT "/saves/7800"
#define A4_SAVES_LYNX A4_ROOT "/saves/lynx"
#define A4_SAVES_8BIT A4_ROOT "/saves/8bit"
#define A4_SAVES_ST A4_ROOT "/saves/st"
#define A4_SAVES_JAGUAR A4_ROOT "/saves/jaguar"
#define A4_SAVES_PC A4_ROOT "/saves/pc"
#define A4_STATES_2600 A4_ROOT "/states/2600"
#define A4_STATES_5200 A4_ROOT "/states/5200"
#define A4_STATES_7800 A4_ROOT "/states/7800"
#define A4_STATES_LYNX A4_ROOT "/states/lynx"
#define A4_STATES_8BIT A4_ROOT "/states/8bit"
#define A4_STATES_ST A4_ROOT "/states/st"
#define A4_STATES_JAGUAR A4_ROOT "/states/jaguar"
#define A4_STATES_PC A4_ROOT "/states/pc"
#define A4_CONFIG_DIR A4_ROOT "/config"
#define A4_LOGS_DIR A4_ROOT "/logs"

#define A4_LAUNCH_FILE A4_CONFIG_DIR "/launch.txt"
#define A4_RETURN_FILE A4_CONFIG_DIR "/return.txt"
#define A4_LOG_FILE A4_LOGS_DIR "/m0.log"
#define A4_WATCHDOG_LOG A4_LOGS_DIR "/watchdog.log"

#define A4_LAUNCHER_EBOOT "/dev_hdd0/game/" A4_APPID "/USRDIR/EBOOT.BIN"
#define A4_SELF_DIR "/dev_hdd0/game/" A4_APPID "/USRDIR/cores"
#define A4_SELF_STELLA A4_SELF_DIR "/stella.self"
#define A4_SELF_ATARI800 A4_SELF_DIR "/atari800.self"
#define A4_SELF_PROSYSTEM A4_SELF_DIR "/prosystem.self"
#define A4_SELF_HANDY A4_SELF_DIR "/handy.self"
#define A4_SELF_HATARI A4_SELF_DIR "/hatari.self"
#define A4_SELF_VJ A4_SELF_DIR "/virtualjaguar.self"
#define A4_SELF_DOSBOX A4_SELF_DIR "/dosbox-pure.self"

/* Kept for compatibility: the original single-runner name. */
#define A4_RUNNER_SELF A4_SELF_STELLA

/* --- per-runner build defines (Makefile -D overrides) --- */

/* Core name: handshake line 1 + return-file tag. Must match the
 * launcher's blade table for the system this SELF serves. */
#ifndef A4_CORE_NAME
#define A4_CORE_NAME "stella"
#endif

/* Save-RAM directory this runner loads/flushes <rom>.srm from. */
#ifndef A4_SAVE_DIR
#define A4_SAVE_DIR A4_SAVES_2600
#endif

/* Required BIOS file in A4_BIOS_DIR, or "" for none. When
 * A4_BIOS_REQUIRED is 1 and the file is missing, the runner shows a
 * clean missing-BIOS screen and returns to the launcher instead of
 * starting the core. */
#ifndef A4_BIOS_FILE
#define A4_BIOS_FILE ""
#endif
#ifndef A4_BIOS_REQUIRED
#define A4_BIOS_REQUIRED 0
#endif

/* 1 = show a performance-warning screen at boot (Jaguar). */
#ifndef A4_WARN_SLOW
#define A4_WARN_SLOW 0
#endif

/* Core option key answered from the handshake hint (line 3), or "".
 * Only the atari800 runner uses this ("atari800_system": "5200" vs
 * "800XL (64K)"). */
#ifndef A4_HINT_KEY
#define A4_HINT_KEY ""
#endif

/* Static core options answered by GET_VARIABLE, as a "k=v;k=v" table,
 * or "". Keys are the core's own option keys, verified in its source
 * (see README). Anything not listed here falls through to the core's
 * built-in default, exactly like M0/M1. */
#ifndef A4_CORE_VARS
#define A4_CORE_VARS ""
#endif

#endif

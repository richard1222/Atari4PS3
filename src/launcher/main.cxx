/* Atari4PS3 M2 launcher: Xbox 360 blades-dashboard UI.
 *
 * Horizontal blades across the screen, 360 style: the selected blade is
 * centered and bright, neighbors peek at the edges, switching blades
 * slides the row with a short animation. All 8 system blades are live
 * in M2 (one statically-linked runner SELF per system); Settings
 * (rescan all, quit to XMB) is the 9th blade.
 *
 * Controls: Left/Right switch blades, Up/Down browse rows, X launches
 * or activates, Triangle re-scans, O quits to XMB, PS quits to XMB.
 * The left stick also moves (with hysteresis, SalsaDS pattern).
 *
 * HARD CONSTRAINTS (proven on hardware 2026-11-14, do not regress):
 * - SOFTWARE SDL renderer is REQUIRED. The PSL1GHT accelerated
 *   renderer black-screens (first present never returns). Keep the
 *   SOFTWARE-first renderer chain, no vsync anywhere.
 * - Works with ZERO ROMs: every blade shows a clean empty state with
 *   its ROM folder path.
 * - Spawn-on-X-only over a real ROM; return-file handling and flushed
 *   logging kept (renderer pick, menu alive heartbeat, launch lines).
 * - Font-atlas text rendering kept (proven).
 *
 * INPUT FIXES (M1, 2026-11-14): the M0 menu saw no controller input on
 * hardware. The M0 pad code polled port 0 only via the old
 * ioPadGetInfo; this build uses the Neon Lounge pattern (ioPadGetInfo2,
 * all 7 ports, sticky per-port state, ioPadInit AFTER SDL_Init) and
 * logs the raw button word on every change so the log proves input is
 * seen. The PS button did nothing because no sysutil callback was ever
 * registered; this build registers one (SalsaDS/Neon Lounge pattern)
 * and pumps sysUtilCheckCallback() every frame.
 *
 * FREEZE FIX (M2, 2026-11-14): the M1 blades UI froze on hardware after
 * navigating blade 8 -> blade 0: m0.log ended mid-loop with no further
 * lines, which the append-per-call log design proves was the loop
 * itself stopping. The easing math is bounded and converges (verified
 * by code review); the suspect is SDL_RenderPresent -- the PSL1GHT
 * SDL2 driver's present path waits for flip completion
 * (gcmSetWaitFlip/gcmGetFlipStatus) and can block indefinitely if a
 * flip completion is missed, after hundreds of successful presents.
 * Three changes address it:
 *  1. Dirty rendering: the menu draws + presents ONLY when something
 *     changed (input, animation running, rescan, status change). At
 *     idle the loop still runs at 60 Hz for input/sysutil, but makes
 *     zero present calls -- the suspected blocking call is exercised
 *     ~100x less, and only while the user is actively navigating.
 *  2. Watchdog: a second thread (common/watchdog) watches a
 *     frame-sequence + phase variable and logs STALL lines with the
 *     stuck phase to its own file (watchdog.log). If a future freeze
 *     happens, the log names WHERE the loop stuck. As a safety net it
 *     exits cleanly to XMB after 8 s stuck in PRESENT (30 s anywhere
 *     else) instead of leaving a hard freeze.
 *  3. Frame pacing + time-based easing (60 fps requirement): the loop
 *     delays only the remaining frame budget instead of a fixed 16 ms
 *     after a full draw+present (which guaranteed <60 fps), and the
 *     blade slide uses elapsed real time so it stays smooth at 60.
 *     The heartbeat logs loop fps + presents/sec + max frame ms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <math.h>
#include <sys/stat.h>

#include <lv2/process.h>
#include "SDL.h"

#include "common/paths.h"
#include "common/log.h"
#include "common/fsutil.h"
#include "common/pad.h"
#include "common/spawn.h"
#include "common/sysutil.h"
#include "common/watchdog.h"
#include "launcher/font_text.h"

#define MENU_W 1280
#define MENU_H 720

/* Blade geometry: selected blade centered, neighbors peek at the edges. */
#define BLADE_W 520
#define BLADE_H 470
#define BLADE_Y 108
#define BLADE_GAP 580 /* center-to-center spacing */
#define BLADE_CX ((MENU_W - BLADE_W) / 2) /* x of the selected blade */
#define BLADE_PAD 24
#define BLADE_INNER (BLADE_W - 2 * BLADE_PAD)
#define ROW_H 34
#define ROWS_VISIBLE 11
#define MAX_ROMS 256
#define NAME_LEN 128
#define ROW_NAME_CHARS 22 /* display cap for a row name at scale 2 */

#define BLADE_COUNT 9

struct RomEntry {
  char name[NAME_LEN];
  long size; /* -1 when stat failed */
};

struct Blade {
  const char* title;
  const char* folder; /* NULL for the Settings blade */
  const char* ext1;
  const char* ext2; /* may be NULL */
  const char* ext3; /* may be NULL */
  const char* ext4; /* may be NULL */
  const char* self_path; /* runner SELF, NULL for Settings */
  const char* core_name; /* handshake line 1, NULL for Settings */
  const char* sys_hint;  /* handshake line 3 (atari800 only), else NULL */
  const char* bios_file; /* required BIOS in A4_BIOS_DIR, or NULL */
  int warn_slow;         /* 1 = performance warning (Jaguar) */
  int live;              /* X launches from this blade */
  int is_settings;
  int self_ok;  /* SELF passed the pre-flight check at boot/rescan */
  int bios_ok;  /* required BIOS present (1 when none required) */
  struct RomEntry roms[MAX_ROMS];
  int count;
  int selected;
};

static struct Blade g_blades[BLADE_COUNT];
static const char* kSettingsRows[2] = { "Rescan all folders", "Quit to XMB" };

static void init_blades(void) {
  memset(g_blades, 0, sizeof g_blades);
  g_blades[0].title = "Atari 2600";
  g_blades[0].folder = A4_ROMS_2600;
  g_blades[0].ext1 = ".a26";
  g_blades[0].ext2 = ".bin";
  g_blades[0].self_path = A4_SELF_STELLA;
  g_blades[0].core_name = "stella";
  g_blades[0].live = 1;
  g_blades[1].title = "Atari 5200";
  g_blades[1].folder = A4_ROMS_5200;
  g_blades[1].ext1 = ".a52";
  g_blades[1].ext2 = ".bin";
  g_blades[1].ext3 = ".car";
  g_blades[1].ext4 = ".rom";
  g_blades[1].self_path = A4_SELF_ATARI800;
  g_blades[1].core_name = "atari800";
  g_blades[1].sys_hint = "5200";
  g_blades[1].live = 1;
  g_blades[2].title = "Atari 7800";
  g_blades[2].folder = A4_ROMS_7800;
  g_blades[2].ext1 = ".a78";
  g_blades[2].ext2 = ".bin";
  g_blades[2].ext3 = ".cdf";
  g_blades[2].self_path = A4_SELF_PROSYSTEM;
  g_blades[2].core_name = "prosystem";
  g_blades[2].live = 1;
  g_blades[3].title = "Atari Lynx";
  g_blades[3].folder = A4_ROMS_LYNX;
  g_blades[3].ext1 = ".lnx";
  g_blades[3].ext2 = ".lyx";
  g_blades[3].self_path = A4_SELF_HANDY;
  g_blades[3].core_name = "handy";
  g_blades[3].live = 1;
  g_blades[4].title = "8-bit Computers";
  g_blades[4].folder = A4_ROMS_8BIT;
  g_blades[4].ext1 = ".atr";
  g_blades[4].ext2 = ".xex";
  g_blades[4].ext3 = ".car";
  g_blades[4].ext4 = ".cas";
  g_blades[4].self_path = A4_SELF_ATARI800;
  g_blades[4].core_name = "atari800";
  g_blades[4].sys_hint = "800XL (64K)";
  g_blades[4].live = 1;
  g_blades[5].title = "Atari ST";
  g_blades[5].folder = A4_ROMS_ST;
  g_blades[5].ext1 = ".st";
  g_blades[5].ext2 = ".msa";
  g_blades[5].ext3 = ".dim";
  g_blades[5].ext4 = ".stx";
  g_blades[5].self_path = A4_SELF_HATARI;
  g_blades[5].core_name = "hatari";
  g_blades[5].bios_file = "tos.img";
  g_blades[5].live = 1;
  g_blades[6].title = "Atari Jaguar";
  g_blades[6].folder = A4_ROMS_JAGUAR;
  g_blades[6].ext1 = ".j64";
  g_blades[6].ext2 = ".jag";
  g_blades[6].ext3 = ".abs";
  g_blades[6].ext4 = ".cof";
  g_blades[6].self_path = A4_SELF_VJ;
  g_blades[6].core_name = "virtualjaguar";
  g_blades[6].warn_slow = 1;
  g_blades[6].live = 1;
  g_blades[7].title = "Atari PC";
  g_blades[7].folder = A4_ROMS_PC;
  g_blades[7].ext1 = ".zip";
  g_blades[7].ext2 = ".img";
  g_blades[7].ext3 = ".iso";
  g_blades[7].ext4 = ".exe";
  g_blades[7].self_path = A4_SELF_DOSBOX;
  g_blades[7].core_name = "dosbox-pure";
  g_blades[7].live = 1;
  g_blades[8].title = "Settings";
  g_blades[8].is_settings = 1;
}

static int ends_with_ci(const char* name, const char* ext) {
  size_t nl = strlen(name), el = strlen(ext);
  if (nl < el) return 0;
  const char* tail = name + nl - el;
  for (size_t i = 0; i < el; i++) {
    char a = tail[i], b = ext[i];
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    if (a != b) return 0;
  }
  return 1;
}

static int cmp_rom(const void* a, const void* b) {
  return strcmp(((const struct RomEntry*)a)->name,
                ((const struct RomEntry*)b)->name);
}

/* Bounded copy that never overruns dst (avoids snprintf truncation). */
static void copy_capped(char* dst, size_t dst_size, const char* src) {
  size_t n = strlen(src);
  if (n >= dst_size) n = dst_size - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

/* Join a blade's extensions into ".a26/.bin" display form. */
static void exts_display(struct Blade* b, char* buf, size_t buf_size) {
  buf[0] = '\0';
  const char* exts[4] = { b->ext1, b->ext2, b->ext3, b->ext4 };
  size_t off = 0;
  for (int i = 0; i < 4; i++) {
    if (!exts[i]) continue;
    int w = snprintf(buf + off, buf_size - off, "%s%s", off ? "/" : "",
                     exts[i]);
    if (w < 0 || off + (size_t)w >= buf_size) break;
    off += (size_t)w;
  }
}

/* (Re)scan one blade's folder. Returns the count. */
static int scan_blade(struct Blade* b) {
  b->count = 0;
  b->selected = 0;
  if (!b->folder) return 0;
  DIR* d = opendir(b->folder);
  if (!d) {
    a4_log("launcher", "opendir %s failed", b->folder);
    return 0;
  }
  struct dirent* de;
  while ((de = readdir(d)) != NULL && b->count < MAX_ROMS) {
    if (ends_with_ci(de->d_name, b->ext1) ||
        (b->ext2 && ends_with_ci(de->d_name, b->ext2)) ||
        (b->ext3 && ends_with_ci(de->d_name, b->ext3)) ||
        (b->ext4 && ends_with_ci(de->d_name, b->ext4))) {
      /* Join folder + name with explicit bounds (no snprintf: GCC
       * cannot prove the dirent name fits and warns). */
      char full[512];
      size_t fl = strlen(b->folder);
      size_t nl = strlen(de->d_name);
      if (fl + 1 + nl >= sizeof full) continue;
      memcpy(full, b->folder, fl);
      full[fl] = '/';
      memcpy(full + fl + 1, de->d_name, nl + 1);
      struct stat st;
      long sz = -1;
      if (stat(full, &st) == 0) sz = (long)st.st_size;
      copy_capped(b->roms[b->count].name, NAME_LEN, de->d_name);
      b->roms[b->count].size = sz;
      b->count++;
    }
  }
  closedir(d);
  if (b->count > 1)
    qsort(b->roms, (size_t)b->count, sizeof(struct RomEntry), cmp_rom);
  a4_log("launcher", "blade %s: %d file(s) in %s", b->title, b->count,
         b->folder);
  return b->count;
}

static void scan_all(void) {
  for (int i = 0; i < BLADE_COUNT; i++) scan_blade(&g_blades[i]);
}

/* Re-check each blade's runner SELF + required BIOS. Called at boot
 * and on every rescan (so adding tos.img while in the menu unblocks
 * the ST blade without a restart). Logs only on change, except the
 * boot pass (first=1) which always logs. */
static void check_blades(int first) {
  for (int i = 0; i < BLADE_COUNT; i++) {
    struct Blade* b = &g_blades[i];
    if (b->is_settings) continue;
    int prev_self = b->self_ok, prev_bios = b->bios_ok;
    char detail[320];
    b->self_ok = b->self_path
        ? a4_spawn_check(b->self_path, detail, sizeof detail)
        : 0;
    if (b->bios_file) {
      char biospath[512];
      snprintf(biospath, sizeof biospath, "%s/%s", A4_BIOS_DIR,
               b->bios_file);
      b->bios_ok = a4_is_file(biospath) ? 1 : 0;
    } else {
      b->bios_ok = 1;
    }
    if (first || b->self_ok != prev_self || b->bios_ok != prev_bios) {
      a4_log("launcher", "blade %s: self_ok=%d bios_ok=%d %s", b->title,
             b->self_ok, b->bios_ok, b->self_path ? detail : "(no SELF)");
    }
  }
}

static int blade_rows(struct Blade* b) {
  return b->is_settings ? 2 : b->count;
}

/* "512 B", "32 KB", "4.0 MB". */
static void fmt_size(long size, char* buf, size_t buf_size) {
  if (size < 0) {
    snprintf(buf, buf_size, "?");
  } else if (size < 1024) {
    snprintf(buf, buf_size, "%ld B", size);
  } else if (size < 1024 * 1024) {
    snprintf(buf, buf_size, "%ld KB", (size + 512) / 1024);
  } else {
    snprintf(buf, buf_size, "%ld.%ld MB", size / (1024 * 1024),
             (size % (1024 * 1024)) * 10 / (1024 * 1024));
  }
}

/* Set when the texture probe fails: blades draw rects only (no
 * RenderCopy), so the loop can still prove itself alive on screen. */
static int g_no_text = 0;

static void log_renderer_info(SDL_Renderer* ren, const char* tag) {
  if (!ren) {
    a4_log("launcher", "%s: no renderer err=%s", tag, SDL_GetError());
    return;
  }
  SDL_RendererInfo info;
  if (SDL_GetRendererInfo(ren, &info) == 0) {
    a4_log("launcher", "%s: name=%s flags=0x%x maxtex=%dx%d", tag,
           info.name ? info.name : "?", (unsigned)info.flags,
           info.max_texture_width, info.max_texture_height);
  } else {
    a4_log("launcher", "%s: GetRendererInfo FAILED err=%s", tag,
           SDL_GetError());
  }
}

/* Rects-only probe: clear + one fill + present, logging every step.
 * Returns 0 when every step looked healthy, -1 otherwise. Note
 * SDL_RenderPresent returns void, so the only failure signal for the
 * present itself is an SDL error string left behind -- SDL_ClearError
 * is called first so a stale string cannot fake a failure. */
static int probe_present(SDL_Renderer* ren, const char* tag) {
  if (!ren) return -1;
  int bad = 0;
  SDL_SetRenderDrawColor(ren, 16, 20, 32, 255);
  int rc = SDL_RenderClear(ren);
  a4_log("launcher", "%s: clear rc=%d err=%s", tag, rc, SDL_GetError());
  if (rc != 0) bad = 1;
  SDL_Rect r = { 0, 0, MENU_W, 64 };
  SDL_SetRenderDrawColor(ren, 245, 245, 245, 255);
  rc = SDL_RenderFillRect(ren, &r);
  a4_log("launcher", "%s: fill rc=%d err=%s", tag, rc, SDL_GetError());
  if (rc != 0) bad = 1;
  a4_log("launcher", "%s: present start", tag);
  SDL_ClearError();
  SDL_RenderPresent(ren);
  const char* err = SDL_GetError();
  a4_log("launcher", "%s: present done err=%s", tag, err);
  if (err && err[0] != '\0') bad = 1;
  return bad ? -1 : 0;
}

/* NOTE (black-screen fixes, M0): the first M0 build created the
 * renderer with SDL_RENDERER_PRESENTVSYNC first, and on hardware the
 * first SDL_RenderPresent never returned. Vsync was dropped, but the
 * second build STILL showed a black screen -- the log stopped INSIDE
 * the first menu draw with the PSL1GHT accelerated renderer. The
 * SOFTWARE-first chain below is what fixed it on hardware: SOFTWARE
 * first, then ACCELERATED, then default, every candidate
 * probe-presented before acceptance. Do not reorder this. */
static SDL_Renderer* make_renderer(SDL_Window* win) {
  if (!win) return NULL;
  static const struct {
    const char* name;
    uint32_t flags;
  } kCandidates[] = {
    { "SOFTWARE", SDL_RENDERER_SOFTWARE },
    { "ACCELERATED", SDL_RENDERER_ACCELERATED },
    { "DEFAULT", 0 },
  };
  SDL_Renderer* last = NULL;
  for (size_t i = 0; i < sizeof kCandidates / sizeof kCandidates[0]; i++) {
    SDL_Renderer* ren =
        SDL_CreateRenderer(win, -1, kCandidates[i].flags);
    a4_log("launcher", "renderer try %s -> %p err=%s",
           kCandidates[i].name, (void*)ren, SDL_GetError());
    if (!ren) continue;
    log_renderer_info(ren, kCandidates[i].name);
    char tag[48];
    snprintf(tag, sizeof tag, "probe %s", kCandidates[i].name);
    int rc = probe_present(ren, tag);
    if (rc == 0) {
      a4_log("launcher", "renderer picked: %s", kCandidates[i].name);
      if (last) SDL_DestroyRenderer(last);
      return ren;
    }
    a4_log("launcher", "renderer %s REJECTED (probe failed)",
           kCandidates[i].name);
    if (last) SDL_DestroyRenderer(last);
    last = ren; /* keep the last created one as a last resort */
  }
  if (last) {
    a4_log("launcher",
           "WARNING: all renderer probes failed; keeping last renderer");
    return last;
  }
  return NULL;
}

static void blade_text_centered(struct A4Font* font, SDL_Renderer* ren,
                                int bx, int y, const char* text, int scale,
                                unsigned char r, unsigned char g,
                                unsigned char b) {
  int w = a4_text_width(text, scale);
  a4_draw_text(font, ren, bx + (BLADE_W - w) / 2, y, text, scale, r, g, b);
}

/* Draw the selected blade's rows (games or settings). */
static void draw_blade_rows(struct A4Font* font, SDL_Renderer* ren,
                            struct Blade* b, int bx) {
  int rows = blade_rows(b);
  int top = 0;
  if (b->selected >= ROWS_VISIBLE) top = b->selected - ROWS_VISIBLE + 1;
  if (top + ROWS_VISIBLE > rows) top = rows - ROWS_VISIBLE;
  if (top < 0) top = 0;
  int y0 = BLADE_Y + 80;
  for (int i = 0; i < ROWS_VISIBLE && top + i < rows; i++) {
    int idx = top + i;
    int y = y0 + i * ROW_H;
    if (idx == b->selected) {
      SDL_Rect sel = { bx + 12, y - 4, BLADE_W - 24, ROW_H - 2 };
      SDL_SetRenderDrawColor(ren, 60, 90, 220, 255);
      SDL_RenderFillRect(ren, &sel);
    }
    if (b->is_settings) {
      unsigned char sh = idx == b->selected ? 255 : 40;
      a4_draw_text(font, ren, bx + BLADE_PAD, y, kSettingsRows[idx], 2,
                   sh, sh, idx == b->selected ? 255 : 50);
    } else {
      char disp[ROW_NAME_CHARS + 3];
      copy_capped(disp, sizeof disp, b->roms[idx].name);
      if (strlen(b->roms[idx].name) > ROW_NAME_CHARS) {
        disp[ROW_NAME_CHARS - 2] = '.';
        disp[ROW_NAME_CHARS - 1] = '.';
        disp[ROW_NAME_CHARS] = '\0';
      }
      unsigned char sh = idx == b->selected ? 255 : 40;
      a4_draw_text(font, ren, bx + BLADE_PAD, y, disp, 2, sh, sh,
                   idx == b->selected ? 255 : 50);
      char sz[32];
      fmt_size(b->roms[idx].size, sz, sizeof sz);
      int sw = a4_text_width(sz, 1);
      a4_draw_text(font, ren, bx + BLADE_W - BLADE_PAD - sw, y + 8, sz, 1,
                   sh, sh, idx == b->selected ? 255 : 90);
    }
  }
  if (top > 0)
    a4_draw_text(font, ren, bx + BLADE_PAD, y0 - 22, "more above", 1,
                 120, 120, 130);
  if (top + ROWS_VISIBLE < rows)
    a4_draw_text(font, ren, bx + BLADE_PAD, y0 + ROWS_VISIBLE * ROW_H + 2,
                 "more below", 1, 120, 120, 130);
}

/* Draw the selected blade's empty state. The message depends on WHY
 * the blade is empty: a missing core, a missing BIOS, or genuinely
 * no game files. */
static void draw_blade_empty(struct A4Font* font, SDL_Renderer* ren,
                             struct Blade* b, int bx) {
  int cy = BLADE_Y + 150;
  if (!b->self_ok) {
    blade_text_centered(font, ren, bx, cy, "Core not built", 3, 30, 30, 40);
    blade_text_centered(font, ren, bx, cy + 70, "This system's core did not",
                        2, 90, 90, 100);
    blade_text_centered(font, ren, bx, cy + 102, "build. See the build",
                        2, 90, 90, 100);
    blade_text_centered(font, ren, bx, cy + 134, "report.", 2, 90, 90, 100);
  } else if (!b->bios_ok) {
    blade_text_centered(font, ren, bx, cy, "BIOS missing", 3, 30, 30, 40);
    char line[96];
    snprintf(line, sizeof line, "Put %s in:", b->bios_file);
    blade_text_centered(font, ren, bx, cy + 70, line, 2, 90, 90, 100);
    blade_text_centered(font, ren, bx, cy + 106, A4_BIOS_DIR, 2, 40, 80, 180);
  } else {
    blade_text_centered(font, ren, bx, cy, "No games found", 3, 30, 30, 40);
    char exts[48];
    exts_display(b, exts, sizeof exts);
    char line[96];
    snprintf(line, sizeof line, "Put %s files in:", exts);
    blade_text_centered(font, ren, bx, cy + 70, line, 2, 90, 90, 100);
    blade_text_centered(font, ren, bx, cy + 106, b->folder, 2, 40, 80, 180);
  }
}

/* Draw one frame into the back buffer. Does NOT present: the caller
 * presents (with watchdog phase markers around it) so DRAW and
 * PRESENT stalls are distinguishable in watchdog.log. */
static void draw_blades(SDL_Renderer* ren, struct A4Font* font,
                        float blade_pos, int target, int returned,
                        int spawn_failed, const char* spawn_err) {
  if (!ren) return;
  int use_text = font && !g_no_text;

  /* background */
  SDL_SetRenderDrawColor(ren, 22, 26, 38, 255);
  SDL_RenderClear(ren);

  /* header bar */
  SDL_Rect bar = { 0, 0, MENU_W, 64 };
  SDL_SetRenderDrawColor(ren, 245, 245, 245, 255);
  SDL_RenderFillRect(ren, &bar);
  if (use_text) {
    a4_draw_text(font, ren, 40, 14, "ATARI4PS3", 3, 20, 20, 30);
    int total = 0;
    for (int i = 0; i < BLADE_COUNT; i++) total += g_blades[i].count;
    char countbuf[64];
    snprintf(countbuf, sizeof countbuf, "%d game(s)", total);
    a4_draw_text(font, ren, MENU_W - 40 - a4_text_width(countbuf, 2), 28,
                 countbuf, 2, 80, 80, 90);
  }
  /* one status line under the bar: spawn error wins over "returned" */
  if (use_text && spawn_failed && spawn_err) {
    a4_draw_text(font, ren, 40, 76, spawn_err, 2, 255, 120, 255);
  } else if (use_text && returned) {
    a4_draw_text(font, ren, 40, 76, "Returned from the runner.", 2,
                 120, 170, 255);
  }

  /* blades */
  for (int i = 0; i < BLADE_COUNT; i++) {
    float d = (float)i - blade_pos;
    int bx = (int)(BLADE_CX + d * BLADE_GAP);
    if (bx + BLADE_W < 0 || bx > MENU_W) continue;
    float ad = fabsf(d);
    int is_sel = ad < 0.5f;
    SDL_Rect blade = { bx, BLADE_Y, BLADE_W, BLADE_H };
    if (is_sel)
      SDL_SetRenderDrawColor(ren, 236, 238, 244, 255);
    else if (ad < 1.5f)
      SDL_SetRenderDrawColor(ren, 96, 102, 120, 255);
    else
      SDL_SetRenderDrawColor(ren, 58, 63, 78, 255);
    SDL_RenderFillRect(ren, &blade);
    if (is_sel) {
      SDL_Rect acc = { bx, BLADE_Y, BLADE_W, 6 };
      SDL_SetRenderDrawColor(ren, 90, 180, 60, 255);
      SDL_RenderFillRect(ren, &acc);
    }
    if (!use_text) continue;
    struct Blade* b = &g_blades[i];
    if (is_sel) {
      a4_draw_text(font, ren, bx + BLADE_PAD, BLADE_Y + 16, b->title, 3,
                   20, 20, 30);
      SDL_Rect div = { bx + BLADE_PAD, BLADE_Y + 64, BLADE_INNER, 2 };
      SDL_SetRenderDrawColor(ren, 170, 175, 185, 255);
      SDL_RenderFillRect(ren, &div);
      if (b->is_settings || b->count > 0) {
        draw_blade_rows(font, ren, b, bx);
        if (!b->is_settings && b->warn_slow) {
          a4_draw_text(font, ren, bx + BLADE_PAD, BLADE_Y + BLADE_H - 32,
                       "Warning: may run below full speed.", 1, 200, 120, 40);
        } else if (!b->is_settings && !b->bios_ok) {
          a4_draw_text(font, ren, bx + BLADE_PAD, BLADE_Y + BLADE_H - 32,
                       "BIOS missing: see empty state.", 1, 200, 60, 60);
        }
      } else {
        draw_blade_empty(font, ren, b, bx);
      }
    } else {
      unsigned char tsh = ad < 1.5f ? 235 : 190;
      a4_draw_text(font, ren, bx + BLADE_PAD, BLADE_Y + 18, b->title, 2,
                   tsh, tsh, ad < 1.5f ? 240 : 200);
      char sub[64];
      if (b->is_settings) {
        snprintf(sub, sizeof sub, "Rescan / Quit");
      } else if (!b->self_ok) {
        snprintf(sub, sizeof sub, "Core not built");
      } else if (!b->bios_ok) {
        snprintf(sub, sizeof sub, "Needs %s", b->bios_file);
      } else if (b->warn_slow) {
        snprintf(sub, sizeof sub, "%d game(s) - may run slow", b->count);
      } else {
        snprintf(sub, sizeof sub, "%d game(s)", b->count);
      }
      unsigned char ssh = ad < 1.5f ? 200 : 150;
      a4_draw_text(font, ren, bx + BLADE_PAD, BLADE_Y + 54, sub, 1, ssh, ssh,
                   ad < 1.5f ? 210 : 165);
    }
  }

  /* footer hints, aware of the selected blade */
  if (use_text) {
    struct Blade* cur = &g_blades[target];
    const char* hints;
    if (cur->is_settings)
      hints = "U/D: choose    X: select    O: quit";
    else if (cur->live && cur->self_ok && cur->bios_ok && cur->count > 0)
      hints = "L/R: blades  U/D: browse  X: launch  Tri: rescan  O: quit";
    else
      hints = "L/R: blades  U/D: browse  Tri: rescan  O: quit";
    a4_draw_text(font, ren, 40, MENU_H - 78, hints, 2, 220, 220, 220);
  }

  /* status strip */
  SDL_Rect strip = { 0, MENU_H - 40, MENU_W, 40 };
  struct Blade* cur = &g_blades[target];
  if (spawn_failed)
    SDL_SetRenderDrawColor(ren, 220, 40, 220, 255);
  else if (returned)
    SDL_SetRenderDrawColor(ren, 60, 90, 220, 255);
  else if (cur->is_settings || cur->count > 0)
    SDL_SetRenderDrawColor(ren, 40, 170, 60, 255);
  else
    SDL_SetRenderDrawColor(ren, 220, 130, 40, 255);
  SDL_RenderFillRect(ren, &strip);
}

static int pump_quit_event(void) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT) return 1;
  }
  return 0;
}

/* Spawn the runner for rompath on blade b. Only called on X over a
 * real ROM on a ready blade. On failure (the spawn call returns, or
 * the SELF fails its pre-check) the reason is logged, errbuf is
 * filled for the on-screen error line, and the caller stays in the
 * menu. */
static void try_spawn(struct Blade* b, const char* rompath, char* errbuf,
                      size_t errbuf_size) {
  /* Handshake file: fallback if argv does not survive exitspawn.
   * Format: line 1 = core name (the runner verifies it), line 2 =
   * ROM path, line 3 = system hint (atari800 only, empty otherwise).
   * All three lines are always written so the format is stable. */
  char hs[700];
  snprintf(hs, sizeof hs, "%s\n%s\n%s\n", b->core_name, rompath,
           b->sys_hint ? b->sys_hint : "");
  int hs_ok = a4_write_file(A4_LAUNCH_FILE, hs, strlen(hs));
  a4_log("launcher", "handshake written ok=%d core=%s path=%s hint=%s",
         hs_ok, b->core_name, rompath, b->sys_hint ? b->sys_hint : "");

  char detail[320];
  int check = a4_spawn_check(b->self_path, detail, sizeof detail);
  a4_log("launcher", "pre-spawn check=%d %s", check, detail);
  if (!check) {
    a4_log("launcher", "SPAWN ABORTED: runner SELF failed pre-check");
    snprintf(errbuf, errbuf_size,
             "Spawn aborted: runner SELF failed check (see m0.log)");
    return;
  }

  a4_watchdog_phase(A4_PHASE_SPAWN);
  a4_log("launcher", "SPAWN %s arg=%s", b->self_path, rompath);
  a4_spawn(b->self_path, rompath);
  /* The call returned: spawn failed. No kernel error code is available
   * (the PSL1GHT wrapper returns void). Stay in the menu. */
  a4_log("launcher", "SPAWN FAILED (sysProcessExitSpawn2 returned)");
  snprintf(errbuf, errbuf_size,
           "Spawn failed: sysProcessExitSpawn2 returned (see m0.log)");
}

int main(int argc, char** argv) {
  a4_log_init();
  a4_log("launcher", "BOOT argc=%d argv0=%s argv1=%s", argc,
         argc > 0 && argv[0] ? argv[0] : "(none)",
         argc > 1 && argv[1] ? argv[1] : "(none)");
  a4_log("launcher", "ensure_dirs ok=%d", (int)a4_ensure_dirs());

  /* Did the runner send us back? The return file is the round-trip proof. */
  int returned = 0;
  unsigned char* retdata = NULL;
  size_t retsize = 0;
  if (a4_read_file(A4_RETURN_FILE, &retdata, &retsize)) {
    a4_log("launcher", "RETURN-FILE: %.*s", (int)retsize, (const char*)retdata);
    free(retdata);
    remove(A4_RETURN_FILE);
    returned = 1;
  }

  /* PS-button handling: register the sysutil callback early. Without
   * this, pressing PS does nothing (no callback = no SYSUTIL_EXIT_GAME
   * delivery). Pumped once per frame in the loop below. */
  a4_sysutil_init();

  SDL_Window* win = NULL;
  SDL_Renderer* ren = NULL;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    win = SDL_CreateWindow("Atari4PS3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           MENU_W, MENU_H, SDL_WINDOW_FULLSCREEN);
    ren = make_renderer(win);
  }
  a4_log("launcher", "video win=%p ren=%p err=%s",
         (void*)win, (void*)ren, SDL_GetError());

  /* Pad init AFTER SDL_Init (Neon Lounge order). Log the result, the
   * per-port status, and one first poll so the boot log proves what
   * the pad layer sees. */
  a4_pad_init();
  {
    char ps[96];
    a4_pad_status_line(ps, sizeof ps);
    a4_log("launcher", "pad init rc=%d %s", a4_pad_init_result(), ps);
    struct A4Pad first;
    a4_pad_poll(&first);
    a4_log("launcher", "pad first poll buttons=0x%08x lx=%d ly=%d present=%d port=%d",
           first.buttons, first.lx, first.ly, first.present, first.port);
  }

  struct A4Font* font = a4_font_create(ren);
  a4_log("launcher", "font atlas=%p err=%s", (void*)font, SDL_GetError());

  /* Texture probe: one atlas glyph copy + present, before the menu loop.
   * If the copy returns an error, the menu falls back to rects-only
   * drawing (g_no_text). */
  if (ren && font) {
    a4_log("launcher", "texprobe: copy start");
    int prc = a4_font_probe(font, ren, 40, 100);
    a4_log("launcher", "texprobe: copy done rc=%d err=%s", prc,
           SDL_GetError());
    if (prc != 0) {
      g_no_text = 1;
      a4_log("launcher",
             "texprobe FAILED: text-free fallback engaged (rects only)");
    }
    a4_log("launcher", "texprobe: present start");
    SDL_ClearError();
    SDL_RenderPresent(ren);
    a4_log("launcher", "texprobe: present done err=%s", SDL_GetError());
  } else if (ren && !font) {
    a4_log("launcher", "texprobe skipped: no font atlas (rects only)");
  }

  init_blades();
  scan_all();
  check_blades(1);

  /* Freeze watchdog: started after SDL_Init (it needs SDL threads +
   * ticks) and before the menu loop. See common/watchdog.h. */
  a4_watchdog_start("launcher");

  a4_log("launcher", "blades loop enter (X launches, O/PS quit)");
  int blade_target = 0;
  float blade_pos = 0.0f;
  int spawn_failed = 0;
  char spawn_err[128] = "";
  uint32_t prev_vbtn = 0;
  uint32_t prev_raw_buttons = 0;
  uint32_t stick_state = 0; /* virtual d-pad bits from the left stick */
  uint32_t h_next = 0, v_next = 0;
  int h_dir = 0, v_dir = 0;
  unsigned long frames = 0;
  unsigned long presents = 0;
  int first_draw = 1;
  int need_draw = 1; /* dirty flag: draw+present only when set */
  int do_quit = 0;
  uint32_t prev_frame_ms = SDL_GetTicks();
  /* FPS window: loop iterations + presents in the last second, plus
   * the slowest loop period in the window. Reported with the
   * heartbeat so the hardware log proves the 60 fps rate. */
  uint32_t fps_t0 = prev_frame_ms;
  unsigned fps_frames = 0, fps_presents = 0, fps_max_ms = 0;
  unsigned fps_last = 0, fps_pres_last = 0, fps_max_last = 0;
  while (!do_quit) {
    uint32_t frame_start = SDL_GetTicks();
    a4_watchdog_tick();
    uint32_t period_ms = frame_start - prev_frame_ms;
    prev_frame_ms = frame_start;
    uint32_t dt_ms = period_ms;
    if (dt_ms > 100) dt_ms = 100; /* clamp: first frame / long stalls */
    /* Loop period for the fps window (time since the previous loop
     * top, i.e. the real frame rate including the pacing delay). The
     * UNclamped period is used so a stall shows up in max_ms. */
    if (period_ms > fps_max_ms) fps_max_ms = period_ms;
    fps_frames++;

    /* PS button: pump system events first; an exit-game request quits
     * through the same clean path as O. */
    a4_watchdog_phase(A4_PHASE_SYSUTIL);
    if (a4_sysutil_poll()) {
      a4_log("launcher", "QUIT to XMB by PS button");
      do_quit = 1;
      break;
    }

    a4_watchdog_phase(A4_PHASE_PAD);
    struct A4Pad pad;
    a4_pad_poll(&pad);

    /* Log the raw button word on every change: this is the proof the
     * next hardware log needs that input is seen. */
    if (pad.buttons != prev_raw_buttons) {
      a4_log("launcher", "pad buttons=0x%08x lx=%d ly=%d present=%d port=%d",
             pad.buttons, pad.lx, pad.ly, pad.present, pad.port);
      prev_raw_buttons = pad.buttons;
    }

    a4_watchdog_phase(A4_PHASE_INPUT);
    /* Left stick as d-pad, with hysteresis so it does not chatter near
     * the threshold (SalsaDS STICK_ON/STICK_OFF pattern). */
    {
      int dx = (int)pad.lx - 128;
      int dy = (int)pad.ly - 128;
      if (dx <= -80) {
        stick_state |= A4B_BIT(A4B_LEFT);
        stick_state &= ~A4B_BIT(A4B_RIGHT);
      } else if (dx >= 80) {
        stick_state |= A4B_BIT(A4B_RIGHT);
        stick_state &= ~A4B_BIT(A4B_LEFT);
      } else {
        if (dx > -48) stick_state &= ~A4B_BIT(A4B_LEFT);
        if (dx < 48) stick_state &= ~A4B_BIT(A4B_RIGHT);
      }
      if (dy <= -80) {
        stick_state |= A4B_BIT(A4B_UP);
        stick_state &= ~A4B_BIT(A4B_DOWN);
      } else if (dy >= 80) {
        stick_state |= A4B_BIT(A4B_DOWN);
        stick_state &= ~A4B_BIT(A4B_UP);
      } else {
        if (dy > -48) stick_state &= ~A4B_BIT(A4B_UP);
        if (dy < 48) stick_state &= ~A4B_BIT(A4B_DOWN);
      }
    }
    uint32_t vbtn = pad.buttons | stick_state;
    uint32_t pressed = vbtn & ~prev_vbtn;
    uint32_t released = prev_vbtn & ~vbtn;
    prev_vbtn = vbtn;
    uint32_t now = SDL_GetTicks();

    /* Blade switching: edge press + hold-to-repeat, wraps around. */
    int hdir = 0;
    if (pressed & A4B_BIT(A4B_LEFT)) hdir = -1;
    else if (pressed & A4B_BIT(A4B_RIGHT)) hdir = 1;
    if (hdir != 0) {
      h_dir = hdir;
      h_next = now + 400;
    } else if (released & (A4B_BIT(A4B_LEFT) | A4B_BIT(A4B_RIGHT))) {
      h_dir = 0;
    } else if (h_dir != 0 &&
               (vbtn & (A4B_BIT(A4B_LEFT) | A4B_BIT(A4B_RIGHT))) &&
               (int32_t)(now - h_next) >= 0) {
      hdir = h_dir;
      h_next = now + 130;
    }
    if (hdir != 0) {
      blade_target = (blade_target + hdir + BLADE_COUNT) % BLADE_COUNT;
      a4_log("launcher", "blade %d %s", blade_target,
             g_blades[blade_target].title);
      need_draw = 1;
    }

    /* Row movement inside the selected blade: edge + hold-to-repeat. */
    struct Blade* cur = &g_blades[blade_target];
    int vdir = 0;
    if (pressed & A4B_BIT(A4B_UP)) vdir = -1;
    else if (pressed & A4B_BIT(A4B_DOWN)) vdir = 1;
    if (vdir != 0) {
      v_dir = vdir;
      v_next = now + 400;
    } else if (released & (A4B_BIT(A4B_UP) | A4B_BIT(A4B_DOWN))) {
      v_dir = 0;
    } else if (v_dir != 0 &&
               (vbtn & (A4B_BIT(A4B_UP) | A4B_BIT(A4B_DOWN))) &&
               (int32_t)(now - v_next) >= 0) {
      vdir = v_dir;
      v_next = now + 90;
    }
    if (vdir != 0) {
      int rows = blade_rows(cur);
      if (rows > 0) {
        cur->selected = (cur->selected + vdir + rows) % rows;
        if (cur->is_settings) {
          a4_log("launcher", "settings row %d %s", cur->selected,
                 kSettingsRows[cur->selected]);
        } else {
          a4_log("launcher", "select %d/%d %s", cur->selected, cur->count,
                 cur->roms[cur->selected].name);
        }
        need_draw = 1;
      }
    }

    if (pressed & A4B_BIT(A4B_TRIANGLE)) {
      a4_log("launcher", "rescan requested");
      a4_watchdog_phase(A4_PHASE_SCAN);
      scan_all();
      check_blades(0);
      spawn_failed = 0;
      spawn_err[0] = '\0';
      need_draw = 1;
    }
    if ((pressed & A4B_BIT(A4B_CIRCLE)) || pump_quit_event()) {
      a4_log("launcher", "QUIT to XMB by user");
      do_quit = 1;
    }
    if (!do_quit && (pressed & A4B_BIT(A4B_CROSS))) {
      if (cur->is_settings) {
        if (cur->selected == 0) {
          a4_log("launcher", "settings: rescan requested");
          a4_watchdog_phase(A4_PHASE_SCAN);
          scan_all();
          check_blades(0);
          need_draw = 1;
        } else {
          a4_log("launcher", "QUIT to XMB via Settings");
          do_quit = 1;
        }
      } else if (!cur->self_ok) {
        a4_log("launcher", "launch blocked: core not built for %s",
               cur->title);
        snprintf(spawn_err, sizeof spawn_err,
                 "Core not built for %s (see build report)", cur->title);
        spawn_failed = 1;
        need_draw = 1;
      } else if (!cur->bios_ok) {
        a4_log("launcher", "launch blocked: %s missing for %s",
               cur->bios_file, cur->title);
        snprintf(spawn_err, sizeof spawn_err,
                 "Missing BIOS: put %s in %s", cur->bios_file, A4_BIOS_DIR);
        spawn_failed = 1;
        need_draw = 1;
      } else if (cur->live && cur->count > 0) {
        char rompath[512];
        snprintf(rompath, sizeof rompath, "%s/%s", cur->folder,
                 cur->roms[cur->selected].name);
        a4_log("launcher", "launch requested: %s", rompath);
        try_spawn(cur, rompath, spawn_err, sizeof spawn_err);
        spawn_failed = 1;
        need_draw = 1;
        /* Still here: the spawn failed. Stay in the menu. */
      }
    }

    /* Blade slide animation: time-based exponential approach. The
     * blend factor comes from real elapsed time (tau ~45 ms), so the
     * slide takes the same ~200 ms per blade at any frame rate and
     * stays smooth at 60 fps. Snaps when close: the easing always
     * terminates (this was verified by review against the M1 freeze
     * -- the animation cannot spin forever). */
    int animating = 0;
    a4_watchdog_phase(A4_PHASE_ANIM);
    {
      float diff = (float)blade_target - blade_pos;
      if (diff > -0.002f && diff < 0.002f) {
        blade_pos = (float)blade_target;
      } else {
        float alpha = (float)dt_ms / ((float)dt_ms + 45.0f);
        blade_pos += diff * alpha;
        animating = 1;
      }
    }

    /* Dirty rendering (freeze fix): draw + present only when the
     * screen content changed or the slide is still running. At idle
     * the loop keeps pumping input/sysutil at 60 Hz but issues zero
     * presents -- the suspected blocking call is exercised only
     * while the user is actively navigating. */
    if (need_draw || animating) {
      if (first_draw) {
        a4_log("launcher", "blade draw #1 start");
      }
      a4_watchdog_phase(A4_PHASE_DRAW);
      draw_blades(ren, font, blade_pos, blade_target, returned,
                  spawn_failed, spawn_err);
      a4_watchdog_phase(A4_PHASE_PRESENT);
      if (ren) {
        SDL_ClearError();
        SDL_RenderPresent(ren);
      }
      presents++;
      fps_presents++;
      need_draw = 0;
      if (first_draw) {
        a4_log("launcher", "blade draw #1 done (first present returned)");
        first_draw = 0;
      }
    } else {
      a4_watchdog_phase(A4_PHASE_IDLE);
    }
    frames++;

    /* FPS window rollover: once per second, snapshot the counts. */
    if (frame_start - fps_t0 >= 1000) {
      fps_last = fps_frames;
      fps_pres_last = fps_presents;
      fps_max_last = fps_max_ms;
      fps_frames = 0;
      fps_presents = 0;
      fps_max_ms = 0;
      fps_t0 = frame_start;
    }
    if (frames % 180 == 0) {
      a4_log("launcher",
             "menu alive frames=%lu blade=%d (%s) sel=%d count=%d pos=%.2f fps=%u presents/s=%u max_ms=%u",
             frames, blade_target, g_blades[blade_target].title,
             g_blades[blade_target].selected,
             g_blades[blade_target].count, (double)blade_pos,
             fps_last, fps_pres_last, fps_max_last);
    }

    /* Frame pacing: delay only the REMAINING budget. The M1 loop did
     * SDL_Delay(16) after a full draw+present, guaranteeing <60 fps.
     * With dirty rendering the idle frame is microseconds of work +
     * a ~16 ms delay = a true 60 Hz loop. */
    a4_watchdog_phase(A4_PHASE_DELAY);
    uint32_t elapsed = SDL_GetTicks() - frame_start;
    if (elapsed < 16) SDL_Delay(16 - elapsed);
  }

  a4_watchdog_stop();
  a4_font_destroy(font);
  SDL_Quit();
  sysProcessExit(0);
  return 0;
}

/* Atari4PS3 M2 libretro runner (one SELF per system, same sources).
 *
 * Each runner is built from this file with -D overrides (see
 * common/paths.h): A4_CORE_NAME, A4_SAVE_DIR, A4_BIOS_FILE /
 * A4_BIOS_REQUIRED, A4_WARN_SLOW, A4_HINT_KEY, A4_CORE_VARS. The
 * defaults are the Stella (2600) runner.
 *
 * - SDL2 video: the core's software framebuffer is uploaded as a
 *   streaming texture and drawn scaled to 720p, preserving the core's
 *   aspect ratio. Accepted pixel formats: XRGB8888, RGB565, 0RGB1555
 *   (cores negotiate down: Handy tries all three, ProSystem falls
 *   back to RGB565).
 * - SDL2 audio: the core's stereo output is linear-resampled to
 *   48 kHz into a ring buffer drained by the SDL audio callback.
 * - Input: DualShock 3 via ioPad, mapped to the libretro Retropad.
 *   SELECT+START held for 1 s quits back to the launcher. The PS
 *   button quits the app to XMB (sysutil callback, M1 fix) instead of
 *   spawning back to the launcher.
 * - ROM resolution: argv[1] first, handshake file second. The
 *   handshake carries core name (line 1, verified), ROM path (line
 *   2), and a system hint (line 3, atari800 only). Cores that declare
 *   need_fullpath (atari800, Hatari, DOSBox Pure) -- and CD images
 *   for any core -- are loaded by path only, never read into RAM.
 * - Save RAM: loaded from <save-dir>/<rom>.srm before the run and
 *   flushed back on exit, when the core exposes RETRO_MEMORY_SAVE_RAM.
 * - Watchdog hooks (common/watchdog.h): the game loop and wait
 *   screens tick + phase-mark, so a freeze inside retro_run or a
 *   present leaves evidence in watchdog.log (same fix as the
 *   launcher's M1 freeze).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include <lv2/process.h>
#include "SDL.h"
#include "libretro.h"

#include "common/paths.h"
#include "common/log.h"
#include "common/fsutil.h"
#include "common/pad.h"
#include "common/spawn.h"
#include "common/sysutil.h"
#include "common/watchdog.h"
#include "common/audio_ring.h"
#include "common/runner_policy.h"
#include "launcher/font_text.h"

/* ---------- core log forwarding ---------- */

static void core_log(enum retro_log_level level, const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  size_t n = strlen(buf);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
  (void)level;
  a4_log("core", "%s", buf);
}

/* ---------- state ---------- */

static SDL_Window* g_win = NULL;
static SDL_Renderer* g_ren = NULL;
static SDL_Texture* g_tex = NULL;
static struct A4Font* g_font = NULL;
static unsigned g_tex_w = 0, g_tex_h = 0;
static struct retro_system_av_info g_av;
static int g_vsync = 0;
static enum retro_pixel_format g_pix_fmt = RETRO_PIXEL_FORMAT_XRGB8888;

static const void* g_frame = NULL;
static unsigned g_frame_w = 0, g_frame_h = 0;
static size_t g_frame_pitch = 0;
static int g_frame_ready = 0;

static struct A4Pad g_pad;
static int g_quit = 0;
static int g_sys_exit = 0; /* PS-button quit: exit to XMB, no spawn-back */
/* Short reason for a run_game failure, shown on the load-error screen
 * (hardening pass: failures must be visible, not just logged). */
static char g_load_error[96] = "";
static uint32_t g_quit_hold_t0 = 0;
static unsigned g_frames_run = 0;
static char g_hint[64] = ""; /* handshake line 3 (atari800 system) */

/* Audio: single-producer (main thread, via the core's batch callback) /
 * single-consumer (SDL audio thread) ring of stereo frames. */
static A4AudioRing<32768,24000> g_audio_ring;
static double g_resample_step = 1.0; /* input frames per output frame */
static double g_resample_phase = 0.0;
static int16_t g_prev_l = 0, g_prev_r = 0;
static int g_have_prev = 0;
static uint32_t g_audio_pushed = 0, g_audio_dropped = 0, g_audio_underrun = 0;
static SDL_AudioDeviceID g_audio_dev = 0;

static Uint32 sdl_fmt_for(enum retro_pixel_format fmt) {
  switch (fmt) {
    case RETRO_PIXEL_FORMAT_RGB565: return SDL_PIXELFORMAT_RGB565;
    case RETRO_PIXEL_FORMAT_0RGB1555: return SDL_PIXELFORMAT_ARGB1555;
    case RETRO_PIXEL_FORMAT_XRGB8888:
    default: return SDL_PIXELFORMAT_ARGB8888;
  }
}

static void update_texture(void) {
  unsigned mw = g_av.geometry.max_width, mh = g_av.geometry.max_height;
  if (mw == 0 || mh == 0) {
    mw = g_av.geometry.base_width;
    mh = g_av.geometry.base_height;
  }
  if (mw == 0 || mh == 0) {
    mw = 640;
    mh = 312;
  }
  if (g_tex && mw == g_tex_w && mh == g_tex_h) return;
  if (g_tex) SDL_DestroyTexture(g_tex);
  g_tex = NULL;
  if (g_ren) {
    g_tex = SDL_CreateTexture(g_ren, sdl_fmt_for(g_pix_fmt),
                              SDL_TEXTUREACCESS_STREAMING, (int)mw, (int)mh);
    if (g_tex) {
      SDL_SetTextureBlendMode(g_tex, SDL_BLENDMODE_NONE);
      g_tex_w = mw;
      g_tex_h = mh;
    }
  }
  a4_log("runner", "texture %ux%u fmt=%d tex=%p err=%s", mw, mh,
         (int)g_pix_fmt, (void*)g_tex, SDL_GetError());
}

/* ---------- core option answering ---------- */

/* Value buffers for GET_VARIABLE answers: a small ring so a core
 * holding a previous answer's pointer is not clobbered by the next
 * query (cores read the value synchronously, but the ring costs
 * nothing). */
#define A4_VAR_BUFS 4
#define A4_VAR_BUFSZ 128
static char g_var_bufs[A4_VAR_BUFS][A4_VAR_BUFSZ];
static int g_var_buf_next = 0;

/* Keys already logged (queried-key-once diagnostics). */
#define A4_VAR_LOGGED_MAX 32
static char g_var_logged[A4_VAR_LOGGED_MAX][64];
static int g_var_logged_n = 0;

static void log_var_once(const char* key, const char* value) {
  for (int i = 0; i < g_var_logged_n; i++) {
    if (strcmp(g_var_logged[i], key) == 0) return;
  }
  if (g_var_logged_n < A4_VAR_LOGGED_MAX) {
    size_t n = strlen(key);
    if (n >= sizeof g_var_logged[0]) n = sizeof g_var_logged[0] - 1;
    memcpy(g_var_logged[g_var_logged_n], key, n);
    g_var_logged[g_var_logged_n][n] = '\0';
    g_var_logged_n++;
  }
  if (value)
    a4_log("runner", "var %s = \"%s\"", key, value);
  else
    a4_log("runner", "var %s -> (core default)", key);
}

/* Answer a core option query. Order: (1) the handshake hint for
 * A4_HINT_KEY, (2) the static A4_CORE_VARS "k=v;k=v" table, (3) no
 * answer (the core uses its built-in default, exactly like M0/M1).
 * Returns 1 + sets *value_out when answered. */
static int var_lookup(const char* key, const char** value_out) {
  if (A4_HINT_KEY[0] && g_hint[0] && strcmp(key, A4_HINT_KEY) == 0) {
    *value_out = g_hint;
    return 1;
  }
  const char* t = A4_CORE_VARS;
  size_t klen = strlen(key);
  while (*t) {
    const char* eq = strchr(t, '=');
    if (!eq) break;
    if ((size_t)(eq - t) == klen && strncmp(key, t, klen) == 0) {
      const char* vs = eq + 1;
      const char* vend = strchr(vs, ';');
      size_t vlen = vend ? (size_t)(vend - vs) : strlen(vs);
      char* buf = g_var_bufs[g_var_buf_next];
      g_var_buf_next = (g_var_buf_next + 1) % A4_VAR_BUFS;
      if (vlen >= A4_VAR_BUFSZ) vlen = A4_VAR_BUFSZ - 1;
      memcpy(buf, vs, vlen);
      buf[vlen] = '\0';
      *value_out = buf;
      return 1;
    }
    const char* semi = strchr(eq, ';');
    if (!semi) break;
    t = semi + 1;
  }
  return 0;
}

/* ---------- libretro environment ---------- */

static bool env_cb(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
      struct retro_log_callback* cb = (struct retro_log_callback*)data;
      cb->log = core_log;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable* var = (struct retro_variable*)data;
      if (!var || !var->key) return false;
      const char* value = NULL;
      if (var_lookup(var->key, &value)) {
        var->value = value;
        log_var_once(var->key, value);
        return true;
      }
      log_var_once(var->key, NULL);
      return false; /* core uses its built-in default */
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      if (data) *(bool*)data = false;
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return false; /* core polls buttons individually */
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
      enum retro_pixel_format fmt = *(enum retro_pixel_format*)data;
      if (fmt == RETRO_PIXEL_FORMAT_XRGB8888 ||
          fmt == RETRO_PIXEL_FORMAT_RGB565 ||
          fmt == RETRO_PIXEL_FORMAT_0RGB1555) {
        g_pix_fmt = fmt;
        a4_log("runner", "pixel format %d accepted", (int)fmt);
        return true;
      }
      a4_log("runner", "pixel format %d rejected", (int)fmt);
      return false;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      if (data) *(const char**)data = A4_BIOS_DIR;
      return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      if (data) *(const char**)data = a4_runner_save_dir(A4_CORE_NAME,g_hint,A4_SAVE_DIR);
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      retro_get_system_av_info(&g_av);
      update_texture();
      a4_log("runner", "geometry update: base %ux%u max %ux%u aspect %.3f fps %.2f rate %.0f",
             g_av.geometry.base_width, g_av.geometry.base_height,
             g_av.geometry.max_width, g_av.geometry.max_height,
             g_av.geometry.aspect_ratio, g_av.timing.fps, g_av.timing.sample_rate);
      return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      if (data) *(int*)data = 1 | 2; /* audio + video, no runahead bit */
      return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
      if (data) *(unsigned*)data = RETRO_LANGUAGE_ENGLISH;
      return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      if (data) *(bool*)data = true;
      return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:
      /* The core asked to exit (e.g. DOSBox's own exit, Stella's
       * quit): honor it by ending the run and returning to the
       * launcher. Ignoring it left the game running with no visible
       * effect. */
      a4_log("runner", "core requested SHUTDOWN");
      g_quit = 1;
      return true;
    default:
      return false;
  }
}

/* ---------- libretro video/audio/input callbacks ---------- */

static void video_cb(const void* data, unsigned width, unsigned height, size_t pitch) {
  if (!data || data == RETRO_HW_FRAME_BUFFER_VALID) return;
  g_frame = data;
  g_frame_w = width;
  g_frame_h = height;
  g_frame_pitch = pitch;
  g_frame_ready = 1;
}

static void ring_push(int16_t l, int16_t r) {
  if(g_audio_ring.push(l,r)) g_audio_pushed++;
  else g_audio_dropped++;
}

static void resample_frame(int16_t l, int16_t r) {
  if (!g_have_prev) {
    g_prev_l = l;
    g_prev_r = r;
    g_have_prev = 1;
    g_resample_phase = 0.0;
    return;
  }
  while (g_resample_phase < 1.0) {
    double t = g_resample_phase;
    int32_t ol = (int32_t)(g_prev_l + (l - g_prev_l) * t);
    int32_t orr = (int32_t)(g_prev_r + (r - g_prev_r) * t);
    ring_push((int16_t)ol, (int16_t)orr);
    g_resample_phase += g_resample_step;
  }
  g_resample_phase -= 1.0;
  g_prev_l = l;
  g_prev_r = r;
}

static size_t audio_batch_cb(const int16_t* data, size_t frames) {
  for (size_t i = 0; i < frames; i++)
    resample_frame(data[2 * i], data[2 * i + 1]);
  return frames; /* all input frames consumed; ring drops are output-side */
}

static void audio_sample_cb(int16_t left, int16_t right) {
  resample_frame(left, right);
}

static void sdl_audio_cb(void* userdata, Uint8* stream, int len) {
  (void)userdata;
  int frames = len / 4;
  int16_t* out = (int16_t*)stream;
  for (int i = 0; i < frames; i++) {
    if (g_audio_ring.pop(out[2*i],out[2*i+1])) {
      /* The complete stereo frame was acquired from the producer. */
    } else {
      out[2 * i] = 0;
      out[2 * i + 1] = 0;
      g_audio_underrun++;
    }
  }
}

static void input_poll_cb(void) {
  a4_pad_poll(&g_pad);
  /* SELECT+START held 1 s quits to the launcher (same combo as SalsaDS). */
  uint32_t both = A4B_BIT(A4B_SELECT) | A4B_BIT(A4B_START);
  if ((g_pad.buttons & both) == both) {
    uint32_t now = SDL_GetTicks();
    if (!g_quit_hold_t0)
      g_quit_hold_t0 = now;
    else if (now - g_quit_hold_t0 >= 1000)
      g_quit = 1;
  } else {
    g_quit_hold_t0 = 0;
  }
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (port != 0) return 0;
  uint32_t b = g_pad.buttons;
  if (device == RETRO_DEVICE_JOYPAD) {
    switch (id) {
      case RETRO_DEVICE_ID_JOYPAD_B: return (b & A4B_BIT(A4B_CROSS)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_A: return (b & A4B_BIT(A4B_CIRCLE)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_X: return (b & A4B_BIT(A4B_SQUARE)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_Y: return (b & A4B_BIT(A4B_TRIANGLE)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_SELECT: return (b & A4B_BIT(A4B_SELECT)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_START: return (b & A4B_BIT(A4B_START)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_UP: return (b & A4B_BIT(A4B_UP)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_DOWN: return (b & A4B_BIT(A4B_DOWN)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_LEFT: return (b & A4B_BIT(A4B_LEFT)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_RIGHT: return (b & A4B_BIT(A4B_RIGHT)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_L: return (b & A4B_BIT(A4B_L1)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_R: return (b & A4B_BIT(A4B_R1)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_L2: return (b & A4B_BIT(A4B_L2)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_R2: return (b & A4B_BIT(A4B_R2)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_L3: return (b & A4B_BIT(A4B_L3)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_R3: return (b & A4B_BIT(A4B_R3)) ? 1 : 0;
      case RETRO_DEVICE_ID_JOYPAD_MASK: {
        int16_t m = 0;
        if (b & A4B_BIT(A4B_CROSS)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_B;
        if (b & A4B_BIT(A4B_CIRCLE)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_A;
        if (b & A4B_BIT(A4B_SQUARE)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_X;
        if (b & A4B_BIT(A4B_TRIANGLE)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_Y;
        if (b & A4B_BIT(A4B_SELECT)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT;
        if (b & A4B_BIT(A4B_START)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_START;
        if (b & A4B_BIT(A4B_UP)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
        if (b & A4B_BIT(A4B_DOWN)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
        if (b & A4B_BIT(A4B_LEFT)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (b & A4B_BIT(A4B_RIGHT)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
        if (b & A4B_BIT(A4B_L1)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_L;
        if (b & A4B_BIT(A4B_R1)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_R;
        if (b & A4B_BIT(A4B_L2)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_L2;
        if (b & A4B_BIT(A4B_R2)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_R2;
        if (b & A4B_BIT(A4B_L3)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_L3;
        if (b & A4B_BIT(A4B_R3)) m |= 1 << RETRO_DEVICE_ID_JOYPAD_R3;
        return m;
      }
      default: return 0;
    }
  }
  if (device == RETRO_DEVICE_ANALOG) {
    int axis = 128;
    if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
      if (id == RETRO_DEVICE_ID_ANALOG_X) axis = g_pad.lx;
      else if (id == RETRO_DEVICE_ID_ANALOG_Y) axis = g_pad.ly;
      else return 0;
    } else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
      if (id == RETRO_DEVICE_ID_ANALOG_X) axis = g_pad.rx;
      else if (id == RETRO_DEVICE_ID_ANALOG_Y) axis = g_pad.ry;
      else return 0;
    } else {
      return 0;
    }
    return (int16_t)((axis - 128) * 256);
  }
  return 0;
}

/* ---------- placeholder / message screens ---------- */

/* Solid screen + accent rect until a button press, SDL_QUIT, PS quit,
 * or timeout. Returns 0 on button/quit, 1 on timeout. A PS quit sets
 * g_sys_exit so the caller exits to XMB instead of spawning back. */
static int wait_screen(uint8_t r, uint8_t gg, uint8_t b, uint32_t timeout_ms) {
  uint32_t t0 = SDL_GetTicks();
  uint32_t prev = 0;
  for (;;) {
    a4_watchdog_tick();
    a4_watchdog_phase(A4_PHASE_WAIT);
    if (a4_sysutil_poll()) {
      g_sys_exit = 1;
      return 0;
    }
    struct A4Pad pad;
    a4_pad_poll(&pad);
    uint32_t pressed = pad.buttons & ~prev;
    prev = pad.buttons;
    if (pressed & (A4B_BIT(A4B_CROSS) | A4B_BIT(A4B_CIRCLE) | A4B_BIT(A4B_START)))
      return 0;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) { g_sys_exit=1; return 0; }
    }
    if (timeout_ms && SDL_GetTicks() - t0 >= timeout_ms) return 1;
    if (g_ren) {
      SDL_SetRenderDrawColor(g_ren, r, gg, b, 255);
      SDL_RenderClear(g_ren);
      SDL_Rect acc = { 540, 310, 200, 100 };
      SDL_SetRenderDrawColor(g_ren, 255, 210, 60, 255);
      SDL_RenderFillRect(g_ren, &acc);
      a4_watchdog_phase(A4_PHASE_PRESENT);
      SDL_RenderPresent(g_ren);
    }
    SDL_Delay(16);
  }
}

/* Text message screen (BIOS-missing, performance warning). Same exit
 * semantics as wait_screen. Falls back to the rects-only screen when
 * the font atlas is unavailable. */
static int wait_screen_msg(const char* title, const char* sub1,
                           const char* sub2, uint8_t r, uint8_t gg,
                           uint8_t b, uint32_t timeout_ms) {
  if (!g_ren || !g_font) return wait_screen(r, gg, b, timeout_ms);
  uint32_t t0 = SDL_GetTicks();
  uint32_t prev = 0;
  for (;;) {
    a4_watchdog_tick();
    a4_watchdog_phase(A4_PHASE_WAIT);
    if (a4_sysutil_poll()) {
      g_sys_exit = 1;
      return 0;
    }
    struct A4Pad pad;
    a4_pad_poll(&pad);
    uint32_t pressed = pad.buttons & ~prev;
    prev = pad.buttons;
    if (pressed & (A4B_BIT(A4B_CROSS) | A4B_BIT(A4B_CIRCLE) | A4B_BIT(A4B_START)))
      return 0;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) { g_sys_exit=1; return 0; }
    }
    if (timeout_ms && SDL_GetTicks() - t0 >= timeout_ms) return 1;
    a4_watchdog_phase(A4_PHASE_DRAW);
    SDL_SetRenderDrawColor(g_ren, r, gg, b, 255);
    SDL_RenderClear(g_ren);
    if (title) {
      int w = a4_text_width(title, 3);
      a4_draw_text(g_font, g_ren, (1280 - w) / 2, 240, title, 3, 255, 210, 60);
    }
    if (sub1) {
      int w = a4_text_width(sub1, 2);
      a4_draw_text(g_font, g_ren, (1280 - w) / 2, 320, sub1, 2, 240, 240, 240);
    }
    if (sub2) {
      int w = a4_text_width(sub2, 2);
      a4_draw_text(g_font, g_ren, (1280 - w) / 2, 356, sub2, 2, 240, 240, 240);
    }
    {
      const char* hint = "Press any button to continue";
      int w = a4_text_width(hint, 1);
      a4_draw_text(g_font, g_ren, (1280 - w) / 2, 440, hint, 1, 180, 180, 180);
    }
    a4_watchdog_phase(A4_PHASE_PRESENT);
    SDL_RenderPresent(g_ren);
    SDL_Delay(16);
  }
}

/* ---------- save RAM ---------- */

/* <save-dir>/<rom-basename>.srm */
static void sram_path_for(const char* rompath, char* out, size_t out_size) {
  const char* base = strrchr(rompath, '/');
  base = base ? base + 1 : rompath;
  const char* dot = strrchr(base, '.');
  size_t nlen = dot ? (size_t)(dot - base) : strlen(base);
  const char* save_dir=a4_runner_save_dir(A4_CORE_NAME,g_hint,A4_SAVE_DIR);
  size_t dlen = strlen(save_dir);
  if (dlen + 1 + nlen + 4 >= out_size) {
    out[0] = '\0';
    return;
  }
  memcpy(out, save_dir, dlen);
  out[dlen] = '/';
  memcpy(out + dlen + 1, base, nlen);
  memcpy(out + dlen + 1 + nlen, ".srm", 5);
}

/* ---------- game run ---------- */

static int ends_with_ci_local(const char* name, const char* ext) {
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

/* CD images are always path-loaded, even for cores whose
 * retro_get_system_info says need_fullpath=false (Virtual Jaguar
 * declares CD extensions path-loaded via a content-info override the
 * runner does not implement; loading a multi-hundred-MB image into
 * RAM would also be wasteful). */
static int is_cd_image(const char* rompath) {
  return ends_with_ci_local(rompath, ".cue") ||
         ends_with_ci_local(rompath, ".cdi") ||
         ends_with_ci_local(rompath, ".chd") ||
         ends_with_ci_local(rompath, ".gdi");
}

static int run_game(const char* rompath) {
  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_sample_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);
  retro_init();

  struct retro_system_info sysinfo;
  retro_get_system_info(&sysinfo);
  a4_log("runner", "core: %s %s exts=%s fullpath=%d",
         sysinfo.library_name ? sysinfo.library_name : "?",
         sysinfo.library_version ? sysinfo.library_version : "?",
         sysinfo.valid_extensions ? sysinfo.valid_extensions : "?",
         (int)sysinfo.need_fullpath);

  /* Load method: path-only when the core demands it or the content
   * is a CD image; otherwise read the file into memory. */
  int path_only = sysinfo.need_fullpath || is_cd_image(rompath);
  unsigned char* data = NULL;
  size_t size = 0;
  if (!path_only) {
    if (!a4_read_file(rompath, &data, &size)) {
      a4_log("runner", "cannot read ROM %s", rompath);
      snprintf(g_load_error, sizeof g_load_error,
               "Could not read the ROM file");
      retro_deinit();
      return 0;
    }
    a4_log("runner", "ROM %s (%u bytes, in-memory)", rompath, (unsigned)size);
  } else {
    if (!a4_is_file(rompath)) {
      a4_log("runner", "ROM file missing: %s", rompath);
      snprintf(g_load_error, sizeof g_load_error, "ROM file is missing");
      retro_deinit();
      return 0;
    }
    a4_log("runner", "ROM %s (path-only load)", rompath);
  }

  struct retro_game_info gi;
  memset(&gi, 0, sizeof gi);
  gi.path = rompath;
  gi.data = data;
  gi.size = size;
  a4_watchdog_phase(A4_PHASE_LOAD);
  if (!retro_load_game(&gi)) {
    a4_log("runner", "retro_load_game FAILED");
    snprintf(g_load_error, sizeof g_load_error,
             "The core rejected this file");
    free(data);
    retro_deinit();
    return 0;
  }
  retro_get_system_av_info(&g_av);
  a4_log("runner", "av: fps=%.2f rate=%.0f base=%ux%u max=%ux%u aspect=%.3f",
         g_av.timing.fps, g_av.timing.sample_rate,
         g_av.geometry.base_width, g_av.geometry.base_height,
         g_av.geometry.max_width, g_av.geometry.max_height,
         g_av.geometry.aspect_ratio);
  update_texture();

  /* Save RAM in: the core's battery-backed memory, restored from the
   * per-system save dir when a previous session left one. */
  char sram_path[512] = "";
  size_t sram_size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
  void* sram = sram_size ? retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) : NULL;
  if (sram && sram_size > 0) {
    sram_path_for(rompath, sram_path, sizeof sram_path);
    unsigned char* saved = NULL;
    size_t saved_size = 0;
    if (sram_path[0] && a4_read_file(sram_path, &saved, &saved_size)) {
      size_t n = saved_size < sram_size ? saved_size : sram_size;
      memcpy(sram, saved, n);
      free(saved);
      a4_log("runner", "save RAM: loaded %u of %u bytes from %s",
             (unsigned)n, (unsigned)sram_size, sram_path);
    } else {
      a4_log("runner", "save RAM: %u bytes, no save file (fresh)",
             (unsigned)sram_size);
    }
  } else {
    a4_log("runner", "save RAM: none exposed by core");
  }

  g_resample_step = g_av.timing.sample_rate > 0 ? g_av.timing.sample_rate / 48000.0 : 1.0;
  SDL_AudioSpec want, got;
  memset(&want, 0, sizeof want);
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = sdl_audio_cb;
  g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
  if (g_audio_dev) {
    a4_log("runner", "audio: got %d Hz fmt=%u ch=%d samples=%d",
           (int)got.freq, (unsigned)got.format, (int)got.channels, (int)got.samples);
    SDL_PauseAudioDevice(g_audio_dev, 0);
  } else {
    a4_log("runner", "audio open FAILED: %s", SDL_GetError());
  }

  if (g_ren) {
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(g_ren, &info) == 0)
      g_vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
  }
  a4_log("runner", "vsync=%d", g_vsync);

  double frame_ms = g_av.timing.fps > 0 ? 1000.0 / g_av.timing.fps : 16.666;
  while (!g_quit) {
    uint32_t t0 = SDL_GetTicks();
    a4_watchdog_tick();
    g_frame_ready = 0;
    a4_watchdog_phase(A4_PHASE_RUN);
    retro_run();
    g_frames_run++;
    if (g_frame_ready && g_tex) {
      SDL_Rect src = { 0, 0, (int)g_frame_w, (int)g_frame_h };
      if (src.w > (int)g_tex_w) src.w = (int)g_tex_w;
      if (src.h > (int)g_tex_h) src.h = (int)g_tex_h;
      SDL_UpdateTexture(g_tex, &src, g_frame, (int)g_frame_pitch);
    }
    if (g_ren) {
      a4_watchdog_phase(A4_PHASE_DRAW);
      SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
      SDL_RenderClear(g_ren);
      if (g_tex && g_frame_w > 0 && g_frame_h > 0) {
        SDL_Rect src = { 0, 0, (int)g_frame_w, (int)g_frame_h };
        if (src.w > (int)g_tex_w) src.w = (int)g_tex_w;
        if (src.h > (int)g_tex_h) src.h = (int)g_tex_h;
        double asp = g_av.geometry.aspect_ratio;
        if (asp <= 0.0) asp = 4.0 / 3.0;
        int dw = 1280, dh = 720;
        if (1280.0 / 720.0 > asp) dw = (int)(720.0 * asp);
        else dh = (int)(1280.0 / asp);
        SDL_Rect dst = { (1280 - dw) / 2, (720 - dh) / 2, dw, dh };
        SDL_RenderCopy(g_ren, g_tex, &src, &dst);
      }
      a4_watchdog_phase(A4_PHASE_PRESENT);
      SDL_RenderPresent(g_ren);
    }
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) g_quit = 1;
    }
    if (a4_sysutil_poll()) {
      g_sys_exit = 1;
      g_quit = 1;
    }
    if (!g_vsync) {
      uint32_t elapsed = SDL_GetTicks() - t0;
      if (elapsed < frame_ms) SDL_Delay((uint32_t)(frame_ms - elapsed));
    }
  }

  if (g_audio_dev) { SDL_CloseAudioDevice(g_audio_dev); g_audio_dev=0; }

  a4_log("runner", "game loop exit: frames=%u audio pushed=%u dropped=%u underrun=%u",
         g_frames_run, g_audio_pushed, g_audio_dropped, g_audio_underrun);

  /* Save RAM out: flush before unload so the next session restores it. */
  if (sram && sram_size > 0 && sram_path[0]) {
    if (a4_write_file(sram_path, sram, sram_size))
      a4_log("runner", "save RAM: flushed %u bytes to %s",
             (unsigned)sram_size, sram_path);
    else
      a4_log("runner", "save RAM: flush FAILED for %s", sram_path);
  }

  retro_unload_game();
  retro_deinit();
  free(data);
  return 1;
}

/* ---------- entry ---------- */

int main(int argc, char** argv) {
  a4_log_init();
  {
    char ab[512];
    size_t off = 0;
    ab[0] = '\0';
    for (int i = 0; i < argc && off < sizeof ab - 1; i++) {
      int w = snprintf(ab + off, sizeof ab - off, "[%d]=%s ", i, argv[i] ? argv[i] : "(null)");
      if (w > 0) off += (size_t)w;
    }
    a4_log("runner", "BOOT core=%s argc=%d %s", A4_CORE_NAME, argc, ab);
  }
  a4_ensure_dirs();

  /* ROM resolution: argv[1] first, handshake file second. The logged
   * source is the evidence for which spawn mechanism delivered the
   * path. The handshake is ALWAYS parsed when present: line 1 (core
   * name) is verified against this runner, and line 3 (system hint)
   * is picked up even when the ROM itself came via argv. A core
   * mismatch means a stale handshake from another system's launch:
   * its path and hint are ignored (the argv ROM, if any, still wins).
   */
  char rompath[512] = "";
  const char* source = "none";
  int handshake_ok = 0;
  if (argc > 1 && argv[1] && argv[1][0] && a4_is_file(argv[1])) {
    snprintf(rompath, sizeof rompath, "%s", argv[1]);
    source = "argv";
  }
  {
    unsigned char* hs = NULL;
    size_t hs_size = 0;
    if (a4_read_file(A4_LAUNCH_FILE, &hs, &hs_size)) {
      /* NUL-terminate a copy for line splitting (the file may not
       * end with a newline). */
      char* s = (char*)malloc(hs_size + 1);
      if (s) {
        memcpy(s, hs, hs_size);
        s[hs_size] = '\0';
        char* l1 = s;
        char* nl1 = strchr(l1, '\n');
        char* l2 = NULL;
        char* l3 = NULL;
        if (nl1) {
          *nl1 = '\0';
          l2 = nl1 + 1;
          char* nl2 = strchr(l2, '\n');
          if (nl2) {
            *nl2 = '\0';
            l3 = nl2 + 1;
            char* nl3 = strchr(l3, '\n');
            if (nl3) *nl3 = '\0';
          }
        }
        /* Strip CRs (handshake is LF-only, but be liberal). */
        char* lines[3] = { l1, l2, l3 };
        for (int i = 0; i < 3; i++) {
          if (!lines[i]) continue;
          size_t L = strlen(lines[i]);
          if (L > 0 && lines[i][L - 1] == '\r') lines[i][L - 1] = '\0';
        }
        if (l1[0] && strcmp(l1, A4_CORE_NAME) == 0) {
          handshake_ok = 1;
          if (l3 && l3[0]) {
            size_t n = strlen(l3);
            if (n >= sizeof g_hint) n = sizeof g_hint - 1;
            memcpy(g_hint, l3, n);
            g_hint[n] = '\0';
          }
          if (!rompath[0] && l2 && l2[0] && a4_is_file(l2)) {
            snprintf(rompath, sizeof rompath, "%s", l2);
            source = "handshake";
          }
        } else if (l1[0]) {
          a4_log("runner",
                 "handshake core mismatch: expected %s, file says %s (ignored)",
                 A4_CORE_NAME, l1);
        }
        free(s);
      }
      free(hs);
    }
  }
  a4_log("runner", "rom source=%s handshake_ok=%d hint=%s path=%s", source,
         handshake_ok, g_hint[0] ? g_hint : "(none)",
         rompath[0] ? rompath : "(none)");

  /* PS-button handling (M1 fix): without a registered sysutil callback,
   * pressing PS does nothing. Pumped in the game loop + wait screens. */
  a4_sysutil_init();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    a4_log("runner", "SDL_Init FAILED: %s", SDL_GetError());
  /* Pad init AFTER SDL_Init (Neon Lounge order; the M0 order saw no
   * input on hardware). */
  a4_pad_init();
  {
    char ps[96];
    a4_pad_status_line(ps, sizeof ps);
    a4_log("runner", "pad init rc=%d %s", a4_pad_init_result(), ps);
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  g_win = SDL_CreateWindow("Atari4PS3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           1280, 720, SDL_WINDOW_FULLSCREEN);
  /* No PRESENTVSYNC: the M0 launcher's vsync renderer hung on the first
   * SDL_RenderPresent on hardware (black screen). Frame pacing comes
   * from SDL_Delay (see g_vsync handling in run_game).
   * ROUND 2: the launcher ALSO hung inside its first draw with the
   * PSL1GHT accelerated renderer (maxtex=0x0) after vsync was removed,
   * so the accelerated renderer itself is suspect. Try SOFTWARE first
   * here too -- frames are small, CPU upload is cheap -- then
   * ACCELERATED, then default. */
  if (g_win) {
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    a4_log("runner", "renderer try SOFTWARE -> %p err=%s", (void*)g_ren, SDL_GetError());
  }
  if (!g_ren && g_win) {
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    a4_log("runner", "renderer try ACCELERATED -> %p err=%s", (void*)g_ren, SDL_GetError());
  }
  if (!g_ren && g_win) {
    g_ren = SDL_CreateRenderer(g_win, -1, 0);
    a4_log("runner", "renderer try DEFAULT -> %p err=%s", (void*)g_ren, SDL_GetError());
  }
  a4_log("runner", "video win=%p ren=%p err=%s", (void*)g_win, (void*)g_ren, SDL_GetError());
  if (g_ren) {
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(g_ren, &info) == 0) {
      a4_log("runner", "renderer name=%s flags=0x%x maxtex=%dx%d",
             info.name ? info.name : "?", (unsigned)info.flags,
             info.max_texture_width, info.max_texture_height);
    }
  }
  g_font = a4_font_create(g_ren);
  a4_log("runner", "font atlas=%p err=%s", (void*)g_font, SDL_GetError());

  /* Freeze watchdog (same fix as the launcher). Started after SDL
   * init, before any loop that can hang. */
  a4_watchdog_start("runner");

  int played = 0;
  int bios_blocked = 0;
  if (rompath[0]) {
    /* Required-BIOS gate: a clean message + return to the launcher,
     * instead of starting a core that cannot boot. */
    if (A4_BIOS_REQUIRED && A4_BIOS_FILE[0]) {
      char biospath[512];
      snprintf(biospath, sizeof biospath, "%s/%s", A4_BIOS_DIR, A4_BIOS_FILE);
      if (!a4_is_file(biospath)) {
        bios_blocked = 1;
        a4_log("runner", "BIOS MISSING: %s (required by %s)", biospath,
               A4_CORE_NAME);
        char sub[96];
        snprintf(sub, sizeof sub, "Put %s in:", A4_BIOS_FILE);
        wait_screen_msg("BIOS MISSING", sub, A4_BIOS_DIR, 120, 20, 30, 8000);
      }
    }
    /* Performance warning (Jaguar): shown at boot, any button skips. */
    if (!bios_blocked && A4_WARN_SLOW) {
      a4_log("runner", "performance warning shown (Jaguar may run slow)");
      wait_screen_msg("PERFORMANCE WARNING",
                      "Jaguar emulation may run below",
                      "full speed on PS3.", 40, 30, 10, 4000);
    }
    if (!bios_blocked && !g_sys_exit) {
      played = run_game(rompath);
      if (!played) {
        a4_log("runner", "showing load-error screen: %s", g_load_error);
        wait_screen_msg("LOAD FAILED",
                        g_load_error[0] ? g_load_error : "Unknown load error",
                        "See m0.log for details.", 150, 30, 60, 8000);
      }
    }
  } else {
    a4_log("runner", "no ROM: showing no-ROM screen (any button returns, 30s timeout)");
    int why = wait_screen_msg("NO ROM RECEIVED",
                              "The launcher sent no game path.",
                              "Check m0.log, then press any button.",
                              30, 40, 120, 30000);
    a4_log("runner", "no-ROM screen exit: %s", why ? "timeout" : "button");
  }

  /* PS quit means "leave the app": exit to XMB, do NOT spawn back to
   * the launcher (the user asked to quit, not to return). */
  if (g_sys_exit || a4_sysutil_exit_requested()) {
    a4_log("runner", "PS quit: exiting to XMB (no spawn-back)");
    a4_watchdog_stop();
    a4_font_destroy(g_font);
    SDL_Quit();
    sysProcessExit(0);
    return 0;
  }

  {
    char rb[256];
    snprintf(rb, sizeof rb, "core=%s source=%s played=%d frames=%u audio pushed=%u dropped=%u underrun=%u\n",
             A4_CORE_NAME, source, played, g_frames_run,
             g_audio_pushed, g_audio_dropped, g_audio_underrun);
    a4_write_file(A4_RETURN_FILE, rb, strlen(rb));
  }
  {
    char detail[320];
    int check = a4_spawn_check(A4_LAUNCHER_EBOOT, detail, sizeof detail);
    a4_log("runner", "pre-spawn-back check=%d %s", check, detail);
  }
  a4_watchdog_stop();
  a4_log("runner", "RETURN to launcher %s", A4_LAUNCHER_EBOOT);
  a4_spawn(A4_LAUNCHER_EBOOT, "returned");

  a4_log("runner", "SPAWN BACK FAILED (sysProcessExitSpawn2 returned); exiting to XMB");
  a4_font_destroy(g_font);
  SDL_Quit();
  sysProcessExit(0);
  return 0;
}

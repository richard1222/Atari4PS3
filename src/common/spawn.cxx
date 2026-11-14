#include "common/spawn.h"

#include <lv2/process.h>
#include <sys/process.h>
#include <stddef.h>
#include <stdio.h>

void a4_spawn(const char* path, const char* arg1) {
  const char* argv[3];
  argv[0] = path;
  argv[1] = arg1 ? arg1 : "";
  argv[2] = NULL;
  /* Priority 1001, 1 MiB main-thread stack for the new process. This
   * call returns only on error; on success the current process is
   * replaced. 1001 is the priority every known-working exitspawn2
   * caller uses (PS3LoadX, Cobra ntfs_ext_iso examples) and matches
   * LV2's normal process priority; the M0/M2 builds passed 0, which
   * no working example uses. Fixed in the hardening pass, 2026-11-14. */
  sysProcessExitSpawn2(path, argv, NULL, NULL, 0, 1001,
                       SYS_PROCESS_SPAWN_STACK_SIZE_1M);
}

int a4_spawn_check(const char* path, char* detail, size_t detail_size) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    snprintf(detail, detail_size, "missing: %s", path);
    return 0;
  }
  unsigned char magic[4] = { 0, 0, 0, 0 };
  size_t got = fread(magic, 1, sizeof magic, f);
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fclose(f);
  if (got != sizeof magic || size <= 0) {
    snprintf(detail, detail_size, "unreadable/empty: %s", path);
    return 0;
  }
  /* SELF header magic is "SCE\0" (0x53434500) — verified against the
   * actual make_self / make_self_npdrm outputs, NOT 7F 'S' 'C' 'E'. */
  if (!(magic[0] == 0x53 && magic[1] == 0x43 && magic[2] == 0x45 && magic[3] == 0x00)) {
    snprintf(detail, detail_size, "bad magic %02X %02X %02X %02X size=%ld: %s",
             magic[0], magic[1], magic[2], magic[3], size, path);
    return 0;
  }
  snprintf(detail, detail_size, "ok SELF size=%ld: %s", size, path);
  return 1;
}

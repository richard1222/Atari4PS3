#include "common/fsutil.h"
#include "common/paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool a4_mkdir_p(const char* path) {
  char tmp[512];
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof tmp) return false;
  memcpy(tmp, path, len + 1);
  for (size_t i = 1; i <= len; i++) {
    if (tmp[i] == '/' || tmp[i] == '\0') {
      char save = tmp[i];
      tmp[i] = '\0';
      mkdir(tmp, 0777); /* EEXIST and friends are fine; stat() decides below */
      tmp[i] = save;
    }
  }
  struct stat st;
  return stat(path, &st) == 0;
}

bool a4_ensure_dirs(void) {
  bool ok = true;
  ok &= a4_mkdir_p(A4_ROMS_2600);
  ok &= a4_mkdir_p(A4_ROMS_5200);
  ok &= a4_mkdir_p(A4_ROMS_7800);
  ok &= a4_mkdir_p(A4_ROMS_LYNX);
  ok &= a4_mkdir_p(A4_ROMS_8BIT);
  ok &= a4_mkdir_p(A4_ROMS_ST);
  ok &= a4_mkdir_p(A4_ROMS_JAGUAR);
  ok &= a4_mkdir_p(A4_ROMS_PC);
  ok &= a4_mkdir_p(A4_BIOS_DIR);
  ok &= a4_mkdir_p(A4_SAVES_2600);
  ok &= a4_mkdir_p(A4_SAVES_5200);
  ok &= a4_mkdir_p(A4_SAVES_7800);
  ok &= a4_mkdir_p(A4_SAVES_LYNX);
  ok &= a4_mkdir_p(A4_SAVES_8BIT);
  ok &= a4_mkdir_p(A4_SAVES_ST);
  ok &= a4_mkdir_p(A4_SAVES_JAGUAR);
  ok &= a4_mkdir_p(A4_SAVES_PC);
  ok &= a4_mkdir_p(A4_STATES_2600);
  ok &= a4_mkdir_p(A4_STATES_5200);
  ok &= a4_mkdir_p(A4_STATES_7800);
  ok &= a4_mkdir_p(A4_STATES_LYNX);
  ok &= a4_mkdir_p(A4_STATES_8BIT);
  ok &= a4_mkdir_p(A4_STATES_ST);
  ok &= a4_mkdir_p(A4_STATES_JAGUAR);
  ok &= a4_mkdir_p(A4_STATES_PC);
  ok &= a4_mkdir_p(A4_CONFIG_DIR);
  ok &= a4_mkdir_p(A4_LOGS_DIR);
  return ok;
}

bool a4_is_file(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool a4_read_file(const char* path, unsigned char** out_data, size_t* out_size) {
  *out_data = NULL;
  *out_size = 0;
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0) { fclose(f); return false; }
  unsigned char* buf = (unsigned char*)malloc((size_t)n);
  if (!buf) { fclose(f); return false; }
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  if (got != (size_t)n) { free(buf); return false; }
  *out_data = buf;
  *out_size = (size_t)n;
  return true;
}

bool a4_write_file(const char* path, const void* data, size_t size) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  size_t put = fwrite(data, 1, size, f);
  fclose(f);
  return put == size;
}

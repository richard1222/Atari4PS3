#include "common/log.h"
#include "common/paths.h"
#include "common/fsutil.h"

#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>

static unsigned a4_log_seq;

void a4_log_init(void) {
  a4_mkdir_p(A4_LOGS_DIR);
  /* Keep the log bounded: start fresh once it passes 256 KiB. */
  struct stat st;
  if (stat(A4_LOG_FILE, &st) == 0 && st.st_size > 262144)
    remove(A4_LOG_FILE);
}

void a4_log(const char* tag, const char* fmt, ...) {
  FILE* f = fopen(A4_LOG_FILE, "a");
  if (!f) return;
  fprintf(f, "[%05u][%s] ", a4_log_seq++, tag);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

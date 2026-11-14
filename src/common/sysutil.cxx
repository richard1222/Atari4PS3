#include "common/sysutil.h"
#include "common/log.h"

#include <ppu-types.h>
#include <sysutil/sysutil.h>

static volatile int g_exit_requested = 0;

/* Runs on the calling thread via sysUtilCheckCallback, so logging here
 * is safe (a4_log opens/appends/closes per line). */
static void a4_sysutil_cb(u64 status, u64 param, void* usrdata) {
  (void)param;
  (void)usrdata;
  a4_log("sysutil", "callback status=0x%04llx",
         (unsigned long long)status);
  if (status == SYSUTIL_EXIT_GAME) g_exit_requested = 1;
}

int a4_sysutil_init(void) {
  int rc = sysUtilRegisterCallback(0, a4_sysutil_cb, NULL);
  a4_log("sysutil", "register rc=%d", rc);
  return rc;
}

int a4_sysutil_poll(void) {
  sysUtilCheckCallback();
  return g_exit_requested;
}

int a4_sysutil_exit_requested(void) {
  return g_exit_requested;
}

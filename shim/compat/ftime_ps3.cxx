/* Legacy ftime() for the DOSBox Pure runner ONLY (M2).
 *
 * newlib on this SDK ships <sys/timeb.h> (so DOSBox Pure's
 * src/ints/bios.cpp compiles) but provides no ftime() implementation.
 * bios.cpp:532 calls ftime() to seed the emulated PC BIOS clock
 * (INT 1A date/time services). Implemented here on top of
 * gettimeofday(), which the SDK libc does provide. Linked only
 * into the dosbox-pure runner, never into other runners. */
#include <sys/time.h>
#include <sys/timeb.h>

extern "C" int ftime(struct timeb *tp) {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0)
    return -1;
  tp->time = tv.tv_sec;
  tp->millitm = static_cast<unsigned short>(tv.tv_usec / 1000);
  tp->timezone = 0;
  tp->dstflag = 0;
  return 0;
}

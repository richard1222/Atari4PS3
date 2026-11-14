/* Real PPU FPSCR rounding adapter for the pinned Hatari core.
 * Unsupported exception APIs are undeclared, never no-op successes. */
#include "fenv.h"
#include <math.h>
#include <float.h>
static_assert(sizeof(long double)==sizeof(double) && LDBL_MANT_DIG==DBL_MANT_DIG,"native-precision log2l requires equal double/long-double formats");
extern "C" {
long double log2l(long double value) { return static_cast<long double>(log2(static_cast<double>(value))); }
int fegetround(void) {
  double fpscr; uint64_t bits;
  __asm__ volatile("mffs %0" : "=f"(fpscr));
  memcpy(&bits,&fpscr,sizeof bits);
  return (int)(bits & UINT64_C(3));
}
int fesetround(int direction) {
  double fpscr; uint64_t bits;
  if(direction<0 || direction>3) return -1;
  __asm__ volatile("mffs %0" : "=f"(fpscr));
  memcpy(&bits,&fpscr,sizeof bits);
  bits=(bits & ~UINT64_C(3)) | (unsigned)direction;
  memcpy(&fpscr,&bits,sizeof fpscr);
  /* Field mask 1 writes only FPSCR field 7; retained bits include NI/XE. */
  __asm__ volatile("mtfsf 1,%0" : : "f"(fpscr) : "memory");
  return 0;
}
}

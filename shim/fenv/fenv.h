/* Narrow Hatari floating-point environment adapter for the PS3 PPU.
 * Implements the only live fenv operation used by the pinned core, plus
 * round-mode readback. Unsupported fenv APIs are deliberately undeclared.
 * Rounding uses FPSCR RN bits; other FPSCR control bits are preserved.
 * This header is included only in the Hatari cross-build. */
#ifndef A4_HATARI_PPU_FENV_H
#define A4_HATARI_PPU_FENV_H
#if !defined(__powerpc__) && !defined(__powerpc64__)
#error This floating-point adapter requires a PowerPC PPU target
#endif
#include <stdint.h>
#include <string.h>
#define FE_TONEAREST 0
#define FE_TOWARDZERO 1
#define FE_UPWARD 2
#define FE_DOWNWARD 3
#ifdef __cplusplus
extern "C" {
#endif
int fegetround(void);
int fesetround(int direction);
/* This SDK omits log2l; its long double has the same 53-bit precision
 * and eight-byte representation as double. The implementation checks that
 * contract at compile time and delegates to the SDK's real log2. */
long double log2l(long double value);
#ifdef __cplusplus
}
#endif
#endif

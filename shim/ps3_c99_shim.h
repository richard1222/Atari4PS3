// Forced-include shim (-include) for the Stella core build on PSL1GHT.
//
// The ps3toolchain libstdc++ was configured without C99 library support,
// so the C++ headers do not declare the C99 functions in namespace std
// even though newlib provides them all in the global namespace. Import
// exactly the ones the codebase uses (each absence below was proven by a
// compiler error; the C89 siblings such as std::strtol, std::sprintf and
// std::abs ARE declared normally and are deliberately NOT redeclared).
//
// NOTE: newlib hides the C99 declarations behind
// `#if !defined(__STRICT_ANSI__) || ...`, so the build must also pass
// -U__STRICT_ANSI__ (see shim/README.md); without it the global names are
// undeclared and the using-declarations below fail.
//
// - <cstdio>:  std::snprintf, std::vsnprintf (Base.cxx).
// - <cstdlib>: std::strtof, std::strtold, std::strtoll, std::strtoull
//   (nlohmann/json), plus the unused-but-harmless C99 siblings atoll
//   and llabs. std::to_string's integer overloads exist and link; only
//   the floating-point overloads are missing, and Stella never calls
//   those (all 8 call sites pass integers), so no to_string shim.
// - <cmath>:   std::round (Bezel.cxx), std::lround / std::llround
//   (PointingDevice.cxx). The C library has round/roundf/roundl (and
//   lround/lroundf/lroundl, llround/llroundf/llroundl) as separate
//   names, so the float/long-double overloads are defined explicitly,
//   plus the standard integer overloads.
#ifndef A4_PS3_C99_SHIM_H
#define A4_PS3_C99_SHIM_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace std {
using ::snprintf;
using ::vsnprintf;

using ::strtof;
using ::strtold;
using ::strtoll;
using ::strtoull;
using ::atoll;
using ::llabs;

using ::round;
inline float round(float x) { return ::roundf(x); }
inline long double round(long double x) { return ::roundl(x); }
template <typename _Integer>
typename enable_if<is_integral<_Integer>::value, double>::type round(
    _Integer x) {
  return ::round(static_cast<double>(x));
}

using ::lround;
inline long lround(float x) { return ::lroundf(x); }
inline long lround(long double x) { return ::lroundl(x); }
template <typename _Integer>
typename enable_if<is_integral<_Integer>::value, long>::type lround(
    _Integer x) {
  return ::lround(static_cast<double>(x));
}

using ::llround;
inline long long llround(float x) { return ::llroundf(x); }
inline long long llround(long double x) { return ::llroundl(x); }
template <typename _Integer>
typename enable_if<is_integral<_Integer>::value, long long>::type llround(
    _Integer x) {
  return ::llround(static_cast<double>(x));
}
}  // namespace std

#endif  // A4_PS3_C99_SHIM_H

# PS3 compatibility shims

These adapters stay outside the pinned emulator trees. See [build notes](../docs/BUILD.md) for their scope.

The C++ and pthread shims use LV2 synchronization/thread APIs. The pthread trampoline releases its allocation and exits with the actual start-routine result. The audio frontend uses standard lock-free atomics independently of these thread shims.

Hatari uses real FPSCR rounding changes; unsupported fenv exception APIs are undeclared. This SDK uses the same format for double and long double, checked at compile time for the `log2l` compatibility function. The other narrow SDK adapters provide legacy memory declarations and a real millisecond wall clock.

No shim modifies the SDK installation or upstream emulator source. Hardware behavior remains part of the PS3 test checklist.

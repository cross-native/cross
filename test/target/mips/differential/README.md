# MIPS differential execution tests

Each kernel set is one `@ABI@` template compiled twice: once for the x86-64
host backend (the reference) and once for a MIPS target/CPU profile, then run
under QEMU R4000 and compared line by line against the host reference. This
complements `test/target/mips/runtime.cmake`, which checks fixed expected
values baked into hand-written assembly; here the "expected" values come from
the compiler's own x86-64 backend, so the comparison catches divergences
between backends rather than between the compiler and a hand-written oracle.

## Files

- `int_kernels.x.in`, `float_kernels.x.in`, `private_kernels.x.in` -- kernel
  sources. Every kernel is `global u32 k_name(in u32 a, in u32 b, in u32 c)`
  tagged `[[abi("@ABI@"), noinline]]`; `@ABI@` is substituted by
  `differential.cmake`, `ms_abi`/`sysv_abi` for the host and `o32` for MIPS.
  `private_kernels.x.in` merges the former private-ABI and projected-o32-pair
  kernel sets and is integer-only, so it runs on both CPU profiles;
  `float_kernels.x.in` keeps `k_p_float`, the one kernel needing
  float-to-integer conversion, which MIPS I (`r3000-o32`) diagnoses.
- `start_int.s`, `start_float.s`, `start_private.s` -- checked-in generated
  MIPS bare-metal startup, one per kernel set. Each calls every kernel with
  six argument triples and prints each `u32` result as 8 hex digits over the
  Malta UART, then exits through the UHI semihosting trap.
- `main_int.cpp`, `main_float.cpp`, `main_private.cpp` -- checked-in
  generated host reference, printing the same values in the same order from
  the native x86-64 backend.
- `generate.py` -- regenerates a kernel set's `start_*.s`/`main_*.cpp` pair
  from its template. The generated files are checked in so ctest needs no
  Python; rerun this by hand (`python generate.py <set>_kernels.x.in
  start_<set>.s main_<set>.cpp`) only when a kernel set gains, loses, or
  renames a kernel.
- `differential.cmake` -- the `-P` driver, described below.

## differential.cmake

Given `CC`, `HOST_CXX`, `TEMPLATE`, `STARTUP`, `HOST_MAIN`, `LINKER`,
`WRAPPER_TEMPLATE`, `WRAPPER_LINKER`, `TARGET_FLAGS`, and `OUTPUT` (plus
optional `OPT_LEVEL`, default `-O2`; `EXTRA_FLAGS`; and `ENDIAN`, `BE` or
`LE`, default `BE`), it substitutes `@ABI@` for both sides, compiles and runs
the host reference, compiles the MIPS side and runs it under
`qemu-system-mips64`/`qemu-system-mips64el` through the same `bare.ld` +
`runtime64.s.in`/`bare64.ld` wrapping as `runtime.cmake`, then splits both
stdout captures into lines and compares them positionally. Every mismatching
kernel/argument-set pair is reported with its expected and actual value via
`message(FATAL_ERROR)`; otherwise the test passes. Only CMake string/list
operations run at test time -- no Python. Like `runtime.cmake`, the test
skips (rather than fails) when `llvm-mc`, `llvm-objcopy`, `ld.lld`, or the
matching `qemu-system-mips64*` is not on `PATH`.

Kernels stay free of undefined behavior by construction: no signed overflow,
bounded loops, in-range float-to-int conversions, endian-neutral byte access.
Keep new kernels to the same rules.

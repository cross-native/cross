# Targets

`cc --print-targets` lists the architectures compiled into a `cc` build and
`cc --print-profiles` the shipped profiles. Select a target with
`-target TRIPLE`, or a whole configuration with `-mprofile=NAME`; see
[invoke.md](invoke.md). `--print-abis`, `--print-options=target`,
`--print-features`, `--print-instructions`, and `--print-registers` describe
the selected target. The model files that define ABIs, manglings, option
presets, and profiles are described in [models.md](models.md).

## Object formats

The triple selects the object format: `windows` or `mingw` triples produce
COFF, `darwin` or `apple` triples Mach-O, and others ELF. Symbol attributes
map to each format as follows:

| Attribute | ELF | COFF | Mach-O |
| --- | --- | --- | --- |
| `[[weak]]` definition | Yes | Weak external | Weak definition |
| `[[visibility("hidden")]]` | Yes | Error | Private external |
| `[[visibility("protected")]]`, `("internal")` | Yes | Error | Error |
| `[[alias("name")]]` | Yes | Yes | Yes, but not with `[[weak]]` |
| `[[weakref("name")]]` | Yes | Yes | Yes |
| `[[retain]]` | `SHF_GNU_RETAIN` | Linker include directive | `no_dead_strip` |

An alias must name a compatible definition in the same compilation.
`[[hot]]` and `[[cold]]` functions go to separate text sections. `[[used]]`
forces emission. `[[no_stack_protector]]` and `[[no_sanitize("name")]]` are
accepted; Cross generates neither kind of instrumentation.

Mach-O output does not prefix symbol names with an underscore, so a C symbol
needs its full name, as in `[[link_name("_write")]]`.

`-funwind-tables` (or `-fasynchronous-unwind-tables`) emits CFI or SEH unwind
records; no unwinder is linked. By default functions assume nothing unwinds
through them; `-funwind-model=platform` keeps incoming frame state intact for a
platform unwinder even without tables.

Only address space 0 exists on the current targets, so
`[[address_space(N)]]` with any other `N` is an error.

## x86-64

| Triple | Format | Default ABI |
| --- | --- | --- |
| `x86_64-w64-windows-gnu` | COFF | `cross` |
| `x86_64-unknown-linux-gnu` | ELF | `cross` |
| `x86_64-apple-darwin` | Mach-O | `cross` |

`amd64-*` is accepted for `x86_64-*`. The `x86_64-windows` and `x86_64-elf`
profiles select the first two triples. ABIs:

| ABI | Aliases | Use |
| --- | --- | --- |
| `cross` | `cross_abi` | Default for global, unresolved, and indirect calls. |
| `sysv_abi` | `linux` | The System V AMD64 C ABI. |
| `ms_abi` | `ms`, `win64`, `windows` | The Microsoft x64 C ABI. |

The Cross ABI is not compatible with either C ABI; it preserves only the stack
and frame registers across calls. Use `sysv_abi` or `ms_abi` at C boundaries.

Target options:

| Option | Values |
| --- | --- |
| `-march=`, `-mtune=` | `generic`, `x86-64`, `x86-64-v2`, `x86-64-v3`, `x86-64-v4`, `haswell`, `skylake`, `skylake-avx512`, `znver1`, `znver2`, `znver3` |
| `-mFEATURE`, `-mno-FEATURE` | Instruction set extensions such as `sse4.2`, `popcnt`, `avx`, `avx2`, `fma`, `bmi2`, `avx512f`, `avx512vl` (see `--print-options=target`) |
| `-mcmodel=` | `small` (default), `kernel`, `medium`, `large` |
| `-mred-zone`, `-mno-red-zone` | Let leaf functions use the area below the stack pointer (not with COFF). |
| `-mprefer-vector-width=` | `none`, `128`, `256`, `512`: widest vector the compiler chooses on its own. |
| `-mrisc-cisc-balance=` | `0` to `100`, default `50`: weighs optimization cost estimates from load/store RISC (`0`) to CISC (`100`) behavior; it changes code choices, not the instruction set. The x86-64 profiles set `100`; MIPS also accepts the option, and its profiles set `0`. |

Disabling a feature also disables the features that depend on it, so
`-mno-avx` disables AVX2 and AVX-512 too. 128-bit integers, `f80`, vectors,
atomics, thread-local storage, and variadic functions are supported, but a
variadic function or function pointer cannot use register or stack locations.

`[[naked]]` functions can inline `[[raw_inline]]` functions whose locals,
control flow, pointer accesses, and integer, `f32`, and `f64` operations fit
in the naked function's bound registers and declared clobbers. Anything that
would need a call, a spill, or a stack object is an error.

## MIPS

| Triple | Byte order | Address size | Default ABI |
| --- | --- | --- | --- |
| `mips-unknown-elf` | Big | 32-bit | `cross32` |
| `mipsel-unknown-elf` | Little | 32-bit | `cross32` |
| `mipsallegrexel-sony-psp-elf` | Little | 32-bit | `eabi32` |
| `mips64-unknown-elf` | Big | 64-bit | `cross-n64` |
| `mips64el-unknown-elf` | Little | 64-bit | `cross-n64` |

| Profile | Triple | CPU | ABI |
| --- | --- | --- | --- |
| `mips-elf`, `mipsel-elf` | `mips-unknown-elf`, `mipsel-unknown-elf` | generic | `cross32` |
| `r3000-o32` | `mips-unknown-elf` | MIPS I (R3000) | `o32` |
| `r6000-eabi` | `mips-unknown-elf` | MIPS II (R6000) | `eabi32` |
| `vr4300-o32` | `mips-unknown-elf` | MIPS III (VR4300) | `o32` |
| `vr4300-cross64` | `mips-unknown-elf` | MIPS III (VR4300) | `cross64` |
| `psp-allegrex` | `mipsallegrexel-sony-psp-elf` | Allegrex | `eabi32` |
| `allegrex-o32` | `mipsallegrexel-sony-psp-elf` | Allegrex | `o32` |
| `mips64-n64`, `mips64el-n64` | `mips64-unknown-elf`, `mips64el-unknown-elf` | MIPS64 | `cross-n64` |

For example, these two commands select the same CPU and ABI:

```sh
cc -c -O2 -mprofile=vr4300-o32 input.x -o input.o
cc -c -O2 -target mips-unknown-elf -march=vr4300 -mabi=o32 input.x -o input.o
```

ABIs:

| ABI | Aliases | Notes |
| --- | --- | --- |
| `cross32` | `cross`, `cross_abi`, `cross-32` | Cross ABI with 32-bit registers. |
| `cross64` | `cross-64` | Cross ABI with 64-bit registers and 32-bit addresses; needs MIPS III or later. |
| `cross-n64` | `cross`, `cross_abi` | Cross ABI with 64-bit addresses; needs a `mips64` triple. |
| `o32` | `32`, `abi32` | The standard 32-bit C ABI. |
| `eabi32` | `eabi`, `eabi-32` | The embedded ABI used by the PSP toolchain. |
| `n64` | `64`, `abi64` | The standard 64-bit C ABI; needs a `mips64` triple. |

`cross` and `cross_abi` name `cross-n64` on `mips64` and `mips64el` triples
and `cross32` on the others. The Cross ABIs are not compatible with the C
ABIs. Across calls, `cross32` preserves only `sp` and `fp`, while `cross64`
and `cross-n64` also preserve `s0`–`s7`. Select `o32`, `n64`, or `eabi32` at
C boundaries, per declaration with `[[abi("n64")]]` or for the whole
compilation with `-mabi=n64`.

`-march=` accepts `generic`, `mips1`, `r2000`, `r3000`, `mips2`, `r6000`,
`allegrex`, `mips3`, `r4000`, `r4400`, `r4600`, `vr4300`, `mips4`, `mips5`,
`mips32`, `mips32r2`, and `mips64`; `-mtune=` is independent. A CPU enables
the features it implies, and `-m`/`-mno-` options adjust them:

| Option | Effect |
| --- | --- |
| `-mhard-float`, `-msoft-float`, `-msingle-float` | FPU availability. |
| `-mfp32`, `-mfpxx`, `-mfp64`, `-modd-spreg` | FPU register model. |
| `-mllsc` | 32-bit LL/SC atomics. |
| `-mfix4300` | Separate consecutive floating-point multiplies affected by the VR4300 erratum. |
| `-mbranch-likely`, `-mcond-move`, `-mrotate` | Allow branch-likely, conditional-move, and rotate instructions. |
| `-mcmodel=large` | Materialize full 64-bit addresses and call through registers. |

`cross-n64` and `n64` code uses 64-bit addresses and produces ELF64 objects
that assume a 64-bit FPU; the other ABIs produce ELF32, even on a 64-bit CPU.
With 64-bit addresses, symbol addresses use two instructions by default, so
symbols must lie in the low 2 GB or in the sign-extended KSEG0/KSEG1 range
unless `-mcmodel=large` is given, and assembly output names general registers
by number (`$8`) because 64-bit assemblers read `$t0`–`$t3` differently.

Allegrex has a single-precision FPU: `f64` arithmetic is an error. Code that
needs floating point under `-msoft-float` is an error, because Cross does not
call a software floating-point library.

Supported: integer and pointer operations, 64-bit integers on 32-bit CPUs,
hard-float `f32` and `f64`, structures and unions by value under every ABI,
function pointers, variable-length arrays, computed goto, `$::patch` values,
and LL/SC atomics. Not supported: vectors, integers wider than 64 bits,
variadic function definitions, manual register locations, machine-instruction
built-ins, position-independent code (`-mabicalls`), thread-local storage,
MIPS16, microMIPS, and the Allegrex VFPU. They are diagnosed when used.

## Limits on all targets

- `case` and `default` labels cannot be nested inside another statement of
  the `switch` body (as in Duff's device), and a `switch` body cannot declare a
  variable-length array.
- Function pointers can be converted to another ABI or other register
  locations only when the function is named directly; see
  [language.md](language.md#function-pointers).
- `[[aligned(N)]]` applies to objects, records, members, and function
  definitions; it is not supported on a typedef of a non-record type.

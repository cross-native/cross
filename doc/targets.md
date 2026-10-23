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

A `[[section]]` section takes the type that its name has in the object
format: on ELF, `.init_array`, `.fini_array`, and `.preinit_array`, also
followed by `.` and a suffix, are SHT_INIT_ARRAY, SHT_FINI_ARRAY, and
SHT_PREINIT_ARRAY sections, as GNU `as` makes them.

The shipped profiles name these initializer tables for `[[constructor]]` and
`[[destructor]]` ([models.md](models.md#initializer-table-entries)), where
`NNNNN` is a priority `P` as five digits:

| Profile | Constructors | Destructors | ABI |
| --- | --- | --- | --- |
| `x86_64-elf` (ELF) | `.init_array`, `.init_array.NNNNN` | `.fini_array`, `.fini_array.NNNNN` | `sysv_abi` |
| `x86_64-windows` (COFF) | `.CRT$XCU`, `.CRT$XCTNNNNN` | `.dtors`, `.dtors.NNNNN` of 65535 - `P` | `ms_abi` |
| `x86_64-darwin` (Mach-O) | `__DATA,__mod_init_func` | `__DATA,__mod_term_func` | `sysv_abi` |
| MIPS profiles (ELF) | `.init_array`, `.init_array.NNNNN` | `.fini_array`, `.fini_array.NNNNN` | the compilation's |
| AArch64 profiles (ELF) | `.init_array`, `.init_array.NNNNN` | `.fini_array`, `.fini_array.NNNNN` | `aapcs64` |

glibc and other ELF C runtimes call `.init_array` before `main` and
`.fini_array` at exit. MinGW-w64's startup code calls the `.CRT$XC*`
entries before `main`, and the exit handler that its `__main` registers
calls `.dtors`; dyld calls the Mach-O sections. GNU `ld`'s default scripts
order the prioritized sections of each table by priority. On bare metal,
the program's startup code walks the ELF arrays, for example between symbols
that its linker script defines, and calls each entry with the compilation's
ABI. Mach-O tables have no priorities.

An alias must name a compatible definition in the same compilation. An
instance of a `global` generic goes to a COMDAT group keyed by its link name
on ELF and COFF, and is a weak definition on Mach-O, so the linker keeps one
copy. `[[hot]]` and `[[cold]]` functions go to separate text sections.
`[[used]]` forces emission. `[[no_stack_protector]]` is accepted; Cross
generates no stack-protector instrumentation. `[[no_sanitize("bounds")]]`
removes the `-fbounds-trap` checks from a function; other names are accepted
and have no effect.

Output follows definition order. Each source unit, in command-line order,
contributes its functions, `[[naked]]` ones included, in definition order
with its generic instances after them and each copy that `-fipa-cp-clone`
specializes right after the function it copies, and then its objects. A
function's jump tables and the constants it loads from memory follow it in
read-only data: in `.rodata` (COFF `.rdata`, Mach-O `__TEXT,__const`), where
the functions of one source unit share one copy of each constant, or, when the
function has a section of its own named after its link name `NAME`
(`-ffunction-sections`, `[[hot]]`, `[[cold]]`, `[[retain]]`, or a `global`
generic instance), in `.rodata.NAME` (COFF `.rdata$NAME`), which joins an
instance's COMDAT group and holds the function's own copy of each constant.

Mach-O output spells every link name with the leading underscore of C symbols
on that format, so `[[link_name("write")]]` refers to the C function `write`
(object-file symbol `_write`) and a definition of `app::run` defines
`_app::run`. ELF and COFF use link names unchanged. Mach-O cannot hold a
`global label` in a function with unwind tables.

Assembly output quotes a link name that contains characters other than
letters, digits, `_`, `.`, and `$`, such as the `::` in `"app::run"`. GNU `as`
accepts quoted symbol names from version 2.26. Linker scripts and debuggers
need the same quoting, for example `ENTRY("app::run")` in a GNU `ld` script.

`-funwind-tables` (or `-fasynchronous-unwind-tables`) emits CFI or SEH unwind
records; no unwinder is linked. By default functions assume nothing unwinds
through them; `-funwind-model=platform` keeps incoming frame state intact for a
platform unwinder even without tables.

`-g` adds DWARF 5 debugging information ([invoke.md](invoke.md#debugging-information))
in all three formats. ELF and COFF objects describe frames in `.debug_frame`,
beside the SEH records of COFF unwind tables. Mach-O objects get no
call-frame information from `-g`; `-funwind-tables` still emits it.

Only address space 0 exists on the current targets, so
`[[address_space(N)]]` with any other `N` is an error.

A `label` is a code address with the size and alignment of a pointer.
`(uptr)` converts it to that address; in a static initializer the conversion is
a relocation against the label, like `(uptr)&function`.

A pointer converts only to an integer at least as wide as `uptr`. An integer
narrower than an address converts to an object pointer by its signedness, so
`(u8 *)(i8)-1` is the last address. A constant wider than an address must be
an address in range, `0` through the largest address; otherwise the conversion
is an error. At run time a wider integer keeps its low address bits.

## x86-64

| Triple | Format | Default ABI |
| --- | --- | --- |
| `x86_64-w64-windows-gnu` | COFF | `x` |
| `x86_64-unknown-linux-gnu` | ELF | `x` |
| `x86_64-apple-darwin` | Mach-O | `x` |

`amd64-*` is accepted for `x86_64-*`. The `x86_64-windows`, `x86_64-elf`,
and `x86_64-darwin` profiles select the three triples. ABIs:

| ABI | Aliases | Use |
| --- | --- | --- |
| `x` | | The Cross ABI: default for global, unresolved, and indirect calls. |
| `sysv_abi` | `linux` | The System V AMD64 C ABI. |
| `ms_abi` | `ms`, `win64`, `windows` | The Microsoft x64 C ABI. |

The Cross ABI is not compatible with either C ABI. Across calls it preserves
`rbx`, `r12`–`r15`, and the stack and frame registers; every other general
register and all SIMD, mask, and x87 state may change. Use `sysv_abi` or
`ms_abi` at C boundaries.

`ms_abi` preserves only the low 128 bits of `xmm6`-`xmm15`. The upper
YMM/ZMM bits and all vector registers 16-31 and mask registers are volatile.
User ABIs can specify other preserved slices with `call_preserved_views`
([models.md](models.md#entry-properties)). Values wider than a preserved
view are spilled around calls or allocated elsewhere. Definitions preserve
the views they use in frame save slots; stronger callers also save the
preservation difference when calling another ABI. SIMD save forms follow
the encodings used and the contracts of called functions. Legacy SSE
writes leave upper lanes intact; VEX and EVEX writes also clear upper
lanes. An operation or call that destroys promised bits the selected
features cannot save is diagnosed, for explicit views and whole-register
promises alike. Sibling calls require the callee to preserve every bit
the caller promises; an incompatible `musttail` call is diagnosed.
Win64 unwind records describe XMM saves; saves of registers 16-31 and
frame-register offsets above 240 bytes with Win64 unwind tables are
diagnosed.

The C ABIs name the C types ([models.md](models.md#entry-properties)), which
the format checkers read; `char`, `short`, `int`, `long long`, `intmax_t`,
`float`, and `double` are `i8`, `i16`, `i32`, `i64`, `i64`, `f32`, and `f64`
in both:

| C type | `sysv_abi` | `ms_abi` |
| --- | --- | --- |
| `long` | `i64` | `i32` |
| `size_t`, `ptrdiff_t` | `u64`, `i64` | `u64`, `i64` |
| `wchar_t`, `wint_t` | `i32`, `u32` | `u16`, `u16` |
| `long double` | `f80` | `f64` |

Target options:

| Option | Values |
| --- | --- |
| `-march=`, `-mtune=` | A `cpu` model entry ([models.md](models.md#cpu-entries)), listed by `cc --print-cpus`. |
| `-mFEATURE`, `-mno-FEATURE` | Instruction set extensions such as `sse4.2`, `popcnt`, `avx`, `avx2`, `fma`, `bmi2`, `avx512f`, `avx512vl` (see `--print-options=target`) |
| `-mcmodel=` | `small` (default), `kernel`, `medium`, `large` |
| `-mred-zone`, `-mno-red-zone` | Let leaf functions use the area below the stack pointer (not with COFF). |
| `-mprefer-vector-width=` | `none`, `128`, `256`, `512`: widest vector the compiler chooses on its own. |
| `-mrisc-cisc-balance=` | `0` to `100`, default `50`: weighs optimization cost estimates from load/store RISC (`0`) to CISC (`100`) behavior; it changes code choices, not the instruction set. The x86-64 profiles set `100`; MIPS also accepts the option, and its profiles set `0`. |

`model/x86_64.xm` ships the `x86-64` levels and an entry for each CPU name
that GCC and Clang accept for 64-bit code, such as `k8`, `core2`,
`nehalem`, `haswell`, `alderlake`, `sapphirerapids`, `knl`, `znver5`,
`c86-4g-m7`, and `nano-x4`, with their other names, such as `corei7` for
`nehalem` and `skx` for `skylake-avx512`; the 32-bit CPUs, such as `i686`
and `pentium4`, are not available, and neither is `native`. The entries set
features only, so their instructions have the target's default costs
(`cc --print-costs`). A CPU that adds features to the CPU it inherits takes
them from an entry named `ext-` and the CPU's name; `intel`, a tuning name,
selects the `x86-64` baseline. When `-mtune` names `skylake`,
`skylake-avx512`, `znver3`, or a CPU that inherits one of them, such as
`cascadelake`, `sapphirerapids`, or `znver4`, functions compiled for speed
align to 32 bytes, and otherwise to 16 bytes; size goals align them less,
and `-falign-functions` gives the alignment itself. Extensions that
`--print-options=target` does not list, such as TSX, SGX, CET, AMX, APX,
AVX-VNNI, SSE4a, XOP, FMA4, and the AVX-512 IFMA, BF16, FP16, and
VP2INTERSECT subsets, are not modeled, so no CPU enables them.

Disabling a feature also disables the features that depend on it, so
`-mno-avx` disables AVX2 and AVX-512 too. 128-bit integers, `f80`, vectors,
atomics, thread-local storage, and variadic functions are supported, but a
variadic function or function pointer cannot use register or stack locations.
The `va_list` state of `sysv_abi` is the address of a `__va_list_tag` that
the definition fills in from `gp_offset`, `fp_offset`, `overflow_arg_area`,
and `reg_save_area`, and that of `ms_abi` the address of the next argument
slot, so a `variadic(void *arguments "va_list")` binding can be passed to a
C function that takes a `va_list`. The Cross ABI `x` has no `va_list`
state.

`$::trap()`, like a failed `-fbounds-trap` check, executes `ud2`.

The x86-64 profiles declare the floating environment in which these systems
start a program: MXCSR and the x87 control word mask every exception, and
denormals are kept (FTZ and DAZ clear). Code that unmasks exceptions or sets
FTZ needs a profile that declares it ([models.md](models.md#profiles)). The
relational operators compile to `comiss`, `comisd`, and `fcomip`, which raise
the invalid-operation exception for every NaN; `==`, `!=`, and the truth value
of a floating operand use `ucomiss`, `ucomisd`, and `fucomip`, which raise it
only for a signaling NaN. An invalid operation produces a negative quiet NaN,
as SSE and the x87 unit do: 0xffc00000 for `f32`, 0xfff8000000000000 for
`f64`, and likewise for `f80` and `f128`. An operation or conversion with a
NaN operand produces that NaN, quieted, with its sign and the leading bits of
its payload. With two NaN operands, `f80` operations take a quiet NaN before
a signaling one, then the larger significand, then the positive one; `f128`
operations and `f32` and `f64` subtraction and division take the left one,
and `f32` and `f64` `+` and `*` either one, because the compiler may swap
their operands. Compile-time evaluation produces the same NaNs, taking the
left operand of `+` and `*`. FTZ does not apply to `f80` and `f128` results,
and `f128` operations raise no exceptions.

The floating intrinsics use SSE for `f32` and `f64` and the x87 unit for
`f80`; `$::fmin` and `$::fmax` are `minss`/`maxss` (`minsd`/`maxsd`) with an
ordered compare that substitutes the second operand for a NaN first one.
Because those instructions raise the invalid-operation exception for a quiet
NaN, under a profile that traps that exception the two intrinsics compare with
`ucomiss` or `ucomisd` and branch instead. On `f128`, `$::fabs` and `$::copysign` change
the sign bit, and `$::sqrt`, `$::fmin`, and `$::fmax` are errors. The `f80`
forms round as the x87 precision control selects, which must be the 64-bit
significand.

A `[[musttail]]` call cannot target a variadic function or leave a function
that binds variadic state, and passes every argument in a register. With
automatic locations, each argument must be an integer or a pointer and the
result must fit in one register. A function with register or stack locations
can tail-call only another such function or function pointer, and neither may
have `out` or `inout` parameters.

`[[naked]]` functions can inline `[[raw_inline]]` functions whose locals,
control flow, pointer accesses, and integer, `f32`, and `f64` operations fit
in the naked function's bound registers and declared clobbers. Both can read,
write, and take the address of a static-duration object: one at a fixed
address through its sign-extended 32-bit absolute address, any other relative
to `%rip`. A thread-local object, a fixed address outside that range, an
object that needs a GOT entry under `-fpic`, and `%rip`-relative access under
the medium and large code models are errors. Anything that would need a call,
a spill, or a stack object is an error.

## MIPS

| Triple | Byte order | Address size | Default ABI |
| --- | --- | --- | --- |
| `mips-unknown-elf` | Big | 32-bit | `x32` |
| `mipsel-unknown-elf` | Little | 32-bit | `x32` |
| `mipsallegrexel-sony-psp-elf` | Little | 32-bit | `eabi32` |
| `mips64-unknown-elf` | Big | 64-bit | `x64` |
| `mips64el-unknown-elf` | Little | 64-bit | `x64` |

| Profile | Triple | CPU | ABI |
| --- | --- | --- | --- |
| `mips-elf`, `mipsel-elf` | `mips-unknown-elf`, `mipsel-unknown-elf` | generic | `x32` |
| `r3000-o32` | `mips-unknown-elf` | MIPS I (R3000) | `o32` |
| `r6000-eabi` | `mips-unknown-elf` | MIPS II (R6000) | `eabi32` |
| `vr4300-o32` | `mips-unknown-elf` | MIPS III (VR4300) | `o32` |
| `vr4300-x64` | `mips-unknown-elf` | MIPS III (VR4300) | `x64` |
| `n64` | `mips-unknown-elf` | MIPS III (VR4300), `-mfix4300` | `o32` |
| `psp-allegrex` | `mipsallegrexel-sony-psp-elf` | Allegrex | `eabi32` |
| `allegrex-o32` | `mipsallegrexel-sony-psp-elf` | Allegrex | `o32` |
| `mips64-n64`, `mips64el-n64` | `mips64-unknown-elf`, `mips64el-unknown-elf` | MIPS64 | `x64` |

For example, these two commands select the same CPU and ABI:

```sh
cc -c -O2 -mprofile=vr4300-o32 input.x -o input.o
cc -c -O2 -target mips-unknown-elf -march=vr4300 -mabi=o32 input.x -o input.o
```

The MIPS profiles other than `n64` declare the IEEE default floating
environment: no exception traps, and denormal results are kept. The VR4300
FPU raises an unimplemented-operation exception for a denormal operand, and
for a denormal result unless FCSR.FS flushes it, so on a VR4300 that
environment needs a system that emulates those operations. The `n64`
profile declares the environment in which threads of the N64 operating
system start, with FS and the invalid-operation enable set:

```text
fp_traps = ["invalid"];
fp_denormal_operand = "trap";
fp_denormal_result = "flush";
```

Code that runs in another environment declares it in its own profile
([models.md](models.md#profiles)).

`$::_ctc1` changes the FCSR without changing the environment that the profile
declares.

Without `-mnan2008`, MIPS FPUs use the legacy NaN encoding: a set most
significant fraction bit marks a signaling NaN, and an invalid operation
produces the NaN 0x7fbfffff (`f32`) or 0x7ff7ffffffffffff (`f64`), as does
an arithmetic operation whose NaN operands are all signaling. An arithmetic
operation or square root with a quiet NaN operand and a conversion of a NaN
may trap: R4000-class FPUs, the VR4300 among them, take an
unimplemented-operation exception that no FCSR bit masks, and other FPUs
choose the result. Compile-time evaluation does not evaluate such an
operation, so a constant expression that needs one is an error. With
`-mnan2008`, an operation or conversion with a NaN operand produces the first
signaling NaN operand, otherwise the first NaN operand, quieted, with its
sign and the leading bits of its payload. Compile-time evaluation follows
the selected encoding.

`neg.s` and `neg.d` are arithmetic on these FPUs: they raise the
invalid-operation exception for a signaling NaN, need not keep a NaN's sign
and payload, and trap on a VR4300 for a denormal or quiet NaN operand. Unary
`-` uses them only under a profile that traps neither the invalid-operation
exception nor denormal operands and keeps denormal results; otherwise it
flips the sign bit in general registers, as `$::fabs` does.

ABIs:

| ABI | Aliases | Notes |
| --- | --- | --- |
| `x32` | `x` on 32-bit triples | Cross ABI with 32-bit registers; needs a 32-bit triple. |
| `x64` | `x` on `mips64` triples | Cross ABI with 64-bit registers and the triple's address width; needs MIPS III or later. |
| `o32` | `32`, `abi32` | The standard 32-bit C ABI. |
| `eabi32` | `eabi`, `eabi-32` | The embedded ABI used by the PSP toolchain. A single-precision FPU with odd single-precision registers, such as Allegrex, passes `f32` arguments in consecutive registers from `f12`; other FPUs pass each in an even register, as GCC does. |
| `n64` | `64`, `abi64` | The standard 64-bit C ABI; needs a `mips64` triple. |

`x` names `x64` on `mips64` and `mips64el` triples and `x32` on the others.
`x64` has 32-bit addresses and produces ELF32 objects on `mips` and `mipsel`
triples, and 64-bit addresses and ELF64 objects on `mips64` and `mips64el`
triples; its registers and cursors are the same at both widths, and a record
result of up to 256 bits returns in registers, a larger one through a hidden
pointer. The Cross ABIs are not compatible with the C ABIs. Across calls,
`x32` and `x64` preserve `s0`–`s7`, `sp`, and `fp`. Under `x32`, `o32`, and
`eabi32`, whose integer registers have 32 bits, a call preserves only the low
32 bits of `s0`–`s7`, also on a CPU with 64-bit registers; under `x64` and
`n64` it preserves all 64 bits. Select `o32`, `n64`, or `eabi32` at C
boundaries, per declaration with `[[abi("n64")]]` or for the whole
compilation with `-mabi=n64`.

The C ABIs name the C types ([models.md](models.md#entry-properties)), which
the format checkers read; `char`, `short`, `int`, `long long`, `intmax_t`,
`wchar_t`, `wint_t`, `float`, and `double` are `i8`, `i16`, `i32`, `i64`,
`i64`, `i32`, `u32`, `f32`, and `f64` in all three:

| C type | `o32`, `eabi32` | `n64` |
| --- | --- | --- |
| `long` | `i32` | `i64` |
| `size_t`, `ptrdiff_t` | `u32`, `i32` | `u64`, `i64` |
| `long double` | `f64` | `f128` |

A 128-bit integer has 16-byte alignment under `n64` and `x64`, as GCC's
`__int128` under n64, and 8-byte alignment under the other ABIs. It travels
as two 64-bit pieces under `x64` and `n64` and as
four 32-bit pieces under `x32`, `o32`, and `eabi32`, its most significant
piece first on big-endian targets and its least significant piece first on
little-endian ones. `n64` passes it as GCC passes `__int128`: from an even
argument register (`$4`, `$6`, `$8`, or `$10`), or on the stack from a
16-byte boundary, and returns it in `v0` and `v1`. `o32` passes it like a
64-bit integer, from an even argument slot and partly on the stack when the
slots run out, `eabi32` from an even argument register, and both return it
in `v0`, `v1`, `a0`, and `a1`. `x32` and `x64` pass it in the next argument
registers and return it in `v0`, `v1`, `a0`, and `a1` or in `v0` and `v1`.
Its operations are inline sequences on 64-bit halves, which are word pairs
before MIPS III; a division or remainder by a divisor other than a power of
two is a loop of 128 steps, and a conversion from `f32` or `f64` uses only
integer instructions, so it works on MIPS I too.

`n64` passes a structure or union of any size as GCC does: in the remaining
argument registers and then on the stack, a 64-bit chunk of a named
structure that holds an `f64` member in the floating-point register of its
position and every other chunk, unions and unnamed arguments included, in a
general register. A structure of one or two floating members returns them
in `f0` and `f2`, another record of up to 16 bytes returns in `v0` and `v1`,
and a larger one through a hidden pointer.

`-march=` and `-mtune=` name `cpu` model entries
([models.md](models.md#cpu-entries)); `cc --print-cpus` lists the loaded
entries. `model/mips.xm` ships `generic`, the ISA names `mips1` to `mips5`,
`mips32`, `mips32r2`, `mips32r3`, `mips32r5`, `mips32r6`, `mips64`,
`mips64r2`, `mips64r3`, `mips64r5`, and `mips64r6`, and the CPUs that GCC,
Clang, and GNU as accept for these levels, from `r2000`, `r3900`, and
`r4650` to `vr5500`, `24kf`, `m5100`, `sb1`, `loongson3a`, `octeon3`, and
`p6600`, with the other spellings they accept, such as `r4300` for `vr4300`
and `r10k` for `r10000`. Entries named `isa-`
hold an ISA level's instructions and pipeline interlocks without an FPU,
`fpu-` entries an FPU, and `errata-` entries the workarounds of a CPU's
errata; the ISA names and CPUs combine them. CPUs without an FPU, such as
the VR4100 series, the R3900, the 4K, M4K, M14K, 24Kc, 34Kc, 74Kc, and 5Kc
cores, the XLR, and the Octeon before Octeon III, use `-msoft-float`; the
R4650 has a single-precision FPU, and the VR4100 series has no LL/SC. The
`r5900` entry (the PlayStation 2's Emotion Engine) has no LL/SC and no
doubleword multiply or divide, so code that needs them is rejected, and no
FPU: the R5900's single-precision FPU, which has no infinities, NaNs, or
denormals and rounds toward zero, is not modeled. Extensions that
`--print-options=target` does not list, such as DSP, MT, MIPS-3D, MSA,
SmartMIPS, and the Octeon and Loongson instructions, are not modeled, and
Release 3 and 5 CPUs produce Release 2 code and objects.

CPUs whose errata compiled code can avoid, and their other spellings,
enable the workarounds, as GCC does for an explicit `-march` (and, for the
Loongson 3, as Linux does on Loongson 3 systems); `-mfix-NAME` and
`-mno-fix-NAME` select them for any CPU, and tuning for a CPU does not:

| Option | CPUs | Effect |
| --- | --- | --- |
| `-mfix-r4000` | `r4000` | An extended or variable shift may misexecute while a multiply runs: each multiply's `mflo` follows it at once, so none runs in the multiply's shadow. |
| `-mfix-r4400` | `r4400` | A doubleword or variable shift may misexecute right after a divide starts. Divides are always followed by the `mflo` or `mfhi` that reads them and never fill a delay slot, which also avoids the R4000's divide errata. |
| `-mfix-r10000` | `r10000` | LL/SC sequences retry with `beql`; requires branch-likely instructions. |
| `-mfix-loongson3-llsc` | `gs464`, `loongson3a`, `gs464e`, `gs264e` | Every attempt of an LL/SC sequence starts with `sync`, and the exit that a compare-exchange takes between `ll` and `sc` starts with `sync`. |
| `-mfix-r5900` | `r5900` | A loop of six instructions or fewer whose only branch ends it runs only once or twice unless that branch has a `nop` in its delay slot: such a branch keeps its slot empty. |
| `-mfix4300` | (the `n64` profile) | Separate consecutive floating-point multiplies affected by the VR4300 erratum. |

`as` applies `-mfix-r5900` and `-mfix-loongson3-llsc` as GNU as does
([invoke.md](invoke.md)). Assemble `cc -S` output for a Loongson 3 without
GNU as's `-mfix-loongson3-llsc`: the compiler places the SYNCs itself, and
GNU as, which compares label names by their first 15 characters, also puts
SYNCs between the `ll` and the `sc` of a compare-exchange.

MIPS32 and MIPS64 Release 2 CPUs sign-extend bytes and halfwords with `seb`
and `seh`, extract and insert bit fields with `ext` and `ins`, and swap
bytes with `wsbh`; MIPS64 Release 2 CPUs also extract and insert doubleword
fields with `dext`, `dins`, and their `m` and `u` forms, swap doubleword
bytes with `dsbh` and `dshd`, and rotate doublewords with `drotr`,
`drotr32`, and `drotrv`; MIPS32 and MIPS64 CPUs multiply words with `mul`;
and Allegrex code uses its `seb`, `seh`, `ext`, and `ins` words. Each
replaces the sequence it shortens only where the `-mtune` latencies are no
higher.

Release 6 CPUs (`mips32r6`, `mips64r6`, `i6400`, `i6500`, and `p6600`) have
the Release 2 instructions but no branch-likely, conditional-move, HI/LO,
`c.cond.fmt`, or unaligned word instructions. They multiply and divide with
`mul`, `muh`, `mulu`, `muhu`, `div`, `mod`, `divu`, `modu`, and their `d`
forms, select with `seleqz`, `selnez`, and `sel.s` or `sel.d`, and compare
floating values with `cmp.cond.fmt` into `$f1`, which holds no other value
there (`$f4` under `-mfpxx`, whose code keeps 64-bit values in even
registers), branching on it with `bc1eqz` and `bc1nez`; branches keep their
delay slots, and compact branches are not selected. Their FPU has only 64-bit
registers and the IEEE 754-2008 NaN encoding, so the `o32`, `x32`, and
`eabi32` ABIs keep the CPU's 64-bit FPRs there, as GCC does, `-mfpxx` is
accepted, and `-mfp32`, `-mno-nan2008`, `-mbranch-likely`, and `-mcond-move`
are errors. Release 6 requires the system to accept misaligned ordinary
loads and stores, in hardware or by emulation, and its CPUs enable
`-munaligned-access`: misaligned data is accessed with ordinary loads and
stores, as GCC does, unless `-mno-unaligned-access` copies it through bytes,
as on other CPUs. A naked function that compares floating values on
Release 6 names that register, `f1` or `f4`, in its clobbers and binds no
object to it.

The `r2000`, `r3000`, `r6000`, `r4000`, `r4400`, `r4600`, and `vr4300`
entries give those CPUs' multiply and divide cycle counts, all but `r4000`
and `r4400` also their floating-point ones, and `r2000`, `r3000`, and
`r4600` their load latency; the `isa-` entries schedule loads of MIPS I,
MIPS II, and MIPS32 CPUs as four-cycle operations, and the other CPUs take
their costs from the entries they inherit. `cc --print-costs` shows the
costs in effect. A CPU enables the features it implies, and `-m`/`-mno-`
options adjust them:

| Option | Effect |
| --- | --- |
| `-mhard-float`, `-msoft-float`, `-msingle-float` | FPU availability. |
| `-mfp32`, `-mfpxx`, `-mfp64` | FPU register model. The ABI selects the default ([models.md](models.md#entry-properties)): `-mfp32` under `o32`, `x32`, and `eabi32`, `-mfp64` under `n64`, and under `x64` the CPU's, which is `-mfp64` from MIPS IV and MIPS64 on; Release 6 FPUs have only 64-bit registers. `-mfp64` needs MIPS III or MIPS32 Release 2. |
| `-modd-spreg` | The FPU has odd-numbered single-precision registers with 32-bit FPRs; the MIPS32 and MIPS64 CPUs with an FPU other than the GS464, and `allegrex`, enable it. A single-precision FPU then keeps values in all 32 registers; under `-mfp32`, where an `f64` occupies an even/odd pair, a function without `f64` values keeps its `f32` values in all 32 registers and others keep floating values in even registers. `-mfpxx` code and FPUs without them use even registers only. `-mfp64` code keeps values in all 32 registers on every CPU. |
| `-mllsc` | 32-bit LL/SC atomics. |
| `-mfix-NAME` | The errata workarounds above. |
| `-mnan2008` | Use the IEEE 754-2008 NaN encoding of Release 2 FPUs, which Release 6 FPUs always use; the output declares it with `.nan 2008`. |
| `-munaligned-access` | Access misaligned data, such as the members of `[[packed]]` records, with ordinary loads and stores, which the system must accept, as Release 6 systems do; otherwise through bytes. Release 6 CPUs enable it. |
| `-mbranch-likely`, `-mcond-move`, `-mrotate` | Allow branch-likely, conditional-move, and rotate instructions; Release 6 has neither of the first two. |
| `-mcmodel=large` | Materialize full 64-bit addresses and call through registers. |
| `-mlong-calls` | Call every function through a register (`lui`, `addiu`, `jalr`), so callers and callees need not lie in the same 256 MB region that `jal` reaches. |
| `-mabicalls` | SVR4 abicalls code, described below; `-fpic`, `-fPIC`, `-fpie`, and `-fPIE` imply it. |
| `-mno-shared` | With `-mabicalls` and 32-bit addresses, code for an executable, which reaches the symbols the group defines without the GOT. |

Code with 64-bit addresses (`n64`, and `x64` on `mips64` triples) produces
ELF64 objects that assume a 64-bit FPU; the other ABIs produce ELF32, even on
a 64-bit CPU.
The objects record the CPU's ISA level, the ABI (`o32` or `eabi32` in ELF32),
`32bitmode` for 64-bit registers in ELF32, `noreorder`, and `nan2008` in the
header flags, and the floating-point ABI in `.MIPS.abiflags`: soft float,
single precision, 64-bit FPRs, FP32 with odd single-precision registers on
CPUs that have them under `-mfp32` and on CPUs without `ldc1` and `sdc1`
(such as MIPS I), and otherwise FPXX with no odd single-precision registers,
because the code uses only the part of the paired-register convention that
runs on 32- and 64-bit FPUs alike. FP32 objects are compatible with FP32
and FPXX objects but not with 64-bit-FPR ones; `-mfpxx` keeps MIPS32 code
FPXX.
With 64-bit addresses, symbol addresses use two instructions by default, so
symbols must lie in the low 2 GB or in the sign-extended KSEG0/KSEG1 range
unless `-mcmodel=large` is given. Assembly output names general registers by
number (`$8`) under these ABIs and under ABIs whose `elf_abi_tag` is
`"eabi32"` ([models.md](models.md)), such as `eabi32`, because assemblers
read `$t0`–`$t3` there as registers 12–15.

Allegrex has a single-precision FPU: `f64` arithmetic is an error. Because
its code uses odd single-precision registers, which MIPS II lacks, Allegrex
assembly declares no ISA level; assemble it with `as -mprofile=psp-allegrex`
or GNU as `-march=allegrex`.
With `-mno-odd-spreg` it uses even registers only and declares MIPS II. Code that
needs floating point under `-msoft-float` is an error, because Cross does not
call a software floating-point library.

`$::trap()`, like a failed `-fbounds-trap` check, executes `break 7`.

`$::sqrt` executes `sqrt.s` or `sqrt.d`, which MIPS I lacks: there it is an
error and `$::has_intrinsic($::sqrt)` is 0. `$::fabs` and `$::copysign` change
the sign bit in general registers rather than with `abs.fmt` or `neg.fmt`, so
they keep a NaN operand's payload and raise no exception. `$::fmin` and
`$::fmax` branch on quiet comparisons, which compare the second operand also
when the first is a NaN, and copy the selected operand.

`-mabicalls` code is SVR4 abicalls code, as GCC produces it for MIPS
GNU/Linux, and its objects carry the `pic` and `cpic` header flags.
`-fpic`, `-fPIC`, `-fpie`, and `-fPIE` select it unless `-mno-abicalls` is
given, which is then an error. Every call goes through `$t9`, which holds the
callee's address on entry: the GOT gives it through `%call16`, or for a
function of local binding through its page entry. A function that reaches the
GOT computes `$gp` from `$t9` on entry, through `_gp_disp` in `o32` and
`%neg(%gp_rel(...))` with 64-bit addresses, and reaches objects, functions,
and labels through GOT entries: a symbol of local binding through the entry of
its 64 KiB page and a `%lo` or `%got_ofst` displacement, any other through its
own entry. With `-mno-shared` and 32-bit addresses, functions instead take
`$gp` from `__gnu_local_gp`, reach what the group defines with absolute
addresses and `jal`, and use the GOT only for symbols that it does not define
or defines weak; with 64-bit addresses `-mno-shared` code is the same as
other `-mabicalls` code. A call may change `$gp` when the callee's ABI lists
it in `call_clobbers` or, in `-mabicalls` code, in `pic_call_clobbers`
([models.md](models.md#entry-properties)). `o32` lists it in
`pic_call_clobbers`, so callers keep their `$gp` in the frame and reload it
after a call;
`n64` preserves it, so a function that sets up `$gp` saves its caller's first;
`x32` and `x64` let calls change it. With `-mabicalls`, a function that
needs `$gp` under an ABI that preserves `$gp` with 32-bit addresses, an
interface that passes a value in `$t9`, a naked function that would reach the
GOT, a `-mdata-bases` entry that uses `$gp`, and an ABI whose objects carry
the EABI tag are errors.

Thread-local objects go to `.tdata` and `.tbss`. Code reads the thread
pointer with `rdhwr $3,$29` (hardware register 29, UserLocal), which Release
2 CPUs execute when the system allows it; earlier CPUs, such as the VR4300,
trap on it, and the system must emulate it, as Linux does. For them the
instruction is assembled as MIPS32 Release 2. The thread pointer lies 0x7000
past the start of the thread's TLS block, a copy of the TLS segment.
`tls_model` selects `local-exec`, whose offset from the thread pointer the
linker gives (`%tprel_hi` and `%tprel_lo`), or `initial-exec`, which loads
it from the GOT (`%gottprel`); without it, an object that the group defines
without `global` is local-exec and others initial-exec. Without `-mabicalls`
there is no GOT and every access is local-exec, as with GCC. `global-dynamic`
and `local-dynamic`, which call a resolver, are errors.

Supported: integer and pointer operations, 64-bit integers on 32-bit CPUs,
128-bit integers, hard-float `f32` and `f64`, structures and unions by value
under every ABI, function pointers, variable-length arrays, computed goto,
`$::patch` values, LL/SC atomics, variadic functions, `-mabicalls` code,
thread-local storage, the machine-instruction built-ins below, and naked
functions. Not supported: vectors, register locations outside naked
functions, MIPS16, microMIPS, and the Allegrex VFPU. They are diagnosed when
used. A `[[musttail]]` call passes every
argument in a register, cannot target a variadic function or leave a function
that binds variadic state, and neither function may have `out` or `inout`
parameters.

Machine-instruction built-ins. Every instruction of the table
`model/mips_instructions.xm` is a built-in named after its mnemonic, with `_`
for each `.`, whose operands are the instruction's in assembler order:
`$::_addu(d, a, b)` is `addu rd, rs, rt`, `$::_lw(v, p[1])` is
`lw rt, 4(base)`, and `$::_c_eq_s(a, b)` is `c.eq.s`. A model can add others
([models.md](models.md#instruction-entries)). `cc --print-instructions` lists
each form with its feature gates and whether it is privileged or volatile, and
names each table entry's built-in or why it has none. Register operands are
objects of the instruction's width: `u32` or `i32`, `u64` or `i64` from
MIPS III for an instruction on 64-bit registers, and `f32` or `f64` for an
FPR. Immediates, coprocessor register numbers, selects, codes, and cache
operations are constants of the instruction's range, and bit fields satisfy
its constraints (`$::_ext(d, s, 30, 4)` is an error). Memory operands are
lvalues `*pointer` or `pointer[index]`, whose offset the instruction takes,
and a store's lvalue is writable. A destination that the instruction writes
only in part or on a condition, as for `lwl`, `lwr`, `movn`, `movz`, `ins`,
and `mthc1`, starts as its object's value. The `$zero` of `div`, `divu`,
`ddiv`, and `ddivu` is no operand: `$::_divu(a, b)`.

Names other than the mnemonic:

| Built-in | Instruction | Needs |
| --- | --- | --- |
| `$::_sqrt`, `$::_abs`, `$::_neg` with `f32` or `f64` operands | `sqrt.s`/`.d`, `abs.s`/`.d`, `neg.s`/`.d`; `$::_neg` of a `u32` is `neg`, which traps | hard float; `sqrt` MIPS II; `f64` not with `-msingle-float` |
| `$::_rfe(in u32 value)` | `jr` through a register with `rfe` in its delay slot; naked functions only (`$::_rfe()` is `rfe` alone) | MIPS I and II |

These have no built-in, and using one is an error that says why: the
assembler macros (`li`, `dli`, and the forms of `addu`, `and`, `teq`, and
others with an immediate that `as` expands), the instructions that link
(`jal`, `jalr`, `bal`, `bltzal`, `bgezal`, and Release 6's `balc`, `jialc`,
and `beqzalc` to `bltzalc`), branches on the FPU condition (`bc1t`, `bc1f`,
their likely forms, `bc1eqz`, and `bc1nez`) and Release 6's overflow branches
(`bovc`, `bnvc`), instructions with an `$fccN` operand (`movt`, `movf`, and
the condition-code forms of `c.cond.fmt`), Release 6's PC-relative loads and
additions (`addiupc`, `lapc`, `lwpc`, `lwupc`, `ldpc`), and Release 6's
`daui`, `dahi`, and `dati`, whose registers must be other than `$zero` or
equal. `$::_wait` needs MIPS32 or MIPS64, though `as` accepts `wait` for
MIPS III.

An instruction whose only effect is its register result is optimized like
arithmetic: equal ones merge, an unused one goes, and one fills a delay slot.
The others stay in order with memory accesses and each other: loads and
stores, coprocessor 0, TLB, cache, `sync`, `syscall`, `break`, `wait`, the
traps (`teq` and the others, and `add`, `addi`, `sub`, `dadd`, `daddi`,
`dsub`, and `neg`, which trap on overflow), FPU control moves, the FPU
compares `c.cond.fmt`, which write the condition, and Allegrex's `mfic` and
`mtic`. Every
coprocessor 0, FCSR, and hardware register read is volatile, since Count,
Random, Cause, the FCSR flags, and the cycle counter change without a write,
and so are Release 6's `auipc` and `aluipc`, whose results depend on where
they are. An exception handler that a trap, `syscall`, or `break` enters from
managed code must preserve every register. Floating-point operations are not
ordered against `$::_ctc1` or `$::_cfc1`.

`$::_mult`, `$::_div`, `$::_madd`, `$::_mthi`, and the others that write `hi`
and `lo` leave their results there for `$::_mfhi` and `$::_mflo`. Compiled
multiplication, division, and remainder, calls, and branches change `hi` and
`lo`, so a built-in that reads them follows the built-in that wrote them in
straight-line code without those between, or reads what the function
received, as an exception handler that saves them does; otherwise it is an
error.

Branch and jump built-ins (`$::_b`, `$::_j`, `$::_beq`, and the other
comparisons, including the likely forms and Release 6's compact branches)
branch to a label of a naked function; the compiler chooses the branch
instruction and fills its delay slot. In managed code they and the exits
(`$::_jr`, `$::_eret`, `$::_rfe`, and Release 6's `$::_jrc` and `$::_jic`)
are errors.

An instruction that the CPU has only through an extension that the function's
ISA level lacks, such as Allegrex's `max`, `min`, `bitrev`, `wsbw`, `clz`,
`ext`, `ins`, `movn`, `cache`, and `eret`, is written as `.word` with its
mnemonic in a comment, as the compiler writes its own Allegrex instructions,
so that an assembler without the CPU takes it.

Coprocessor 0 and the TLB are not interlocked. The compiler keeps the
distances of the R4000 hazard table, which the VR4300 keeps: two instructions
from `mtc0` to `mfc0` of the same register and to an ERET that reads EPC;
three from a Status or Cause write to any later instruction, because
interrupts are sampled against them, and four from a Status write to a
coprocessor instruction such as ERET; one to three between writes of the TLB
registers, TLB operations, and their reads; and three from `tlbwi` or `tlbwr`
to a load or store. Instructions already scheduled between them count, and
NOPs fill the rest, or one EHB on MIPS32 and MIPS64 Release 2. Every distance
is complete before a branch, jump, call, or return. A store and a `cache`
operation are separated by two instructions that are not loads or cache
operations. MIPS I `mfc0` results have a load delay. Instruction-fetch
hazards are not tracked: code is assumed to run unmapped or through an
unchanged mapping.

A `[[naked]]` function names a register for each parameter and its result,
binds every local object to a register, and leaves through `$::_jr`,
`$::_eret`, or `$::_rfe`:

```text
[[naked, clobber("k0", "k1")]]
global void timer_vector() {
    register u32 now "k0";
    $::_mfc0(now, 9);
    now = now + 46875000u32;
    $::_mtc0(now, 11);
    $::_eret();
}
```

Any general or floating register can hold a bound object, including `k0`,
`k1`, `sp`, and `ra`, and `gp` unless a `data_base` entry uses it.
Temporaries use the registers of the `clobber` list other than `zero` and `at`,
and a bound register that the function writes anyway while its object's value
is not needed. A naked function has
no frame: a value that needs another register, stack storage, a call, a
`return`, or an instruction that writes a register outside its clobbers,
interface, and bound registers (such as `hi` and `lo` for a multiply, which
`clobber("hi", "lo")` permits) is an error. No `clobber` list can name `gp`,
`sp`, or `fp`, which hold compiler state across calls. `[[raw_inline]]`
functions are inlined into it. Other functions call a naked function with
each argument and the result in the register its parameter and result name;
a call keeps every register outside the function's clobbers and interface,
and cannot pass `out` or `inout` parameters.

A variadic definition reads its unnamed arguments through the states of its
ABI. Under `o32` and `n64`, `va_list` is the same state as `arg_area`, the
value C passes for a `va_list`. `eabi32` has no `va_list` state: GCC's
`va_list` there is `arg_area` without a double-precision FPU and a record
with one, as described below. Under `o32`, `arg_area` (`void *`) is the C
`va_start` address: the
definition stores `a0`–`a3` in the 16 bytes that the caller reserves for them,
so the unnamed arguments form one sequence of 4-byte slots in which `f64` and
`i64` values start at a multiple of 8, and the pointer may be passed to a C
function that takes a `va_list`. Under `n64`, `arg_area` is the C `va_list`
in the same way: the definition stores the argument registers of its unnamed
arguments directly below its stack arguments, so that all of them form one
sequence of 8-byte slots; `gp_arg_area` (`u64 *`) has the same address, and
`overflow_arg_area` points at the stack arguments that follow the named ones.
Under `eabi32`, the definition likewise stores the general registers of its
unnamed arguments as 4-byte slots directly below its stack arguments, where
`i64` and `f64` values start at a multiple of 8. Without a double-precision
FPU, every unnamed argument travels in that sequence, and `arg_area` is GCC's
`va_list`. With one, unnamed `f64` values travel in `f12`–`f19`, and GCC's
`va_list` is this record, whose fields hold the states of the same names:

```text
struct va_list_record {
    void *overflow_arg_area;
    void *gpr_top;
    void *fpr_top;
    u8 gpr_offset;
    u8 fpr_offset;
    u8 reserved[2];
};
```

Under `x32` and `x64`, `gp_arg_area` and `fp_arg_area` point at the saved
integer and floating argument registers that follow the named arguments, and
`overflow_arg_area` at the stack arguments that follow them. These Cross ABIs
have no C `va_list`; a definition that forwards its unnamed arguments to C
selects `o32`, `eabi32`, or `n64`, for example with `[[abi("n64")]]`.

### Small data

`model/mips.xm` ships the `data_base` entry `mips-gp`
([models.md](models.md#data-base-entries)). While it is selected, writable
objects of at most 8 bytes that the compilation group defines go to `.sdata`,
or to `.sbss` when they start as zero, and code reaches them through `$gp`
with one instruction and a `%gp_rel` relocation. No profile selects it;
`-mdata-bases=mips-gp` or a profile's `data_bases` property does:

```sh
cc -c -O2 -mprofile=vr4300-o32 -mdata-bases=mips-gp input.x -o input.o
```

```text
global u32 frames;                        // .sbss, reached through $gp
global u32 seed = 7u32;                   // .sdata
global u32 table[64];                     // .bss: more than 8 bytes
global const u32 limit = 64u32;           // .rodata: constants stay out
[[section(".sdata")]] global u32 lookup[16];   // opted in
[[section(".data")]] global u32 status = 1u32; // opted out
[[section(".sdata")]] u32 shared;          // defined elsewhere in .sdata
u32 elsewhere;                            // defined elsewhere: full address
```

An entry whose `kinds` list `readonly` or `literal` also places `const`
objects or the compiler's constants, such as string literals.
`-fdata-sections` names the sections `.sdata.NAME` and `.sbss.NAME`. An object
defined in another compilation is reached through `$gp` only when its
declaration names `.sdata` or `.sbss`. GCC's `-G8` objects can share the
window: GCC places its small objects in `.sdata` and `.sbss`, and by default
it reaches an external object of at most 8 bytes through `$gp`, so a Cross
object that such code uses must lie in small data.

While an entry is selected, Cross code reads `$gp` and never writes it: the
register is not allocated, calls do not save it, and no register location of
a naked function may name it. The program sets `$gp` to the address the
linker gives `_gp` before such code runs, and its linker script keeps
`.sdata` and `.sbss` within 32 KiB on either side of `_gp`:

```text
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    _gp = . + 0x7ff0;
    .sdata : { *(.sdata .sdata.*) }
    .sbss (NOLOAD) : { *(.sbss .sbss.*) *(.scommon) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
```

```text
        lui     $gp,%hi(_gp)
        addiu   $gp,$gp,%lo(_gp)        # daddiu with 64-bit addresses
```

## AArch64

| Triple | Format | Profile | Default ABI |
| --- | --- | --- | --- |
| `aarch64-unknown-linux-gnu` | ELF | `aarch64-linux` | `x` |
| `aarch64-none-elf` | ELF | `aarch64-elf` | `x` |

The AArch64 target describes A64 code with little-endian ELF64 objects and
64-bit pointers. `cc` compiles an integer subset of Cross for it
([AArch64 code generation](#aarch64-code-generation)), `as` assembles A64
into relocatable objects ([invoke.md](invoke.md#assembling)) with the
instructions of `model/aarch64_instructions.xm`
([models.md](models.md#aarch64-instructions)), and `ld` links AArch64
objects into static executables and, with `-pie`, static
position-independent executables ([invoke.md](invoke.md#linking)). A triple
with
`linux` selects the `aarch64-linux` profile and any other `aarch64-*` triple
`aarch64-elf`; the models describe ELF systems, not Darwin's or Windows'
variants of AAPCS64.

| Profile | Triple | CPU | ABI |
| --- | --- | --- | --- |
| `aarch64-linux` | `aarch64-unknown-linux-gnu` | generic (Armv8-A) | `x` |
| `aarch64-elf` | `aarch64-none-elf` | generic (Armv8-A) | `x` |
| `switch` | `aarch64-none-elf` | `-march=armv8-a+crc+crypto -mtune=cortex-a57`, with `-fpie` | `x` |

The `switch` profile follows devkitA64's options for Nintendo Switch
homebrew. It reserves no register and describes no thread-local storage.

ABIs:

| ABI | Aliases | Use |
| --- | --- | --- |
| `x` | | The Cross ABI: default for global, unresolved, and indirect calls. |
| `aapcs64` | `lp64` | The AAPCS64 C ABI of GNU/Linux and ELF systems. |

Under both ABIs, calls preserve `x19`–`x28`, `x29`, `sp`, the low 64 bits of
`v8`–`v15` (`d8`–`d15`), and FPCR; every other register, NZCV, and FPSR may
change. `x` passes arguments in `x0`–`x15` and then `x19`–`x28`, and in
`v0`–`v7` and then `v16`–`v31`, and returns results in `x0`–`x7` and
`v0`–`v3`; `x16`, `x17`, and `x18` carry nothing. A record of up to four
members of one floating or vector type takes one SIMD and floating-point
register for each member; another record of up to 512 bits travels in
64-bit pieces, a piece that holds only floating members in a SIMD and
floating-point register, and returns in registers up to 256 bits and
otherwise through a hidden pointer in `x0`. Without floating-point
registers (`-mno-fp`), `x` passes floating values in general registers.

`aapcs64` passes arguments in `x0`–`x7` and `v0`–`v7`, unnamed ones as named
ones, and returns results in the registers that the same value would take as
the first argument. A homogeneous floating-point or short-vector aggregate
(HFA, HVA) has one to four members of one floating type or one vector width,
counting nested records, arrays, and union members, and no padding; it takes
one register per member. A 64- or 128-bit vector takes a SIMD and
floating-point register. Any other record or vector of up to 16 bytes takes
one or two general registers, starting at an even one when it is 16-byte
aligned, as a 128-bit integer does; a larger one is passed by reference and
returned to an address in `x8`, which takes no argument register. A value
that finds too few registers left goes on the stack, and so do all later
values of its register class; a stack value takes whole 8-byte slots from an
8-byte boundary, or from a 16-byte boundary when it is aligned to 16 bytes
or more. An integer argument narrower than 64 bits leaves the register bits
above it unspecified. Without floating-point registers (`-mno-fp`),
`aapcs64` has no location for a floating value, as GCC rejects such code.
The `va_list` state of `aapcs64` is the address of a record of `__stack`,
`__gr_top`, `__vr_top`, `__gr_offs`, and `__vr_offs`, the five states of
AAPCS64's `va_list`, so a `variadic(void *arguments "va_list")` binding can
be passed to a C function that takes a `va_list`. The Cross ABI `x` has no
`va_list` state; its `gp_arg_area` and `fp_arg_area` point at the saved
argument registers that follow the named arguments, and `overflow_arg_area`
at the stack arguments that follow them.

### AArch64 code generation

`cc -S` writes GNU syntax that GNU `as`, LLVM's assembler, and `as` accept,
and `cc -c` assembles it with `as`. Code generation covers:

- integers of 8 to 128 bits, `bool`, pointers, and labels: arithmetic,
  bitwise operations, shifts, comparisons, selection (`csel`),
  multiplication, division and remainder (`sdiv` and `udiv`, with `msub`
  for a remainder), extension, and truncation, and the bit intrinsics
  ([Bit intrinsics](#bit-intrinsics)); a 128-bit integer is a pair of
  general registers, and its operations are sequences on the halves;
- constants, with `movz`, `movn`, `movk`, or one `orr` of a logical
  immediate;
- loads and stores of every integer width, signed and unsigned, in frame
  slots, global objects, and through pointers, with immediate offsets, scaled
  indexes, and the low 12 bits of a symbol;
- addresses of objects, functions, and labels in the small code model:
  `adrp` and `add`, and under `-fpic` a load from the GOT for a symbol that
  another module may preempt;
- records and arrays as values: whole copies, and arguments and results
  in general registers, on the stack, by reference, and to a result
  address, as the ABI places them;
- branches and loops, computed `goto`, and `global label`s;
- direct and indirect calls, `[[musttail]]` calls, and functions that never
  return, under `x`, `aapcs64`, and ABI models of your own: arguments,
  results, and `out` and `inout` parameters of the types above, in general
  registers and on the stack, also as the pieces that a model splits a
  value into.

A function with frame storage or calls keeps a frame record (`stp x29, x30`
and `mov x29, sp`) and a 16-byte aligned stack, and saves the registers that
its ABI preserves and its code changes, such as `x19` and `x20` when it
passes a seventeenth and eighteenth argument under `x`. `-funwind-tables`
and `-g` describe its frame with CFI, and `-g` also gives line rows and the
frame slots of local objects. `-ffunction-sections`, `-fdata-sections`,
`[[section]]`, `[[weak]]`, and `[[visibility]]` apply as on the other ELF
targets.

There is no register allocator: at every optimization level each value
lives in a frame slot, and each instruction loads its operands into `x9` to
`x15` and stores its result, so the code is correct but large and slow. An
indirect call branches through `x17`, and a frame slot that a load or store
cannot reach from `sp` is reached through `x16`. Members of packed records
are loaded and stored as aligned ones are, which needs memory that permits
unaligned accesses: not the memory of an AArch64 CPU whose MMU is off.

`cc` reports the following with an error at their source location instead:
floating-point values and conversions, so also records whose ABI pieces go
in SIMD and floating-point registers, vectors, variadic functions,
`[[naked]]` functions, manual register locations of parameters and results,
variable-length arrays, atomic operations, thread-local objects, `$::patch`
values, machine instruction built-ins (`$::_name`), and a `[[musttail]]` call
that needs stack arguments, passes an argument in a register that the
caller's ABI preserves, or passes or returns a record in memory. Mach-O and
COFF triples (`aarch64-apple-darwin`, Windows) have no code generator.

Target options:

| Option | Values |
| --- | --- |
| `-march=`, `-mtune=` | A `cpu` model entry ([models.md](models.md#cpu-entries)), listed by `cc --print-cpus`; `-march` also takes `+FEATURE` and `+noFEATURE` modifiers, as in `-march=armv8-a+crc+crypto`. |
| `-mFEATURE`, `-mno-FEATURE` | The extensions of GCC's AArch64 feature modifiers, such as `fp`, `simd`, `crc`, `crypto`, `aes`, `sha2`, `lse`, `fp16`, `dotprod`, `sve2`, and `sme2`, and the architecture levels `armv8-a` to `armv9.8-a` (see `--print-options=target`) |
| `-mrisc-cisc-balance=` | `0` to `100`; the AArch64 profiles set `0`. |

Each architecture level is a feature that requires the level before it and
enables the extensions that GCC's `-march` of the level enables: `armv8-a`
enables `fp` and `simd`, `armv8.1-a` also `lse`, `crc`, and `rdma`, and
`armv9-a` also `sve2` and what it requires. `armv9.7-a` and `armv9.8-a` are
Clang's levels. Disabling an extension keeps the level, as in GCC:
`-march=armv8.1-a+nolse` keeps `armv8.1-a`, `crc`, and `rdma`, and
`-march=armv8-a+nofp` keeps only `armv8-a`. An extension requires what GCC
gives it, so disabling one disables the extensions that require it, as
`-mno-simd` disables `dotprod` and `sve`. As in GCC, `crypto` requires `aes`
and `sha2`, and disabling it also disables `sm4`; `sve2-aes`, `sve2-bitperm`,
`sve2-sha3`, and `sve2-sm4` likewise name `sve2` with `sve-aes`,
`sve-bitperm`, `sve-sha3`, and `sve-sm4`; selecting `fp16fml` also enables
`fp16`, and disabling `fp16` also disables `fp16fml`, while `armv8.4-a`
enables `fp16fml` without `fp16`.

`model/aarch64.xm` ships an entry for each architecture level, for every
CPU name that GCC or Clang accepts for AArch64, such as `cortex-a57`,
`cortex-a76`, `neoverse-v2`, `apple-m1`, `thunderx2t99`, `a64fx`, and
`oryon-1`, with their other names, such as `ares` for `neoverse-n1` and
`cobalt-100` for `neoverse-n2`, and for GCC's big.LITTLE pairs, such as
`cortex-a57.cortex-a53`, which take their big core's entry. `generic` is
`armv8-a`. Where GCC and Clang give a CPU different extensions, its entry
has those that both give it: Clang lists `aes` and `sha2` for Arm's
Cortex-A cores, which have them only as an option, and GCC does not, so
`-march=cortex-a57` enables `crc` and not `crypto`; GCC lists Apple's M
series with their architecture levels only. A modifier adds a feature, as in
`-march=cortex-a57+crypto`. A CPU that adds features to another of its line
inherits that CPU and takes the features from an entry named `ext-` and its
own name. The CPUs that LLVM schedules with one model inherit an entry
named `pipeline-` and the model's name, which gives LLVM's issue width;
the Cortex-A57 model's entry also sets a 16-byte function alignment, and
the architecture levels and `generic` tune like LLVM's generic CPU, whose
model is the Cortex-A510's. No AArch64 instruction has costs, so
`cc --print-costs` lists none.

## Bit intrinsics

`$::clz`, `$::ctz`, `$::popcount`, `$::rotl`, `$::rotr`, and `$::bswap`
compile to instructions where the `-march` CPU has them and their `-mtune`
latencies (`cc --print-costs`) add up to no more than the inline sequence,
which counts one cycle for each operation, the latency of a multiplication,
and the cost of materializing each constant. Otherwise they compile to the
inline sequence. A `cpu` entry that marks an instruction `available = false`
or raises its `latency` therefore selects the sequence
([models.md](models.md#cpu-entries)). No form calls a function.

| Operation | x86-64 | MIPS | AArch64 |
| --- | --- | --- | --- |
| `$::clz` | `lzcnt` with LZCNT; otherwise `bsr`, `cmovne`, and `xor` | `clz` on MIPS32, MIPS64, and Allegrex, or `clo` for a complement `~x`; `dclz` or `dclo` for 64 bits on MIPS64 | `clz` |
| `$::ctz` | `tzcnt` with BMI; otherwise `bsf` and `cmovne` | the width less the leading ones of `x \| -x` (`clo`) where `clz` exists | `rbit` and `clz` |
| `$::popcount` | `popcnt` with POPCNT | none | none |
| `$::bswap` | `bswap`, or `rolw $8` for 16 bits | `wsbh` and `rotr` on MIPS32 and MIPS64 Release 2, `wsbw` on Allegrex; `wsbh` and `andi` for 16 bits; `dsbh` and `dshd` for 64 bits on MIPS64 Release 2 | `rev`, or `rev16` for 16 bits |
| `$::rotl`, `$::rotr` | `rol`, `ror` | `rotr` or `rotrv` with `-mrotate`, and for 64 bits `drotr`, `drotr32`, or `drotrv` on MIPS64 Release 2; otherwise two shifts and `or` | `ror` or `rorv`, after `neg` of a left rotation's count |

The inline sequences use neither branches nor data tables:

- leading zeros: a binary search that, for `k` from half the width down to
  1, shifts the value left by `k` and counts `k` when it is below
  2^(width - k), and counts one more for zero;
- trailing zeros: the population count of `~(x | -x)`, whose ones are the
  trailing zeros of `x`;
- population count: sums of bit pairs, nibbles, and bytes under masks, the
  byte sums added by shifts or collected by one multiplication by 0x0101…01,
  whichever the costs favor;
- byte swap: exchanges of neighboring bytes, then of larger pieces, under
  masks;
- rotation: two shifts by the count and its negation modulo the width.

8- and 16-bit operands use the 32-bit form on their zero-extended value or
the sequence at their own width, whichever is cheaper. An operand wider than
a general register (64 and 128 bits on MIPS I, MIPS II, MIPS32, and
Allegrex, 128 bits on x86-64 and on MIPS III and later) combines its halves:
a count of the half on the counted side, plus that of the other half when
the first is zero, the sum of the halves' population counts, and the swapped
halves exchanged. A 128-bit rotation uses 128-bit shifts on x86-64; on MIPS
it exchanges the halves when the count modulo 128 is 64 or more and then
shifts each half, filling it with bits of the other. Allegrex's `clz`, `clo`, `wsbh`, `wsbw`, `rotr`, and `rotrv`
appear as `.word` directives with the instruction in a comment; its `clz`
and `clo` are SPECIAL functions 0x16 and 0x17, not the MIPS32 encodings.

## Limits on all targets

- `-fbounds-trap` checks only subscripts of arrays with a known bound; it
  does not check subscripts of pointers or `out` and `inout` channels.
- `-g` gives variables locations only where one home holds them for the
  whole function: frame slots (on Mach-O only in functions whose frame is
  set up at entry), parameters that the register allocator does not assign,
  register-bound objects, and global objects. Values kept in registers by
  the optimizer, the parameters and locals of inlined calls, the pointer
  through which an `out` or `inout` parameter is copied back, typedef names,
  lexical scopes, thread-local objects, and big-endian bit-fields are not
  described, and naked functions have no call-frame information.
- A function is wrapped for another ABI or other register locations only
  when it is named directly; an explicit cast of a function-pointer value
  reinterprets it without a wrapper; see
  [language.md](language.md#function-pointers).
- `-emit-gimple` rejects what GCC's `__GIMPLE` input cannot express or the
  serializer does not encode, among them aggregate static initializers,
  variadic functions and calls through a variadic function pointer, label
  addresses, `$::patch` values, floating intrinsics, bit intrinsics that
  compile to instructions, and `[[musttail]]` calls.
- `-emit-llvm` and `-emit-gimple` output ignores the profile's floating
  environment: it is valid only where no exception traps and denormal results
  are kept.

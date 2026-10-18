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

`amd64-*` is accepted for `x86_64-*`. The `x86_64-windows` and `x86_64-elf`
profiles select the first two triples. ABIs:

| ABI | Aliases | Use |
| --- | --- | --- |
| `x` | | The Cross ABI: default for global, unresolved, and indirect calls. |
| `sysv_abi` | `linux` | The System V AMD64 C ABI. |
| `ms_abi` | `ms`, `win64`, `windows` | The Microsoft x64 C ABI. |

The Cross ABI is not compatible with either C ABI. Across calls it preserves
`rbx`, `r12`–`r15`, and the stack and frame registers; every other general
register and all SIMD, mask, and x87 state may change. Use `sysv_abi` or
`ms_abi` at C boundaries.

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

`-march=` and `-mtune=` name `cpu` model entries
([models.md](models.md#cpu-entries)); `cc --print-cpus` lists the loaded
entries. `model/mips.xm` ships `generic`, the ISA names `mips1` to `mips5`,
`mips32`, `mips32r2`, `mips32r3`, `mips32r5`, `mips64`, `mips64r2`,
`mips64r3`, and `mips64r5`, and the CPUs that GCC, Clang, and GNU as accept
for these levels, from `r2000`, `r3900`, and `r4650` to `vr5500`, `24kf`,
`m5100`, `sb1`, `loongson3a`, and `octeon3`, with the other spellings they
accept, such as `r4300` for `vr4300` and `r10k` for `r10000`. Not shipped
are the Release 6 CPUs (`mips32r6`, `mips64r6`, `i6400`, `i6500`, `p6600`),
whose instruction encodings Cross does not implement. Entries named `isa-`
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
| `-mfp32`, `-mfpxx`, `-mfp64` | FPU register model. The ABI selects the default ([models.md](models.md#entry-properties)): `-mfp32` under `o32`, `x32`, and `eabi32`, `-mfp64` under `n64`, and under `x64` the CPU's, which is `-mfp64` from MIPS IV and MIPS64 on. |
| `-modd-spreg` | The FPU has odd-numbered single-precision registers with 32-bit FPRs; the MIPS32 and MIPS64 CPUs with an FPU other than the GS464, and `allegrex`, enable it. A single-precision FPU then keeps values in all 32 registers; under `-mfp32`, where an `f64` occupies an even/odd pair, a function without `f64` values keeps its `f32` values in all 32 registers and others keep floating values in even registers. `-mfpxx` code and FPUs without them use even registers only. `-mfp64` code keeps values in all 32 registers on every CPU. |
| `-mllsc` | 32-bit LL/SC atomics. |
| `-mfix-NAME` | The errata workarounds above. |
| `-mnan2008` | Use the IEEE 754-2008 NaN encoding of MIPS32 Release 2 FPUs; the output declares it with `.nan 2008`. |
| `-mbranch-likely`, `-mcond-move`, `-mrotate` | Allow branch-likely, conditional-move, and rotate instructions. |
| `-mcmodel=large` | Materialize full 64-bit addresses and call through registers. |
| `-mlong-calls` | Call every function through a register (`lui`, `addiu`, `jalr`), so callers and callees need not lie in the same 256 MB region that `jal` reaches. |

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

Supported: integer and pointer operations, 64-bit integers on 32-bit CPUs,
hard-float `f32` and `f64`, structures and unions by value under every ABI,
function pointers, variable-length arrays, computed goto, `$::patch` values,
LL/SC atomics, variadic functions, the machine-instruction built-ins below,
and naked functions. Not supported: vectors, integers wider than 64 bits,
register locations outside naked functions, position-independent code
(`-mabicalls`), thread-local storage, MIPS16, microMIPS, and the Allegrex
VFPU. They are diagnosed when used. A `[[musttail]]` call passes every
argument in a register, cannot target a variadic function or leave a function
that binds variadic state, and neither function may have `out` or `inout`
parameters.

Machine-instruction built-ins (`--print-instructions` lists each form with
its feature gates and whether it is privileged or volatile):

| Built-in | Instruction | Needs |
| --- | --- | --- |
| `$::_mfc0(out u32 value, n)`, `$::_mtc0(in u32 value, n)` | `mfc0`, `mtc0` of coprocessor 0 register `n` | |
| `$::_mfc0(out u32 value, n, sel)`, `$::_mtc0(in u32 value, n, sel)` | `mfc0`, `mtc0` of register `n` with select `sel` | MIPS32 |
| `$::_dmfc0`, `$::_dmtc0` with `u64` | `dmfc0`, `dmtc0`, with an optional select | MIPS III; with a select MIPS64 |
| `$::_tlbp()`, `$::_tlbr()`, `$::_tlbwi()`, `$::_tlbwr()` | TLB probe, read, and writes | |
| `$::_cache(operation, object)` | `cache operation` on the line that holds `object` (`*pointer` or `pointer[index]`) | MIPS III or MIPS32 |
| `$::_sync()` | `sync` | MIPS II |
| `$::_syscall()`, `$::_syscall(code)`, `$::_break()`, `$::_break(code)` | `syscall` with a 20-bit code, `break` with a 10-bit code | |
| `$::_wait()` | `wait` | MIPS32 |
| `$::_cfc1(out u32 value, n)`, `$::_ctc1(in u32 value, n)` | FPU control register `n`; 31 is the FCSR | hard float |
| `$::_sqrt`, `$::_abs`, `$::_neg` with `f32` or `f64` operands | `sqrt.s`/`.d`, `abs.s`/`.d`, `neg.s`/`.d` | hard float; `sqrt` MIPS II; `f64` not with `-msingle-float` |
| `$::_eret()` | `eret`; naked functions only | MIPS III or MIPS32 |
| `$::_rfe(in u32 value)` | `jr` through a register with `rfe` in its delay slot; naked functions only | MIPS I and II |
| `$::_jr(in value)` | `jr` through a register; naked functions only | |
| `$::_b(label)` | branch to a label of the function; naked functions only | |
| `$::_beq(in a, in b, label)`, `$::_bne(in a, in b, label)` | branch if two registers are equal or differ; naked functions only | |
| `$::_beqz`, `$::_bnez`, `$::_bltz`, `$::_bgez`, `$::_blez`, `$::_bgtz` with `(in a, label)` | branch on a register compared with zero, as a signed integer for an ordering; naked functions only | |
| `$::_nop()` | `nop` | |

Register numbers, selects, codes, and cache operations are constants.
Coprocessor 0, TLB, cache, `sync`, `syscall`, `break`, `wait`, and FCSR
forms are kept, in order with memory accesses and each other; every
coprocessor 0 and FCSR read is volatile, since Count, Random, Cause, and the
FCSR flags change without a write. An exception handler that `syscall` or
`break` enters from managed code must preserve every register. Branch forms
take `u32` and `i32` or, from MIPS III, `u64` and `i64` operands, and the
compiler fills their delay slots. `sqrt`, `abs`, and `neg`
are optimized like arithmetic. Floating-point operations are not ordered
against `$::_ctc1` or `$::_cfc1`.

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
ABI. Under `o32`, `arg_area` (`void *`) is the C `va_start` address: the
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

## Bit intrinsics

`$::clz`, `$::ctz`, `$::popcount`, `$::rotl`, `$::rotr`, and `$::bswap`
compile to instructions where the `-march` CPU has them and their `-mtune`
latencies (`cc --print-costs`) add up to no more than the inline sequence,
which counts one cycle for each operation, the latency of a multiplication,
and the cost of materializing each constant. Otherwise they compile to the
inline sequence. A `cpu` entry that marks an instruction `available = false`
or raises its `latency` therefore selects the sequence
([models.md](models.md#cpu-entries)). No form calls a function.

| Operation | x86-64 | MIPS |
| --- | --- | --- |
| `$::clz` | `lzcnt` with LZCNT; otherwise `bsr`, `cmovne`, and `xor` | `clz` on MIPS32, MIPS64, and Allegrex, or `clo` for a complement `~x`; `dclz` or `dclo` for 64 bits on MIPS64 |
| `$::ctz` | `tzcnt` with BMI; otherwise `bsf` and `cmovne` | the width less the leading ones of `x \| -x` (`clo`) where `clz` exists |
| `$::popcount` | `popcnt` with POPCNT | none |
| `$::bswap` | `bswap`, or `rolw $8` for 16 bits | `wsbh` and `rotr` on MIPS32 and MIPS64 Release 2, `wsbw` on Allegrex; `wsbh` and `andi` for 16 bits; `dsbh` and `dshd` for 64 bits on MIPS64 Release 2 |
| `$::rotl`, `$::rotr` | `rol`, `ror` | `rotr` or `rotrv` with `-mrotate`, and for 64 bits `drotr`, `drotr32`, or `drotrv` on MIPS64 Release 2; otherwise two shifts and `or` |

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
a general register (64 bits on MIPS I, MIPS II, MIPS32, and Allegrex, 128
bits on x86-64) combines its halves: a count of the half on the counted side,
plus that of the other half when the first is zero, the sum of the halves'
population counts, and the swapped halves exchanged; a 128-bit rotation uses
128-bit shifts. Allegrex's `clz`, `clo`, `wsbh`, `wsbw`, `rotr`, and `rotrv`
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

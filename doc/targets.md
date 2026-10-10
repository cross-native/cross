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

The Cross ABI is not compatible with either C ABI. Across calls it preserves
`rbx`, `r12`–`r15`, and the stack and frame registers; every other general
register and all SIMD, mask, and x87 state may change. Use `sysv_abi` or
`ms_abi` at C boundaries.

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

`$::trap()`, like a failed `-fbounds-trap` check, executes `ud2`.

The x86-64 profiles declare the floating environment in which these systems
start a program: MXCSR and the x87 control word mask every exception, and
denormals are kept (FTZ and DAZ clear). Code that unmasks exceptions or sets
FTZ needs a profile that declares it ([models.md](models.md#profiles)). The
`f32` and `f64` relational operators compile to `ucomiss` and `ucomisd`, which
raise the invalid-operation exception only for a signaling NaN.

The floating intrinsics use SSE for `f32` and `f64` and the x87 unit for
`f80`; `$::fmin` and `$::fmax` are `minss`/`maxss` (`minsd`/`maxsd`) with an
ordered compare that substitutes the second operand for a NaN first one. On
`f128`, `$::fabs` and `$::copysign` change the sign bit, and `$::sqrt`,
`$::fmin`, and `$::fmax` are errors. The `f80` forms round as the x87
precision control selects, which must be the 64-bit significand.

A `[[musttail]]` call cannot target a variadic function or leave a function
that binds variadic state, and passes every argument in a register. With
automatic locations, each argument must be an integer or a pointer and the
result must fit in one register. A function with register or stack locations
can tail-call only another such function or function pointer, and neither may
have `out` or `inout` parameters.

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

The MIPS profiles declare the IEEE default floating environment: no
exception traps, and denormal results are kept. The VR4300 FPU raises an
unimplemented-operation exception for a denormal operand, and for a denormal
result unless FCSR.FS flushes it, so on a VR4300 that environment needs a
system that emulates those operations. Code that runs with traps enabled or
with FS set declares its environment in its own profile; threads of the N64
operating system, for example, start with FS and the invalid-operation enable
set:

```text
profile "n64-thread" {
    target = "mips-unknown-elf";
    abi = "o32";
    mangling = "cross";
    m.arch = "vr4300";
    fp_traps = ["invalid"];
    fp_denormal_operand = "trap";
    fp_denormal_result = "flush";
}
```

`$::_ctc1` changes the FCSR without changing the environment that the profile
declares.

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
ABIs. Across calls, `cross32`, `cross64`, and `cross-n64` preserve `s0`–`s7`,
`sp`, and `fp`. Under `cross32`, `o32`, and `eabi32`, whose integer registers
have 32 bits, a call preserves only the low 32 bits of `s0`–`s7`, also on a
CPU with 64-bit registers; under `cross64`, `cross-n64`, and `n64` it
preserves all 64 bits. Select `o32`, `n64`, or `eabi32` at C boundaries, per
declaration with `[[abi("n64")]]` or for the whole compilation with
`-mabi=n64`.

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
| `-mlong-calls` | Call every function through a register (`lui`, `addiu`, `jalr`), so callers and callees need not lie in the same 256 MB region that `jal` reaches. |

`cross-n64` and `n64` code uses 64-bit addresses and produces ELF64 objects
that assume a 64-bit FPU; the other ABIs produce ELF32, even on a 64-bit CPU.
With 64-bit addresses, symbol addresses use two instructions by default, so
symbols must lie in the low 2 GB or in the sign-extended KSEG0/KSEG1 range
unless `-mcmodel=large` is given, and assembly output names general registers
by number (`$8`) because 64-bit assemblers read `$t0`–`$t3` differently.

Allegrex has a single-precision FPU: `f64` arithmetic is an error. Code that
needs floating point under `-msoft-float` is an error, because Cross does not
call a software floating-point library.

`$::trap()`, like a failed `-fbounds-trap` check, executes `break 7`.

`$::sqrt` executes `sqrt.s` or `sqrt.d`, which MIPS I lacks: there it is an
error and `$::has_intrinsic($::sqrt)` is 0. `$::fabs` and `$::copysign` change
the sign bit in general registers rather than with `abs.fmt` or `neg.fmt`, so
they keep a NaN operand's payload and raise no exception. `$::fmin` and
`$::fmax` branch on quiet comparisons and copy the selected operand.

Supported: integer and pointer operations, 64-bit integers on 32-bit CPUs,
hard-float `f32` and `f64`, structures and unions by value under every ABI,
function pointers, variable-length arrays, computed goto, `$::patch` values,
LL/SC atomics, variadic functions, the machine-instruction built-ins below,
and naked functions. Not supported: vectors, integers wider than 64 bits,
register locations outside naked functions, position-independent code
(`-mabicalls`), thread-local storage, MIPS16, microMIPS, and the Allegrex
VFPU. They are diagnosed when used. With `-g`, MIPS objects have the compile
unit with its global variables and types, but no line table, function
entries, or call-frame information; with `-funwind-tables` as well, the
unwind records are also written to `.debug_frame`. A `[[musttail]]` call passes every
argument in a register, cannot target a variadic function or leave a function
that binds variadic state, and neither function may have `out` or `inout`
parameters.

Machine-instruction built-ins (`--print-instructions` lists each form with
its feature gates and whether it is privileged or volatile):

| Built-in | Instruction | Needs |
| --- | --- | --- |
| `$::_mfc0(out u32 value, n)`, `$::_mtc0(in u32 value, n)` | `mfc0`, `mtc0` of coprocessor 0 register `n` | |
| `$::_dmfc0`, `$::_dmtc0` with `u64` | `dmfc0`, `dmtc0` | MIPS III |
| `$::_tlbp()`, `$::_tlbr()`, `$::_tlbwi()`, `$::_tlbwr()` | TLB probe, read, and writes | |
| `$::_cache(operation, object)` | `cache operation` on the line that holds `object` (`*pointer` or `pointer[index]`) | MIPS III or MIPS32 |
| `$::_sync()` | `sync` | MIPS II |
| `$::_cfc1(out u32 value, n)`, `$::_ctc1(in u32 value, n)` | FPU control register `n`; 31 is the FCSR | hard float |
| `$::_sqrt`, `$::_abs`, `$::_neg` with `f32` or `f64` operands | `sqrt.s`/`.d`, `abs.s`/`.d`, `neg.s`/`.d` | hard float; `sqrt` MIPS II; `f64` not with `-msingle-float` |
| `$::_eret()` | `eret`; naked functions only | MIPS III or MIPS32 |
| `$::_jr(in value)` | `jr` through a register; naked functions only | |
| `$::_nop()` | `nop` | |

Register numbers and cache operations are constants. Coprocessor 0, TLB,
cache, `sync`, and FCSR forms are kept, in order with memory accesses and each
other; every coprocessor 0 and FCSR read is volatile, since Count, Random,
Cause, and the FCSR flags change without a write. `sqrt`, `abs`, and `neg`
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
NOPs fill the rest, or one EHB on MIPS32 Release 2. Every distance is complete
before a branch, jump, call, or return. A store and a `cache` operation are
separated by two instructions that are not loads or cache operations. MIPS I
`mfc0` results have a load delay. Instruction-fetch hazards are not tracked:
code is assumed to run unmapped or through an unchanged mapping.

A `[[naked]]` function names a register for each parameter and its result,
binds every local object to a register, and leaves through `$::_jr` or
`$::_eret`:

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
`k1`, `sp`, and `ra`. Temporaries use the registers of the `clobber` list,
and a bound register that the function writes anyway while its object's value
is not needed. A naked function has
no frame: a value that needs another register, stack storage, a call, a
`return`, or an instruction that writes a register outside its clobbers,
interface, and bound registers (such as `hi` and `lo` for a multiply) is an
error. `[[raw_inline]]` functions whose bodies are one basic block are inlined
into it. Other functions cannot call a naked function that has parameters or
a result.

A variadic definition reads its unnamed arguments through the states of its
ABI. Under `o32`, `arg_area` (`void *`) is the C `va_start` address: the
definition stores `a0`–`a3` in the 16 bytes that the caller reserves for them,
so the unnamed arguments form one sequence of 4-byte slots in which `f64` and
`i64` values start at a multiple of 8, and the pointer may be passed to a C
function that takes a `va_list`. Under `cross32`, `cross64`, and `cross-n64`,
`gp_arg_area` and `fp_arg_area` point at the saved integer and floating
argument registers that follow the named arguments and `overflow_arg_area` at
the stack arguments that follow them; `n64` has `gp_arg_area` for its one
sequence of argument registers and `overflow_arg_area`. Under `eabi32`,
`arg_area` reaches only the unnamed arguments passed on the stack.

## Limits on all targets

- `-fbounds-trap` checks only subscripts of arrays with a known bound; it
  does not check subscripts of pointers or `out` and `inout` channels.
- `-g` describes inlined code with the lines of the function it came from,
  inside its caller, without inlined-subroutine entries. Variables have
  locations only where one home holds them for the whole function: the frame
  slots of functions whose frame is set up at entry, parameters that the
  register allocator does not assign, register-bound objects, and global
  objects. Values kept in registers by the optimizer, the pointer through
  which an `out` or `inout` parameter is copied back, typedef names, lexical
  scopes, thread-local objects, and big-endian bit-fields are not described,
  and naked functions have no call-frame information.
- Generic records and unions (`struct list<T>`) are specified but not
  implemented; `$::feature::generic_types` is absent.
- A function is wrapped for another ABI or other register locations only
  when it is named directly; an explicit cast of a function-pointer value
  reinterprets it without a wrapper; see
  [language.md](language.md#function-pointers).
- `-emit-gimple` rejects what GCC's `__GIMPLE` input cannot express or the
  serializer does not encode, among them aggregate static initializers,
  variadic functions and calls through a variadic function pointer, label
  addresses, `$::patch` values, floating intrinsics, and `[[musttail]]` calls.
- `-emit-llvm` and `-emit-gimple` output ignores the profile's floating
  environment: it is valid only where no exception traps and denormal results
  are kept.
- Compile-time evaluation takes the truth value of a floating operand (in a
  condition, `!`, `&&`, `||`, or a conversion to `bool`) as raising no
  exception; at run time, a signaling NaN, or a denormal operand that the
  profile traps, traps there.
- On MIPS, a function that allocates a variable-length array cannot also have
  a local aligned beyond the stack alignment (8 bytes under o32 and EABI, 16
  under n64 and the Cross ABIs).

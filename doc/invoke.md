# Using cc, cpp, as, and ld

```text
cc [options] input.x ...
cpp [options] input.x ...
as [options] input.s ...
ld [options] file ...
```

`cc` preprocesses and compiles Cross source (`.x`) to assembly or object
files. `cpp` only preprocesses, writing Cross to standard output or to `-o`.
`as` assembles hand-written assembly into an object file
([Assembling](#assembling)) -- GNU-syntax MIPS or AT&T-syntax x86-64 -- and
`ld` links MIPS objects into a static executable ([Linking](#linking)).
`cc --help`, `cpp --help`, `as --help`, and
`ld --help` list every option. Options of `cc`, `cpp`, and `as` taking a value
accept it joined or separate (`-oout.s`, `-o out.s`).

## Output

| Option | Output | Default name |
| --- | --- | --- |
| `-S` | Target assembly | `input.s` |
| `-c` | Relocatable object (ELF, COFF, or Mach-O, by target) | `input.o` |
| `-E` | Preprocessed Cross, also accepted by `cc` as `.i` input | `input.i` |
| `-emit-llvm` | LLVM IR text of the compiled program, for debugging and comparison | `input.ll` |
| `-emit-gimple`, `-emit-gimple=rtl` | GCC GIMPLE text, for comparison with GCC's back end | `input.gimple.c` |
| `-fstack-usage` | With `-S` or `-c`, also the stack each function uses, in GCC's format | the output's name with `.su` |

`-o FILE` names the output, and `-o -` writes it to standard output, which an
object file (`-c`) cannot use; files written beside the output, such as those
of `-fstack-usage` and `-MD`, then take the names they have without `-o`.
`-c` assembles the output in process with the assembler of `as`, for every
target and object format. `-save-temps` keeps the assembly beside the object,
named like it with `.s`. `cc` never runs another program: not a C compiler,
LLVM's code generator, an assembler, or a linker.

`-fstack-usage` writes one line per emitted function, in output order:
`FILE:LINE:COLUMN:NAME`, a tab, the bytes, a tab, and `static` or `dynamic`.
`NAME` is the link name. The bytes are the most stack the function itself
uses below its caller's stack pointer: saved registers, locals, and outgoing
arguments, on x86-64 also the return address (as GCC counts), but not the
stack of its callees; a naked function counts what its own instructions
push. `dynamic` marks a function that also allocates variable-length arrays,
whose sizes the bytes do not include.

`cc` does not link: running it without `-S`, `-c`, or `-E` is an error, and
linker options such as `-e`, `-L`, and `-l` are rejected. Link the objects with
`ld` or another linker, and with your own startup code and libraries. Nothing
is added implicitly, and `main` has no special meaning: choose the entry
symbol with the linker.

All inputs of one command form a single compilation group with one output, named
after the first input when `-o` is absent. Definitions without `static` or
`global` are shared within the group but not exported to the linker. Each
input sees the types it declares or includes, and a type that several inputs
define with the same tokens, as through a shared header, is one type. Compile
separately and link only `global` entities to combine objects. A function that
is not `global` is emitted only when an emitted function or object refers to
it, when it has `[[used]]` or `[[retain]]`, or when an alias names it, so a
function inlined into every caller leaves no copy.

The output depends only on the inputs, options, and models, not on the working
directory or the environment. Internal symbols of `static` entities derive from
each input's path as written on the command line, so the same command in a
copy of the tree produces identical files; `-ffile-prefix-map=OLD=NEW` gives
absolute input paths a location-independent spelling. Two separately compiled
files with the same spelling get the same internal names, so a `global`
entity named with `$::meta::gensym` gets the same link name in both objects;
give such files different spellings, for example with `-ffile-prefix-map`.

## Targets, ABIs, and profiles

| Option | Selects |
| --- | --- |
| `-target TRIPLE` | Target architecture, object format, and default ABI. |
| `-march=CPU`, `-mtune=CPU` | A `cpu` model entry ([models.md](models.md#cpu-entries)): `-march` the instruction set, `-mtune` the instruction costs that guide instruction choice and scheduling (default: the `-march` CPU). `-march` also takes `+FEATURE` and `+noFEATURE` modifiers after the CPU, as in `-march=armv8-a+crc+nofp`. |
| `-mFEATURE`, `-mno-FEATURE` | Target features, such as `-mavx2` or `-mno-red-zone`. |
| `-mOPTION=VALUE` | Other target options, such as `-mcmodel=large`. |
| `-mabi=NAME` | ABI for global, unresolved, and indirect calls (normally the Cross ABI, which `-mabi=x` names on every target). |
| `-mmangling=NAME` | Link-name encoding: `cross` (default), `simple`, `itanium`, or `msvc`. `itanium` and `msvc` match C++ names only for scalar and pointer-to-scalar types. |
| `-mprofile=NAME` | A profile: target, ABI, mangling, optimization preset, and options together. |
| `-mdata-bases=NAME[,NAME...]` | `data_base` model entries ([models.md](models.md#data-base-entries)): small objects placed where one register reaches them, such as MIPS small data through `$gp`; `-mdata-bases=none`, the default, selects none. Targets without base-relative data addressing, such as x86-64, reject an entry. |

A `-march` modifier enables a feature, as `-mFEATURE` would, or disables
one together with the features that require it, as `-mno-FEATURE` would,
in the order the modifiers appear: `-march=armv8-a+nofp+sve` enables `sve`
and, again, the `fp` and `simd` that it requires. Explicit `-mFEATURE` and
`-mno-FEATURE` options override the CPU's features and the modifiers alike.

Without `-target` or `-mprofile`, `cc` uses the default target configured at
build time; `cc --version` prints it. Explicit options override a profile's
settings, independently of their order. [targets.md](targets.md) describes
each target, profile, and target option.

Profiles, ABIs, link-name encodings, and optimization presets are declarative
models built into `cc`; the readable shipped definitions are in `model/`.
`--model=FILE` loads an additional model file, and `--model-path=DIR` adds a
directory in which to find `--model` names. [models.md](models.md) describes
the model format, which is unversioned and may change before 1.0.

## Optimization

`-O0` (the default), `-Og`, `-O1`, `-O2`, `-O3`, `-Os`, and `-Oz` select
presets; `-O` means `-O1`, and `-O=NAME` selects any loaded preset. A preset
only sets `-f` options. Explicit `-fNAME`, `-fno-NAME`, and `-fNAME=VALUE`
options override the preset regardless of order, so `-O0 -ftree-ccp` and
`-fno-inline-functions -O3` work as written. No preset changes floating-point
semantics, the ABI, or the required instruction set.

`--print-options` lists every option with its value, its origin (default,
preset, profile, or command line), and whether it is `implemented` or only
`partial`. Commonly useful options:

| Option | Effect |
| --- | --- |
| `-ffunction-sections`, `-fdata-sections` | Put each function or object in its own section; a function's jump tables and pooled constants go to a read-only section named after it ([targets.md](targets.md#object-formats)). |
| `-ffast-math` | Relax floating-point rules: finite math only, no signed zeros, `-ffp-contract=fast`. Under a profile that traps floating exceptions ([models.md](models.md#profiles)), it reassociates floating operations only when no exception but divide-by-zero traps, and `-ffp-contract=fast` fuses a multiply and an add only when overflow and underflow do not trap. |
| `-funwind-tables` | Emit unwind tables (CFI or SEH). No unwinder is linked. |
| `-fno-inline-functions` | Disable discretionary inlining; `[[always_inline]]` still applies. |
| `-finline-limit=N` | Inline a call that `[[always_inline]]` does not require only when the callee has at most N operations, counting conditional branches (2N for an `inline` function), until the calls inlined into one function add up to 8N operations from `-O1` to `-O3`, 4N at `-Os`, and 2N otherwise, but at least 32. The presets set 32 at `-O1`, 96 at `-O2`, 256 at `-O3`, 24 at `-Os`, and 8 at `-Oz`. A function that allocates a variable-length array is inlined only when required, and a `[[naked]]` function inlines only `[[raw_inline]]` and `[[always_inline]]` functions. |
| `-fno-ipa-reference-addressable` | Keep objects that no emitted code reads. From `-O1`, an object that is not `global`, `weak`, `used`, `retain`, `noinit`, volatile, in a section, an alias, a weak reference, or the target of one, and that no `$::patch` and no initializer of an emitted object names, is not emitted when code uses its address only to store to it, and those stores are removed. Debugging information still describes the object, without a location. |
| `-fno-ipa-cp-clone` | Do not specialize functions for constant arguments. From `-O3` on x86-64, calls with constant arguments to a function that is not `global` may use a copy of it specialized for those constants: at most four copies per function, each named `NAME.const.I_V` after the function's link name `NAME`, with the index `I` and hexadecimal value `V` of each constant argument, and placed right after the function. |
| `-fpeel-loops`, `-fpeel-limit=N` | Replace a loop whose trip count is known at compile time with one copy of its body per iteration when the copies have at most N operations, so that each copy computes with constant induction values; like GCC's `-fpeel-loops` with `--param max-completely-peeled-insns=N`. A loop qualifies when it has one exit, tested on every iteration, and integer arithmetic on its induction variables decides the trip count. With `-floop-bounds-from-extents`, a loop whose exit test reads memory also qualifies when its passes are bounded by an access to an array of known extent: each copy keeps its exit test, and the last copy leaves the loop. The presets enable it at `-O2` with N = 96 and at `-O3` with N = 192. |
| `-fno-loop-bounds-from-extents` | Do not bound a loop's trip count by its accesses. By default from `-O1`, when every pass of a loop begins by accessing `a[i]`, an element of an array object of known extent (a static or automatic array, not a pointer), with no call, store, volatile access, or operation that may trap before it, and integer arithmetic on the loop's induction variables decides `i`, the loop is assumed to make only the passes that keep `a[i]` inside the array, since an access past an object's extent is undefined; `-fpeel-loops` uses that bound, and `-fpeel-limit` still decides whether the copies are made. This is no relaxation: a program whose loop would make another pass already has undefined behavior. GCC's `-faggressive-loop-optimizations` corresponds to this option together with the no-wrap assumption for signed arithmetic that Cross makes unless `-fwrapv` is given. |
| `-ftree-slsr` | Reduce multiplication by an integer constant to shifts, additions, and subtractions when profitable. For speed, a product on a loop-carried dependency uses the selected tuning CPU's instruction latencies; other products retain the target's operation budget. |
| `-ftree-reassoc` | Reassociate eligible integer expressions. Unsigned recurrences, and signed recurrences under `-fwrapv`, can combine independent terms before updating carried state when the target's latency and register-pressure costs favor it. |
| `-ftree-loop-vectorize` | Vectorize eligible loops when the selected ISA and target cost model support their operations. Integer add, bitwise AND, OR, and XOR reductions over contiguous arrays support run-time bounds and scalar remainders, including signed zero-based unit inductions. Floating sums retain their order unless reassociation is enabled. On x86-64, packed 32-bit multiplication uses SSE2 partial products or the selected ISA's packed multiply instruction. |
| `-fthread-jumps-limit=N` | With `-fthread-jumps`, which the presets enable from `-O2`, a branch that the bounds of integers decide on every path to it is folded, and an edge into a join continues past the branches that the path from it decides, through copies of the path's blocks, when the copies keep at most N operations; each value that code after the path reads counts one more, since it then arrives in a new phi. Bounds come from constants, arithmetic, masks, shifts, and conversions, from the comparisons of the branches on the way, and from the values that phis take on the path. A copied path keeps the loops it starts in, and enters a loop only to leave it at its first test. The presets set 6 at `-O2` and `-O3`, and 0 at `-Os` and `-Oz`, which thread only paths that need no copies; like GCC's `--param max-jump-thread-duplication-stmts`. |
| `-fsize-loop-growth=N` | Under `-Os` and `-Oz`, let a pointer that steps through an array in place of a scaled index add up to N instructions for each instruction it saves in every pass of its loop, counting a register it keeps live across the loop as an instruction. Under these presets a loop tests at its bottom only when the values it starts with decide its first test, which then needs no guard, whatever N. The presets set 2 at `-Os` and 0 at `-Oz`. |
| `-fif-conversion-limit=N`, `-fif-conversion-memory-limit=N` | With `-fif-conversion`, replace a branch with selects, or combine the branches of `&&` and `\|\|`, when the operations that then run unconditionally, and the selects, cost at most N operations; the second limit applies instead when it is larger and the condition compares masked loaded data with a constant or tests the top bit of a loaded unsigned value. The presets set 6 and 12 at `-O2` and 12 and 12 at `-O3`. A preset's limits apply only where the target selects without a branch, which MIPS CPUs without conditional moves cannot do; a limit that a profile or the command line sets applies everywhere. With `-ftree-loop-vectorize`, an innermost loop whose branches the limits keep is converted with both limits multiplied by the lanes of a reduction when the converted loop then vectorizes, since its packed selects run once per vector of elements. Under `-ftree-reassoc`, which the presets enable from `-O2`, `&&` and `\|\|` of two comparisons of one integer with constants become one unsigned comparison of the integer's offset when the second comparison needs no other work, whatever the limit and also without `-fif-conversion`. |
| `-fno-shrink-wrap` | Set up the stack frame and save preserved registers at function entry, not only on the paths that need them (the default from `-O1`). |
| `-fno-div-by-constant` | Keep the divide instruction for division and remainder by a constant. From `-Og`, such a divide becomes a multiplication by a fixed-point reciprocal with shifts and adds when the target prices that sequence lower; `-Os` and `-Oz` replace only a divide that a single shift or mask performs. A floating division by a power of two whose reciprocal is a normal number becomes a multiplication by that reciprocal, which rounds and raises exceptions exactly as the division does. |
| `-fno-eval-calls` | Stop evaluating ordinary calls during compilation; required evaluation still happens. |
| `-fwrapv` | Make signed `+`, `-`, `*`, and `<<` wrap modulo 2^N, at run time and in compile-time evaluation alike. Division of the minimum value by -1 and invalid shift counts stay undefined. No preset sets it. |
| `-fbounds-trap` | Check every subscript of an array whose bound is known, fixed or variable-length, and execute the inline `$::trap()` sequence when the index is outside the array; `&a[n]` may still form the address one past the end. No helper is called and no preset sets it. Functions with `[[naked]]` or `[[no_sanitize("bounds")]]` are not checked. |
| `-feval-step-limit=N`, `-feval-depth-limit=N`, `-feval-memory-limit=N`, `-feval-byte-limit=N` | Limits for compile-time evaluation and macro expansion. |
| `-feval-opportunistic-step-limit=N` | Evaluated instructions after which compile-time evaluation that nothing requires, such as of a call with constant arguments, gives up and leaves the code to run time (default 100000). |
| `-fgeneric-instance-limit=N`, `-fgeneric-depth-limit=N` | Limits for the instances of generic functions, records, and unions per compilation (default 4096) and for nested instantiation (default 128). |
| `-fzero-init-in-data` | Put zero-valued static objects with the initialized data instead of zero-filled storage such as `.bss`, so that an image patcher can change them; reads of such an object keep reading memory even when it is `const`. |
| `-ffile-prefix-map=OLD=NEW` | Spell source paths that start with `OLD` as `NEW` in `$::source::file`, internal symbol names, and debugging information; line markers, dependency files, and diagnostics keep the real paths. Repeatable; the last matching mapping applies. |

Options that have no meaning without a hosted runtime, such as `-fno-builtin`,
`-nostdlib`, or `-Ofast`, are rejected rather than ignored.

## Debugging information

| Option | Effect |
| --- | --- |
| `-g` | Emit the debugging information of the profile's `debug` model entry, by default the shipped `dwarf`. |
| `-g=NAME` | Emit that of the loaded `debug` entry `NAME`, such as the shipped `dwarf-lines`. |
| `-g0` | Emit none (the default). |

The last of these options counts. The shipped `dwarf` entry emits DWARF 5:
line tables, call-frame information in `.debug_frame` (not loaded; with
`-funwind-tables` the frames are also in `.eh_frame`), and the compile unit
with its namespaces, functions, the calls inlined into them, variables, and
types. `dwarf-lines` emits only the line tables, the inlined calls, and the
call-frame information, and [models.md](models.md#debug-entries) describes
entries of your own.

From `-O1`, a function that never returns does not save the preserved
registers it changes (`-fno-elide-noreturn-saves` keeps the saves); its
call-frame information marks them undefined, so a debugger reports their
values in the calling frames as not saved.

Debuggers read the compile unit as C++, so qualified names work as in
`break app::run`; generic instances and the specialized copies of a function
carry the function's own name beside their link names. `-g` never changes the
generated code. Paths in the debugging information are the inputs as written,
after `-ffile-prefix-map`, and the compilation directory is `.`, so the
output does not depend on the working directory. `-g` is an error with
`-emit-llvm` and `-emit-gimple`. [targets.md](targets.md) states what each
object format and target describes.

## Warnings

`-WNAME` enables a warning and `-Wno-NAME` disables it. Warnings never change
the output. The groups `-Wall`, `-Wextra`, and `-Wunused` enable several
warnings at once, as in GCC; a warning that the command line names itself
keeps that setting whatever groups it also enables, wherever they appear, so
`-Wno-unused-parameter -Wall -Wextra` leaves parameters unchecked.

| Warning | Enabled by | Reports |
| --- | --- | --- |
| `-Waddress-of-packed-member` | default | Taking the address of a packed member that may be under-aligned. |
| `-Wcpp` | default | `#warning` directives. |
| `-Wformat` | `-Wall` | Calls whose arguments do not match their format string, as checked by `$::format::printf` and `$::format::scanf` ([language.md](language.md#abis-and-interoperability)) or by any call checker declared for this warning ([metaprogramming.md](metaprogramming.md#call-checkers)). |
| `-Wunused-variable` | `-Wunused`, which `-Wall` enables | A local variable that nothing uses, and a `static` object outside functions that nothing uses; a `const` one only in the input file itself, not in a file it includes. |
| `-Wunused-function` | `-Wunused` | A `static` function definition that nothing uses. Inline, translation-only, and generic functions do not count. |
| `-Wunused-parameter` | `-Wunused` with `-Wextra` | A parameter of a function definition that the body does not use. Unnamed parameters, the parameter of a macro, syntax expander, or call checker, and those of `[[naked]]` functions do not count. |
| `-Wimplicit-fallthrough` | `-Wextra` | A statement that can complete normally before a `case` or `default` label of the same `switch`. |
| `-Wsign-compare` | `-Wextra` | A comparison that converts a signed operand to an unsigned type, unless the operand is known to be nonnegative. |
| `-Wconversion` | none | An implicit integer conversion that may change a value: in an initializer, an assignment, an argument, or a `return`. |
| `-Wsign-conversion` | `-Wconversion` | An implicit integer conversion that may change the sign of a value. |
| `-Wfloat-conversion` | `-Wconversion` | An implicit conversion from a floating type to an integer or to a narrower floating type. |

`[[maybe_unused]]` on a variable, parameter, or function and
`[[fallthrough]];` before a label mark the intended cases
([language.md](language.md#other-attributes)). `-Wimplicit-fallthrough`
recognizes only the attribute, as GCC's `-Wimplicit-fallthrough=5`; comments
never matter. Conversions of constants are reported only when they change the
value: `u8 x = 200;` is quiet and `u8 x = 300;` is not. Compound assignments
and the entries of brace initializers are not checked for conversions.

Each warning ends with the option that controls it, as in `warning: unused
variable 'count' [-Wunused-variable]`. `-Werror` makes every warning an
error, `-Werror=NAME` makes one warning, or the warnings of a group, errors
and enables them, and `-Wno-error=NAME` keeps a warning a warning under
`-Werror`. `-w` reports no warnings. A profile may set warnings, such as
`w.all = true` ([models.md](models.md#option-properties)), and
`--print-options=warning` lists every warning option with its value and the
groups that enable it.

An unknown `-WNAME` is an error. As in GCC, an unknown `-Wno-NAME` is accepted
and noted only when the compilation reports a diagnostic that it may have been
meant to silence, so a build can pass the same `-Wno-` options to Cross and to
C compilers.

## Preprocessing

`-DNAME[=VALUE]`, `-UNAME`, `-I DIR`, `-isystem DIR`, `-include FILE`, and
`-imacros FILE` work as in C compilers. Quoted includes search the including
file's directory, then `-I`, then `-isystem`; angle includes search only `-I`
and `-isystem`. There are no implicit include directories. `-include` and
`-imacros` files are looked up relative to the current directory first and
apply to every input; all `-imacros` files are processed before the `-include`
files. `-M`, `-MM`, `-MD`, `-MMD`, `-MF`, `-MT`, and `-MQ` write Make
dependencies, including files read by `$::embed`; `-MF -` writes them to
standard output. `cpp` writes one rule per input and `cc` one rule for its
output, whose prerequisites are the files of every input. `-MP` adds an
empty rule for each prerequisite other than an input, so that Make does not
fail when a header is deleted.

## Assembling

`as` reads the assembly `cc -S` emits, and writes one relocatable object from
all its inputs (`-` reads standard input). The target selects the syntax and
object format: GNU-syntax MIPS into ELF, AT&T-syntax x86-64 into ELF,
COFF/PE, or Mach-O as the triple names (`x86_64-unknown-linux-gnu`,
`x86_64-w64-windows-gnu`, `x86_64-apple-darwin`), or GNU-syntax A64 into
little-endian ELF64 (`aarch64-unknown-linux-gnu`, `aarch64-none-elf`).

| Option | Selects |
| --- | --- |
| `-o FILE` | The object file (default `a.out`). |
| `-target TRIPLE`, `-mprofile=NAME` | The target and its defaults, as for `cc`. |
| `-march=CPU`, `-mips1` ... `-mips64r6` | The CPU or ISA level whose instructions are accepted, until a directive changes it: MIPS `.set`, or AArch64 `.arch`, `.cpu`, and `.arch_extension`. AArch64 levels and CPUs take `+FEATURE` and `+noFEATURE`, as `-march=armv8-a+crc`. |
| `-mabi=NAME`, `-32`, `-64` | The ABI, which fixes the ELF class and ABI flags: `-mabi=32` and `-32` select o32, `-mabi=64` and `-64` n64 (also switching the triple to `mips64`), `-mabi=eabi` EABI32. An ABI without a fixed address width, such as `x64`, takes the ELF class of the triple. |
| `-EB`, `-EL` | Big- or little-endian output. |
| `-mFEATURE`, `-mno-FEATURE`, `-mnan=2008\|legacy` | Target features: for MIPS, such as `-msoft-float`, `-msingle-float`, `-mfp32`, `-mfpxx`, and `-mfp64`, which set the floating-point ABI that `.MIPS.abiflags` records, and the errata workarounds `-mfix-r5900` and `-mfix-loongson3-llsc`, which the `-march` CPU may enable ([targets.md](targets.md#mips)); for AArch64, the extensions whose instructions are accepted, such as `-mlse` and `-mno-fp`. |
| `-mgp32`, `-mgp64` | MIPS: the width of the general registers that `.MIPS.abiflags` records and the macros assume, as `.module gp=32` or `gp=64` sets it; by default the narrower of the ISA's and the ABI's registers, 32 bits for o32. |
| `-KPIC`, `-call_shared`, `-mabicalls`, `-call_nonpic`, `-non_shared` | MIPS PIC modes, of which the last applies: SVR4 PIC code as after `.abicalls` (the first three), abicalls code without PIC as after `.abicalls` and `.option pic0`, or neither (the default). With `-mno-shared`, `.cpload` takes `$gp` from `__gnu_local_gp`. |
| `-O0`, `-O1`, `-O2` | MIPS `.set reorder` code: keep every hazard `nop` the CPU might need, keep only the needed ones, or also fill delay slots (the default; `-O` is `-O1`). |
| `-I DIR` | A further directory to search for `.include` and `.incbin` files. |
| `--fatal-warnings`, `-W` | Treat warnings as errors, or suppress them. |

Without `-target` or `-mprofile`, `as` uses the default target of `cc`. Accepted
directives include sections and their flags and COMDAT groups (`.section`,
`.text`, `.data`, `.bss`, `.rdata`, `.sdata`, `.sbss`, `.pushsection`,
`.popsection`, `.previous`), subsections (`.subsection N`, `.text N` and the
other section directives with a number, `.pushsection NAME, N`), symbols
(`.globl`, `.local`, `.weak`, `.hidden`, `.internal`, `.protected`, `.type`,
`.size`, `.set` and `=`, `.equ`, `.equiv`, `.comm`, `.lcomm`), data (`.byte`,
`.2byte`, `.half`, `.hword`, `.short`, `.4byte`, `.word`, `.long`, `.8byte`,
`.dword`, `.quad`, `.uleb128`, `.sleb128`, `.ascii`, `.asciz`, `.string`,
`.float`, `.double`, `.zero`, `.space`, `.fill`, `.incbin`), layout (`.p2align`,
`.balign`, `.align`, `.org`), source structure (`.include`, `.macro`,
`.altmacro` and `.noaltmacro`, `.rept`, `.if` and its variants, `.error`),
and DWARF (`.file`, `.loc`, and the `.cfi_*` directives, which produce
`.debug_line`, `.debug_frame`, and `.eh_frame`). A subsection's contents
follow those of the lower-numbered subsections of its section, and `.org`
counts from the section's start. Data, fill counts, `.org`, symbol
assignments, and instruction immediates may use label differences that only
layout settles. A quoted macro argument passes its text without the quotes;
in `.altmacro` mode, an argument `%expr` passes the expression's decimal
value, `<text>` passes text, parameters may be named without a backslash, and
`LOCAL` names labels unique to an expansion.

For MIPS, `as` reads the GNU as dialect of GCC output and hand-written
assembly and writes the object GNU as writes for embedded MIPS ELF. The MIPS
directives are `.set` (`push`, `pop`, `reorder`, `noreorder`, `at`, `noat`,
`macro`, `nomacro`, `move` and `volatile`, `mips0` ... `mips64r6`,
`arch=CPU`, `gp=32`, `gp=64`, `fp=32`, `fp=xx`, `fp=64`, `oddspreg`,
`nooddspreg`, and the `default` forms), `.module` before any code (`arch=`,
`mipsN`, `gp=`, `fp=`, `oddspreg`, `nooddspreg`, `softfloat`, `hardfloat`,
`singlefloat`, `doublefloat`), `.abicalls`, `.option pic0` and `pic2`, the
PIC directives `.cpload`, `.cprestore`, `.cpsetup`, `.cplocal`, `.cpreturn`,
and `.cpadd`, `.gpword` and `.gpdword`, `.dtprelword`, `.dtpreldword`,
`.tprelword`, and `.tpreldword`, `.reloc` with `R_MIPS_JALR` or
`R_MIPS_NONE` at `.` or a label of the current section, `.ent`, `.end`,
`.frame`, `.mask`, `.fmask`, and `.nan`. As in GNU as, `.set mipsN` selects
the model's CPU of that name, dropping a CPU extension such as Allegrex's,
and also sets 64-bit general and floating-point registers for a 64-bit ISA
and 64-bit floating-point registers for Release 6;
the PIC directives do nothing outside SVR4 PIC code, `.cpload` and
`.cprestore` nothing in ELF64 objects, and `.cpsetup`, `.cplocal`, and
`.cpreturn` nothing in ELF32 ones; and `.gpword` and `.gpdword` give plain
words outside SVR4 PIC code.

The instructions are those of the loaded MIPS instruction table
([models.md](models.md#instruction-entries)), each accepted where the
current ISA and CPU have it, and `cc --print-instructions` lists them. The
shipped table has the integer, multiply and divide, load and store, branch,
jump, FPU, FPU control, CP0, TLB, cache, and `sync` instructions of MIPS
I-V, MIPS32, and MIPS64 Releases 1, 2, and 6, the instructions Release 6
removes rejected there, with multiply-accumulate, `mul`,
`ext`, `ins`, `seb`, `seh`, traps, `pref`, every rounding conversion and FPU
comparison, and the Allegrex instructions (`clz`, `clo`, `max`, `min`,
`bitrev`, `wsbw`, `mfic`, `mtic`, `movn`, `movz`, and its own
multiply-accumulate encodings); coprocessor 2, MIPS16, and microMIPS are not in it, and a model
may add instructions. As in GNU as, a condition code operand, `$fcc0` too,
needs MIPS IV or MIPS32, and a `sync` type or a coprocessor 0 register
select MIPS32 or MIPS64. A floating-point register may be written `$N` in a
load, store, or move (`lwc1 $4, 0($5)`, `mtc1 $2, $6`), where it counts in
no register mask, but not in arithmetic. The relocation operators are
`%hi`, `%lo`, `%gp_rel`, `%higher`, and `%highest`; the GOT and call
operators `%got`, `%call16`, `%got_disp`, `%got_page`, `%got_ofst`,
`%got_hi`, `%got_lo`, `%call_hi`, and `%call_lo`; the TLS operators
`%gottprel`, `%tprel_hi`, `%tprel_lo`, `%tlsgd`, `%tlsldm`, `%dtprel_hi`, and
`%dtprel_lo`; and n64's `%hi(%neg(%gp_rel(SYMBOL)))` and `%lo` form. Labels
in TLS sections are TLS symbols. The shipped macros are
`li` and `dli`, expanded as GNU as expands 32-bit values (a `li` value that
only layout settles becomes one `addiu`, whose immediate must then fit),
`move`, `b`, `bal`, `beqz`, `bnez`, `beqzl`, `bnezl`, `negu`, `neg`, `not`,
`mul` without the MIPS32 instruction (`multu` and `mflo`), shifts and
rotates by a register written with the immediate mnemonic, and
three-register operations and traps whose last operand is a constant: the
immediate instruction when it fits, otherwise the constant loaded into
`$at`. In SVR4 PIC code, as in GNU as, `jal SYMBOL` loads `$t9` from the GOT
(through `%call16`, or in ELF32 objects for a symbol of local binding through
its page entry and `%lo`), calls through `$t9`, and in ELF32 objects reloads
`$gp` from the `.cprestore` slot; `j SYMBOL` becomes a branch.

In `.set reorder` code, `as` separates each pair of instructions with the
`nop`s the CPU needs unless the CPU model has the interlock: after a load or
a coprocessor 0 move (`load-interlocks`), an FPU move
(`fpu-transfer-interlocks`), an FPU compare (`fpu-compare-interlocks`), or a
HI/LO read (`hilo-interlocks`). A branch takes the place of the instruction
before it, which moves into the delay slot, unless that instruction is in a
`.set noreorder` block or has a label, the branch depends on it, or the move
would create a hazard at either end; otherwise the delay slot gets a `nop`.
Branch-likely instructions always get a `nop`. A Release 6 conditional
compact branch gets a `nop` in its forbidden slot before a control transfer,
data, an alignment, or the end of the input, and the instruction in that
slot stays out of a later delay slot. A label before an instruction
that needs hazard `nop`s moves past them, and `.set noreorder` adds the `nop`s
earlier instructions may need, keeping only those the instructions after it
need. With `-mfix-r5900`, a conditional branch back to a label at most five
instructions before it in the same section and fragment, with no branch or
jump between, keeps its `nop`. With `-mfix-loongson3-llsc`, in either reorder
mode, `as` puts a `sync` before every `ll` and `lld` that does not follow
one, and before an instruction at a local label that a branch between an
`ll` and the `sc` after it targets, within the last five instructions.

The objects carry the MIPS ELF header flags, `.MIPS.abiflags`, `.reginfo`
(ELF32) or `.MIPS.options` (ELF64), neither loaded into the image, `.pdr`
with a record for each `.end`, and `.gnu.attributes` with the floating-point
ABI. As in GNU as, `.half`, `.word`, `.dword`, `.float`, and `.double` align
their values and move the labels before them to the aligned address, and so
does `.align`, while `.align 0` turns that alignment off until the next
section change; `.lcomm` aligns by size; sections are as aligned as their
contents and code sections end padded to their alignment; `.sdata`, `.sbss`,
`.lit4`, and `.lit8` sections are gp-relative (`SHF_MIPS_GPREL`); in ELF32
objects every name starting with `$`, such as GCC's `$L` labels, stays out of
the symbol table unless a relocation must name it, as `.L` names do in every
object; the first `.file` gives the file symbol, and `.file` without `.loc`
gives no line table; `.2byte` or `.half` of a symbol is an error in an o32
object, which has no 2-byte data relocation, and `R_MIPS_REL16` in an n64
object; and in a Release 6 object every PC-relative field, a branch's too,
keeps a relocation against its target, also within its section. Unlike GNU
as:

- the hazard `nop`s follow the CPU model rather than GNU as's tables: the
  shipped `vr4300` model has no FPU transfer or compare interlocks, so
  `.set reorder` code gets a `nop` after `mtc1` and before a `bc1t` that reads
  a compare, where GNU as puts none, while the `allegrex` model's FPU transfer
  interlocks, and the load interlocks that cover `mfc0` and `mtc0` on MIPS II
  and MIPS III CPUs, leave out `nop`s that GNU as inserts;
- a `%gp_rel` of a local symbol more than 32 KiB into its section names the
  symbol in an o32 object, where GNU as reports a relocation overflow;
- a `li` value that only layout settles must fit 16 bits, where GNU as keeps
  its low half;
- `ld` and `sd` with 32-bit general registers, which GNU as expands into word
  pairs, are errors; select 64-bit registers with `.set gp=64` or `.set mips3`;
- `.lcomm` and `.comm` objects stay in `.bss` and common storage, where GNU as
  puts those of up to 8 bytes in `.sbss` and small common storage;
- the `div` and `divu` macros, address macros such as `la` and `lw REG,
  symbol`, `.insn`, and `-g` line tables for assembly source are not
  available: write `div $zero, rs, rt` and `%hi`/`%lo` or GOT operator pairs;
- in an ELF32 object, a PIC `jal` to a local symbol defined after it is an
  error, where GNU as gives it the local form;
- `-march` and `.set arch=` accept the CPUs that `cc --print-cpus` lists
  ([targets.md](targets.md#mips)), with the instructions of their ISA level
  and Allegrex's, but not the instructions that other CPUs add, such as the
  Octeon and Loongson ones;
- the Release 6 `ll`, `sc`, `lld`, `scd`, `cache`, and `pref`, whose offsets
  have 9 bits, take no larger offset or symbol, which GNU as adds into a
  register first;
- a constant operand of `lapc`, `lwpc`, `lwupc`, and `ldpc` is an offset
  from the instruction, where GNU as reads an address that a relocation
  reaches, and `addiupc` also takes a label;
- `-mfix-loongson3-llsc` does not add the `sync` that GNU as emits before an
  `sc` when a branch between the `ll` and the `sc` targets a label before the
  `ll`, which would put the `sync` inside the sequence.

`cc -c` assembles its own MIPS output with the same assembler and the object
conventions of LLVM MC: `.text`, `.data`, and `.bss` aligned to 16 bytes, an
allocated `.reginfo`, register masks of every operand, no `.gnu.attributes` or
empty `.pdr`, and data that does not align itself; there `.set mipsN` changes
only the ISA level of the selected CPU.

For an x86-64 target `as` reads AT&T syntax: operand order source then
destination, `%` registers, `$` immediates, `disp(base, index, scale)` memory
with `%rip`-relative and absolute forms, `b`/`w`/`l`/`q` operand-size suffixes,
and `symbol@PLT`, `@GOTPCREL`, `@GOTTPOFF`, `@TPOFF`, `@tlsgd`, `@SECREL32`,
and the other relocation specifiers. It encodes the integer, x87, SSE through
AVX-512 (VEX and EVEX, with `{%k}` masks, `{z}`, `{1toN}` broadcasts, and
`{rn-sae}` rounding), and BMI instructions the backend selects, choosing the
same encodings as LLVM MC where several are equivalent, including two-byte VEX
prefixes, the NOP forms that pad alignment, and the short or near branch a
layout pass selects. Short branches whose targets move out of reach relax to
their near forms. Objects carry `.cfi_*` tables (`.eh_frame` and
`.debug_frame`), `.loc`/`.file` line tables, and the relocations each format
needs; Win64 `.seh_*` directives produce `.pdata` and `.xdata`, and Mach-O
objects honor `.subsections_via_symbols`. Only 64-bit AT&T syntax is
accepted: Intel syntax, `.code16` and `.code32`, the APX and AVX10 encodings,
and `.reloc` are rejected, and an unknown mnemonic is an error.

For an AArch64 target `as` reads the A64 syntax of GNU as and LLVM MC: `//`
comments, and `#` comments only at the start of a line; `#` before an
immediate, which may be left out; registers in either case, with `fp` and
`lr` for `x29` and `x30`; memory operands `[base, offset]`, `[base, offset]!`,
`[base], offset`, and `[base, index, extend #amount]`; and the relocation
operators, written before a symbol as in `adrp x0, :got:name` and `ldr x0,
[x0, :got_lo12:name]`: `:lo12:`, `:pg_hi21:`, `:pg_hi21_nc:`, `:got:`,
`:got_lo12:`, the TLS operators (`:tprel_hi12:`, `:tprel_lo12:`,
`:tprel_lo12_nc:`, `:tprel_g0:` to `:tprel_g2:`, `:gottprel:`,
`:gottprel_lo12:`, `:gottprel_g1:`, `:gottprel_g0_nc:`, `:dtprel_*:`,
`:tlsdesc:`, `:tlsdesc_lo12:`, `:tlsgd:`, and `:tlsgd_lo12:`), and the move
operators `:abs_g0:` to `:abs_g3:` with their `_nc` and `_s` forms
([models.md](models.md#aarch64-instructions)). The instructions are those of
the loaded AArch64 instruction table, each accepted where the current
features have it, and `cc --print-instructions` lists them. The shipped table
has the integer instructions (arithmetic, logical, move wide, bit-field,
extract, shifts, multiply and divide, conditional select and compare, and the
`crc` checksums), the loads and stores of every addressing mode with pairs,
literals, unprivileged, exclusive, and acquire-release forms and the `lse`
atomics, the branches, `adr` and `adrp`, exception generation, hints,
barriers, `mrs` and `msr`, and scalar floating point, with half precision
under `fp16`; and the aliases of GNU as, among them `mov`, `cmp`, `cmn`,
`tst`, `neg`, `mvn`, `lsl`, `lsr`, `asr`, and `ror` by an immediate, `mul`,
`sxtb` to `uxth`, `cset`, `cinc`, and the bit-field extracts and inserts.
`.arch NAME`, `.cpu NAME` (each with `+FEATURE` and `+noFEATURE`), and
`.arch_extension [no]FEATURE` change the features; `.inst` emits instruction
words and `.xword` 8-byte data. A64 instructions start on a word: one that
follows data in its section is preceded by zeros up to the next word.

The objects are little-endian ELF64 with RELA relocations. Code sections are
aligned to 4 bytes and padded with `nop`s; `$x` and `$d` mapping symbols
mark the instructions and the data of sections that hold code, but not the
data of sections without code, as in GNU as. As in LLVM MC, relocations of
GOT and TLS operators name their symbol, also a local one, and a `movz` whose
signed TLS move relocation (`:tprel_g2:`, `:tprel_g1:`, `:tprel_g0:`,
`:dtprel_g2:` to `:dtprel_g0:`, `:gottprel_g1:`) chooses `movz` or `movn` is
written as a `movn`. Unlike GNU as and LLVM MC, `as` takes `:lo12:` and the
unsigned `:abs_g` operators of constants, and `movz` immediates that only
layout settles; `prfm` with an unscaled offset becomes `prfum` as in GNU as.
Advanced SIMD, SVE, pointer authentication, `sys` and its aliases (`dc`,
`ic`, `tlbi`, `at`), `ldr REG, =value` and literal pools (`.ltorg`, `.pool`),
`.req`, `.tlsdesccall`, `.variant_pcs`, and big-endian and ILP32 objects are
not available.

## Linking

`ld` links ELF relocatable objects, archives, raw binary files, and the
symbol addresses of other images into one static ELF executable, laid out by
a GNU ld linker script. It links MIPS objects of either class and byte order
(o32, n64, and EABI32, from `cc -c`, `as`, GNU as, GCC, and Clang); objects of
other machines are rejected. It accepts GNU ld's spelling of the options
below, and runs no other program.

| Option | Selects |
| --- | --- |
| `-o FILE` | The executable (default `a.out`). |
| `-T FILE`, `--script=FILE` | The linker script. Files its `INPUT` and `GROUP` commands name are linked where `-T` stands among the inputs. |
| `-e SYMBOL`, `--entry=SYMBOL` | The entry point, a symbol or an address. Without it, `ENTRY` in the script, then `_start`, then the start of `.text` (with a warning). |
| `-L DIR`, `-l NAME` | `-l NAME` links `libNAME.a` (`-l:NAME` links `NAME`) from the `-L` directories and the script's `SEARCH_DIR`s. |
| `-R FILE`, `--just-symbols=FILE` | The addresses of the global symbols that the ELF file FILE defines, without its contents. |
| `-b binary`, `--format=binary` | Read the following inputs as raw data, each as a writable `.data` section with the symbols `_binary_NAME_start`, `_binary_NAME_end`, and `_binary_NAME_size`, where NAME is the path as written with each character other than a letter or digit replaced by `_`. `-b elf32-bigmips` (any ELF format) or `--format=default` ends raw input. |
| `-u SYMBOL` | Require SYMBOL, linking an archive member that defines it. |
| `--defsym SYMBOL=EXPRESSION` | Define SYMBOL with a script expression. |
| `-Ttext=ADDRESS`, `-Tdata=ADDRESS`, `-Tbss=ADDRESS`, `--section-start=NAME=ADDRESS` | Place an output section at ADDRESS, read as hexadecimal. |
| `-r`, `--relocatable` | Write a relocatable object instead of an executable. |
| `-d`, `-dc`, `-dp` | Allocate common symbols with `-r` too. |
| `--gc-sections`, `--print-gc-sections` | Remove the sections that nothing reaches, and report each one removed. With `-r`, `-e` or `-u` must name a defined symbol, or `--gc-keep-exported` must be given. |
| `--gc-keep-exported` | Keep the sections that define global symbols of default or protected visibility. |
| `--eh-frame-hdr`, `--no-eh-frame-hdr` | Write, or do not write (the default), an `.eh_frame_hdr` search table and its `PT_GNU_EH_FRAME` header. |
| `-Map FILE`, `-M` | Write a link map to FILE, or to standard output. |
| `-m EMULATION`, `--oformat=FORMAT`, `-EB`, `-EL` | The output class and byte order instead of the first object's: the emulations `elf32btsmip`, `elf32ltsmip`, `elf32ebmip`, `elf32elmip`, `elf64btsmip`, and `elf64ltsmip`, or the formats `elf32-bigmips`, `elf32-littlemips`, `elf32-tradbigmips`, `elf32-tradlittlemips`, and their `elf64-` counterparts. |
| `-z max-page-size=SIZE` | The alignment of loadable segments in the file: by default 64 KiB for ELF64 and for the `ts` emulations and `trad` formats, otherwise 4 KiB. |
| `-s`, `--strip-all` | Omit the symbol table. |
| `--fatal-warnings` | Treat warnings as errors. |
| `--start-group`, `--end-group` | Accepted; archives are searched in any order. |
| `-nostdlib` | Accepted; there are no default library directories. |

**Symbols.** A symbol has at most one strong definition. A strong definition
replaces weak and common ones, a common symbol replaces a weak definition,
and common symbols of one name merge to the largest size and alignment; they
are allocated in an input section named `COMMON` (MIPS small commons in
`.scommon`) of the file that defines them. An undefined weak symbol is zero.
Of several COMDAT groups with one signature, the first is linked. An archive
member is linked when it defines a symbol that a strong reference needs,
wherever the archive and the reference appear on the command line; the first
archive on the command line that defines the symbol provides it, and weak
references link no member. A linker-script assignment defines its symbol
even when an object also defines it.

**Linker scripts.** The accepted commands are `SECTIONS`, `MEMORY` (with
region attributes `R`, `W`, `X`, `A`, `I`, `L`, and `!`), `PHDRS`, `ENTRY`,
`INCLUDE` (searched in the current directory, the `-L` and `SEARCH_DIR`
directories, and the including script's directory; also inside output
section descriptions), `INPUT` and `GROUP` (files, `-lNAME` libraries, and
`AS_NEEDED` lists, searched in the current directory and the library
directories), `SEARCH_DIR`, `EXTERN`, `INSERT AFTER` and `INSERT BEFORE`,
`REGION_ALIAS`, `NOCROSSREFS`, `NOCROSSREFS_TO`, `ASSERT`, `OUTPUT_ARCH`,
`OUTPUT_FORMAT` (both ignored), and assignments, also inside `PROVIDE`,
`HIDDEN`, and `PROVIDE_HIDDEN`. An input file that is neither an object nor
an archive is an implicit linker script, which adds to the main one; files
its `INPUT` names are searched in its own directory first. A script with
`INSERT` adds to the default layout instead of replacing it: the statements
before each `INSERT` move after (or before) the named output section.

An output section description may give an address, a type (`NOLOAD`;
`DSECT`, `COPY`, `INFO`, or `OVERLAY` for a section without memory;
`READONLY`; `TYPE = SHT_PROGBITS` and the other GNU ld type names or a
number), `AT(...)`, `ALIGN(...)`, `ALIGN_WITH_INPUT`, `SUBALIGN(...)`,
`ONLY_IF_RO` or `ONLY_IF_RW`, `>REGION`, `AT>REGION`, `:PHDR`, and `=FILL`;
its contents are input section descriptions, `BYTE`, `SHORT`, `LONG`, `QUAD`,
`SQUAD`, `FILL(...)`, `CONSTRUCTORS` and `LINKER_VERSION` (which place
nothing), assignments to
symbols and to `.`, and `ASSERT`. `OVERLAY` gives its sections one address
and consecutive load addresses, defines `__load_start_NAME` and
`__load_stop_NAME` for each (NAME being the section name without characters
other than letters, digits, and `_`), and moves `.` past the largest. An input
section description is `FILE(SECTION ...)` with the wildcards `*`, `?`, and
`[...]`, optionally in `KEEP(...)`, with `EXCLUDE_FILE(...)`, `SORT`,
`SORT_BY_NAME`, `SORT_BY_ALIGNMENT`, `SORT_BY_INIT_PRIORITY`, `SORT_NONE`, and
`ARCHIVE:MEMBER` file patterns; a file pattern matches the path as written,
and an archive member matches its archive's path. `COMMON` names the common
symbols and `/DISCARD/` drops what it matches. Expressions have C's operators
and the functions `ABSOLUTE`, `ADDR`, `ALIGN`, `ALIGNOF`, `BLOCK`, `CONSTANT`,
`DATA_SEGMENT_ALIGN`, `DATA_SEGMENT_END`, `DATA_SEGMENT_RELRO_END`, `DEFINED`,
`LENGTH`, `LOADADDR`, `LOG2CEIL`, `MAX`, `MIN`, `NEXT`, `ORIGIN`,
`SEGMENT_START`, `SIZEOF`, and `SIZEOF_HEADERS`; `NEXT_SECTION` names the next
output section. Other commands, such as `STARTUP`, `OUTPUT`, `TARGET`,
`INPUT_SECTION_FLAGS`, `CREATE_OBJECT_SYMBOLS`, and the `SPECIAL` constraint,
are diagnosed.

**Layout.** Addresses, load addresses, empty sections, and the location
counter follow GNU ld. An output section that is empty before `.eh_frame`
and `.pdr` editing is left out unless a `KEEP` input section or an
assignment to `.` keeps it (`. = 0`, `. = . + 0`, and
`. = ALIGN(. != 0 ? EXPRESSION : 1)` do not); a left-out section's address
still counts when it defines symbols, which move to a nearby section. A
section of assignments alone becomes zero-filled space once `.` advances in
it and is otherwise not allocated. An address given to a section sets `.`
even when the section is left out; a section without an address follows the
location counter, or its region with `>REGION`, at its alignment. When
`MEMORY` defines regions, an allocated section without `>REGION` goes to the
first region whose attributes match its contents (read-only data counts as
both `R` and `W`, as in GNU ld), and one that no region takes is an error;
an `AT>REGION` without `>REGION` or an address also gives the run-time
region; a section that names an undeclared region gets a warning and a
region at address 0. A section without `AT` or `AT>` keeps the difference
between load and virtual address of the last section of its region that had
one, a later section of an `OVERLAY` loads after the previous one, and a
section without them that follows a section with `AT>` in the same region
also loads in that region.

Expression values are plain numbers, absolute addresses, or addresses
relative to an output section, as in GNU ld. Inside an output section, a
plain number assigned to a symbol or to `.` is an offset from the section's
start, and an absolute symbol counts as a plain number. An operator on a
relative value and a plain number works on the offset in the section and
keeps the section, so inside a section `. != 0` tells whether it has
contents so far and `. = MAX(., 16)` makes it at least 16 bytes long; values
of two different sections are made absolute first; arithmetic on two values
of one section, or on two absolute values, gives a plain number inside
output sections and an absolute value outside them; comparisons give plain
numbers. `ALIGN(EXPRESSION, ALIGNMENT)`, `MAX`, and `MIN` keep the section
of a relative result. A symbol assigned from `.` outside output sections
belongs to the previous allocated section, or after an assignment to `.` to
the next one (but past `_end` to the previous). A non-allocated script
section without an address gets the location counter as its address and
moves `.` past its end, as in GNU ld, so debugging sections need the
address 0. `ONLY_IF_RO` and `ONLY_IF_RW`
sections whose input sections are not all read-only (or all writable) are
dropped, leaving their input sections to later statements. `SUBALIGN`
replaces the alignment of the section and of its input sections. A fill
pattern fills each gap from its start: a hexadecimal literal gives all its
digits as the pattern, any other value its low four bytes, big-endian.
`DATA_SEGMENT_ALIGN(MAXPAGESIZE, COMMONPAGESIZE)` gives the next page at the
same offset, or the form that saves a common page when the data's partial
first and last pages fit in one; `DATA_SEGMENT_RELRO_END` gives its second
operand. An input section that no rule matches keeps its name and follows the
last output section of its kind (code, read-only data, data, or zero-filled
data), or of an earlier kind, in that section's regions; non-allocated ones
follow `.comment` (or come last) at address 0, and `COMMON` goes to `.bss`.
Without `-T` the layout is a subset of GNU ld's default for MIPS: the ABI
sections and code at 0x400000 (0x120000000 for ELF64), `.eh_frame_hdr`,
`.eh_frame`, and `.gcc_except_table` after the read-only data (or, when
writable, before the data), the data placed by `DATA_SEGMENT_ALIGN`, `_gp`
32 KiB past the start of the small data, `_etext`, `_edata`, `__bss_start`,
and `_end`, then `.comment`, the debugging sections, and `.gnu.attributes`
at address 0.

**Segments.** Without `PHDRS`, allocated sections share a loadable segment
unless a section's load and virtual addresses differ by another amount, it
overlaps the previous section or starts more than a page after its end, it
holds data after a zero-filled section, or it is writable after read-only
sections and on another page. With `PHDRS`, a section without `:PHDR` takes
the program headers of the previous section unless it is `NOLOAD`. The file
and program headers join the first loadable segment when there is room for
them below its first section in that page, without `PHDRS` or when the
segment's entry has `FILEHDR` or `PHDRS`. A loadable segment is aligned to
the page size or to its most aligned section; one without file contents gets
an offset within the first page. A `.MIPS.abiflags` or `.reginfo` output
section also gets a `PT_MIPS_ABIFLAGS` or `PT_MIPS_REGINFO` header, and the
output section holding `.eh_frame_hdr` a `PT_GNU_EH_FRAME` header. Once an
input has a `.note.GNU-stack` section, a `PT_GNU_STACK` header tells whether
the stack is executable: it is when an input's note asks for it or an input
has no note.

**Unwind tables.** The input sections of the `.eh_frame` output section are
edited as GNU ld edits them: an FDE whose function is in a removed section
(a duplicate COMDAT copy, a `/DISCARD/` match, or a section `--gc-sections`
collects) or whose address range is empty is dropped, CIEs that no remaining
FDE uses go, identical CIEs (equal but for their personality pointer's bytes,
with the same personality symbol) share the first copy, trailing
`DW_CFA_nop` padding is trimmed, entries realign (FDEs to their address
size), and only the last input section keeps a zero terminator. Live code
keeps the LSDAs its FDEs refer to and the personality routines of their
CIEs. With `--eh-frame-hdr`, `.eh_frame_hdr` is an input section of the first
input with unwind entries, placed by `*(.eh_frame_hdr)` or as an orphan, with
a hidden local symbol `__GNU_EH_FRAME_HDR`; its table lists every FDE's
initial location and address relative to the header, sorted, and FDEs that
overlap are an error. An `.eh_frame` section the linker cannot read stays
whole, with a warning, and the header then omits its table. `-r` edits
unwind tables the same way, without sharing CIEs.

**MIPS.** `ld` applies `R_MIPS_16`, `_32`, `_64`, `_26`, `_HI16`, `_LO16`,
`_GPREL16`, `_GPREL32`, `_LITERAL`, `_PC16`, `_PC32`, `_HIGHER`, and
`_HIGHEST`, the n64 combinations with `R_MIPS_64` and with `R_MIPS_SUB` and
`_HI16` or `_LO16`, the GOT relocations `R_MIPS_GOT16`, `_CALL16`,
`_GOT_DISP`, `_GOT_PAGE`, `_GOT_OFST`, `_GOT_HI16`, `_GOT_LO16`,
`_CALL_HI16`, and `_CALL_LO16`, and the TLS relocations `R_MIPS_TLS_GD`,
`_TLS_LDM`, `_TLS_GOTTPREL`, `_TLS_TPREL_HI16`, `_TLS_TPREL_LO16`,
`_TLS_DTPREL_HI16`, `_TLS_DTPREL_LO16`, `_TLS_TPREL32`, `_TLS_TPREL64`,
`_TLS_DTPREL32`, and `_TLS_DTPREL64`; other TLS, MIPS16, and microMIPS
relocations are diagnosed. As in GNU ld, a `jalr $t9` with an `R_MIPS_JALR`
hint becomes a `bal` when the hint names a function that the link defines
within the branch's reach. A REL `R_MIPS_HI16` takes the low half
of its addend from the next `R_MIPS_LO16` of the same symbol, so several may
share one; one without such a `R_MIPS_LO16` gets a warning. `R_MIPS_26`
requires the target in the 256 MiB region of the delay slot. In ELF32 output
every address is a 32-bit value, so symbol addresses read with
`--just-symbols` and script addresses compare as such. gp-relative
relocations need a definition of `_gp`, and those against local symbols add
the gp value an object's `.reginfo` or `.MIPS.options` records.
`_gp_disp`, which needs no definition, is `_gp` less the address of the
field of its `R_MIPS_HI16`, which an `R_MIPS_LO16` four bytes later
completes, and `__gnu_local_gp` is `_gp`. GOT
relocations add a `.got` input section to the first object that has one,
which `_GLOBAL_OFFSET_TABLE_`, a hidden symbol, starts. As in GNU ld's static
executables, the GOT has two reserved entries, the second with only its top
bit set, then the page and address entries of symbols of local binding, the
entries of other symbols, and the TLS entries: initial-exec entries hold
offsets from the thread pointer, global-dynamic pairs module 1 and the
offset from the DTP, and the local-dynamic pair module 1. Unlike GNU ld,
`ld` orders the TLS entries by first use. The TLS sections form a `PT_TLS`
segment and must be adjacent; the first takes the largest alignment among
them, `.tbss` takes no address space, TLS symbols hold offsets in the
segment, and the thread pointer and the DTP lie 0x7000 and 0x8000 past its
start. `R_MIPS_16`
fills the 16-bit field at its offset, where GNU ld fills the low half of the
32-bit word there. The header flags of the objects must agree on the ABI, the
NaN encoding, and FP64, and their ISAs must extend one another; the output
takes the newest ISA. The output's `.reginfo` is the first object's, as in
GNU ld, with the final `_gp`; `.MIPS.options` merge into one register-usage
option with the union of the masks and the final `_gp`; `.gnu.attributes`
merge as in GNU ld: the first
object's attributes, with the floating-point and MSA ABIs of all objects
merged (an object without a floating-point attribute contributes the one in
its `.MIPS.abiflags`; incompatible ones get a warning) and
`Tag_compatibility` agreeing; and `.MIPS.abiflags` into the largest ISA and
register sizes with the merged floating-point ABI. A `.pdr` record whose
function a file does not contribute, such as a duplicate COMDAT copy, is
dropped. Output sections take GNU ld's MIPS types and flags by name:
`.reginfo`, `.MIPS.options`, `.MIPS.abiflags`, `.mdebug`, and the debugging
sections their MIPS section types; `.sdata`, `.sbss`, `.srdata`, `.lit4`,
`.lit8`, and `.got` the gp-relative flag; and `.MIPS.options`, and in the
IRIX-compatible formats (all but the `ts` emulations and `trad` formats)
`.debug_frame`, the no-strip flag. In the IRIX-compatible formats
`.reginfo` has an entry size of 1, and in their ELF32 forms the symbol
table's `sh_info` counts only the section symbols.

**Other rules.** Relocation-free `SHF_MERGE` sections of one output section
are merged as GNU ld merges them: one copy of each string or constant, and
strings that end other strings share their tails. `--gc-sections` keeps the
entry, `-u`, `EXTERN`, and script symbols, `KEEP` sections, sections whose
symbols those reach through relocations, COMDAT groups as wholes, note, init
and fini array sections, `.ctors`, `.dtors`, OS- and processor-specific
sections, and `.eh_frame`; of a file with a live allocated section, it keeps
the debugging sections and the non-allocated sections without relocations
(so MIPS `.pdr` goes), and of other files none. An undefined symbol that no
remaining relocation refers to leaves the symbol table. The symbol table
starts with a symbol for each output section, then each file's local
symbols (of the files that place a section in an output section with
contents, including those of an `.eh_frame` input section that editing
emptied) after a FILE symbol, named after the file when it has none, then
the symbols a script defines with `HIDDEN` or `PROVIDE_HIDDEN`, made local
after an unnamed FILE symbol, then the global symbols, with the binding and
visibility objects give them; assembler-local labels in mergeable sections
and undefined weak symbols are left out. The map lists discarded input
sections, then each output section (also those left out of the output) with
its input sections, the files they come from, their symbols, and the
script's assignments, in a format like GNU ld's, with addresses of the
output's width. With `-r`, output sections follow
GNU ld's relocatable layout for the target (`.text`, `.rodata`, `.data`,
`.sdata`, `.sbss`, `.bss`, and the like collect input sections of exactly
that name, in that order), then other input sections of one name share an
output section in the order they first appear, except that the members of a
section group keep their own and the group stays; relocations remain,
against output section symbols with their addends adjusted, and common
symbols stay common unless `-d` makes them `.bss` (MIPS small commons
`.scommon`) space; `-r` takes `--defsym` but no linker script.

## Inspecting the compiler

These options print what the selected target and models provide. Combine them
with `-target`, `-march`, or `-mprofile` to inspect another configuration.

| Option | Lists |
| --- | --- |
| `--print-targets` | Compiled-in architectures and their triples. |
| `--print-profiles`, `--print-abis` (`-mabi=help`), `--print-manglings`, `--print-optimizations`, `--print-models` | Model entries. |
| `--print-cpus` | The target's CPU entries with their parents, features, issue widths, and function alignments where one is set. |
| `--print-costs` | The latency, issue cost, and availability of each of the target's instructions for `-march` and `-mtune`: a `cpu: NAME  tune: NAME  issue_width: N` line, then one `INSTRUCTION  latency: N  issue: N  available: yes` (or `no`) line per instruction. |
| `--print-data-bases` | The `data_base` entries: one `NAME  base: REGISTER  sections: LIST  max_size: N  kinds: LIST  source: FILE:LINE` line each. |
| `--print-init-tables` | The `init_table` entries: one `NAME  section: SECTION  priority_section: PREFIX  abi: ABI  source: FILE:LINE` line each, without the parts an entry does not set and with ` (inverted)` after an inverted prefix. |
| `--print-options[=all\|optimization\|common\|target\|warning]` | Options, values, and origins. |
| `--print-keywords`, `--print-builtins`, `--print-attributes` | The language surface. |
| `--print-features`, `--print-registers` | Target feature queries and register names. |
| `--print-instructions` | The machine-instruction built-ins with their feature gates and whether they are privileged or volatile, then, for a target whose instruction table models give, each instruction form: `MNEMONIC OPERANDS  [FEATURES]` (the features of which the form needs one, then `!NAME` for each it excludes), `alias of NAME` or `macro` where it is one, its built-in or `no built-in (REASON)`, and `source: FILE:LINE`. |

## Diagnostics

Diagnostics have the form `file:line:column: error: message`, followed by the
source line and a caret. Lines and columns are those of the written source,
and a token that a preprocessor macro produces is reported at the macro's
invocation; compiling `cpp` output (`.i`) instead reports columns of the
preprocessed lines. Errors in generated code also show the macro or
syntax expansion chain; `as` reports an error inside a macro at the line that
invokes it, and `ld` an error in a linker script at its position. Errors in a
model file have the form `file:line: error: message`, the same under `cc`,
`cpp`, and `as`, and errors that belong to no file, such as an unknown option,
have the form `cc: error: message`, `cpp: error: message`, `as: error:
message`, or `ld: error: message`. `ld` names the object (`archive(member)`
for an archive member), the section and offset, and the symbol of an error in
the input, as in `ld: error: start.o:(.text+0x8): undefined reference to
'main'`. The tools exit with status 1 after an error and 0 otherwise.

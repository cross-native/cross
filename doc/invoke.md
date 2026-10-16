# Using cc, cpp, and as

```text
cc [options] input.x ...
cpp [options] input.x ...
as [options] input.s ...
```

`cc` preprocesses and compiles Cross source (`.x`) to assembly or object
files. `cpp` only preprocesses, writing Cross to standard output or to `-o`.
`as` assembles hand-written GNU-syntax MIPS assembly into an object file
([Assembling](#assembling)). `cc --help`, `cpp --help`, and `as --help` list
every option. Options taking a value accept it joined or separate
(`-oout.s`, `-o out.s`).

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
For MIPS targets `-c` assembles the output in process with the assembler of
`as`; for other targets it runs `llvm-mc` from `PATH`. `-save-temps` keeps the
assembly beside the object, named like it with `.s`. `cc` never calls a C
compiler, LLVM's code generator, or a linker.

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
your own linker, startup code, and libraries. Nothing is added implicitly, and
`main` has no special meaning: choose the entry symbol with the linker.

All inputs of one command form a single compilation group with one output, named
after the first input when `-o` is absent. Definitions without `static` or
`global` are shared within the group but not exported to the linker. Compile
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
| `-march=CPU`, `-mtune=CPU` | A `cpu` model entry ([models.md](models.md#cpu-entries)): `-march` the instruction set, `-mtune` the instruction costs that guide instruction choice and scheduling (default: the `-march` CPU). |
| `-mFEATURE`, `-mno-FEATURE` | Target features, such as `-mavx2` or `-mno-red-zone`. |
| `-mOPTION=VALUE` | Other target options, such as `-mcmodel=large`. |
| `-mabi=NAME` | ABI for global, unresolved, and indirect calls (normally the Cross ABI). |
| `-mmangling=NAME` | Link-name encoding: `cross` (default), `simple`, `itanium`, or `msvc`. `itanium` and `msvc` match C++ names only for scalar and pointer-to-scalar types. |
| `-mprofile=NAME` | A profile: target, ABI, mangling, optimization preset, and options together. |
| `-mdata-bases=NAME[,NAME...]` | `data_base` model entries ([models.md](models.md#data-base-entries)): small objects placed where one register reaches them, such as MIPS small data through `$gp`; `-mdata-bases=none`, the default, selects none. Targets without base-relative data addressing, such as x86-64, reject an entry. |

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
| `-fpeel-loops`, `-fpeel-limit=N` | Replace a loop whose trip count is known at compile time with one copy of its body per iteration when the copies have at most N operations, so that each copy computes with constant induction values; like GCC's `-fpeel-loops` with `--param max-completely-peeled-insns=N`. A loop qualifies when it has one exit, tested on every iteration, and integer arithmetic on its induction variables decides the trip count. The presets enable it at `-O2` with N = 96 and at `-O3` with N = 192. |
| `-fif-conversion-limit=N`, `-fif-conversion-memory-limit=N` | With `-fif-conversion`, replace a branch with selects, or combine the branches of `&&` and `\|\|`, when the operations that then run unconditionally, and the selects, cost at most N operations; the second limit applies instead when it is larger and the condition tests loaded data with a mask or the top bit of an unsigned value. The presets set 6 and 12 at `-O2` and 12 and 12 at `-O3`. A preset's first limit applies only where the target selects without a branch, which MIPS CPUs without conditional moves cannot do; a limit that a profile or the command line sets applies everywhere. Under `-ftree-reassoc`, which the presets enable from `-O2`, `&&` and `\|\|` of two comparisons of one integer with constants become one unsigned comparison of the integer's offset when the second comparison needs no other work, whatever the limit and also without `-fif-conversion`. |
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

`as` reads GNU-syntax assembly, the language of `cc -S` output, and writes one
ELF relocatable object from all its inputs (`-` reads standard input). It
assembles MIPS code; other targets are rejected.

| Option | Selects |
| --- | --- |
| `-o FILE` | The object file (default `a.out`). |
| `-target TRIPLE`, `-mprofile=NAME` | The target and its defaults, as for `cc`. |
| `-march=CPU`, `-mips1` ... `-mips64r2` | The CPU or ISA level whose instructions are accepted, until a `.set` directive changes it. |
| `-mabi=NAME`, `-32`, `-64` | The ABI, which fixes the ELF class and ABI flags: `-mabi=32` and `-32` select o32, `-mabi=64` and `-64` n64 (also switching the triple to `mips64`), `-mabi=eabi` EABI32. |
| `-EB`, `-EL` | Big- or little-endian output. |
| `-mFEATURE`, `-mno-FEATURE`, `-mnan=2008\|legacy` | Target features, such as `-msoft-float`, `-msingle-float`, `-mfp32`, `-mfpxx`, and `-mfp64`, which set the floating-point ABI that `.MIPS.abiflags` records. |
| `-I DIR` | A further directory to search for `.include` and `.incbin` files. |
| `--fatal-warnings`, `-W` | Treat warnings as errors, or suppress them. |

Without `-target` or `-mprofile`, `as` uses the default target of `cc`. Accepted
directives include sections and their flags and COMDAT groups (`.section`,
`.text`, `.data`, `.bss`, `.rdata`, `.sdata`, `.sbss`, `.pushsection`,
`.popsection`, `.previous`), symbols (`.globl`, `.local`, `.weak`, `.hidden`,
`.internal`, `.protected`, `.type`, `.size`, `.set` and `=`, `.equ`,
`.equiv`, `.comm`, `.lcomm`), data (`.byte`, `.2byte`, `.half`, `.short`,
`.4byte`, `.word`, `.long`, `.8byte`, `.dword`, `.quad`, `.uleb128`,
`.sleb128`, `.ascii`, `.asciz`, `.string`, `.float`, `.double`, `.zero`,
`.space`, `.fill`, `.incbin`), layout (`.p2align`, `.balign`, `.align`,
`.org`), source structure (`.include`, `.macro`, `.rept`, `.if` and its
variants, `.error`), DWARF (`.file`, `.loc`, and the `.cfi_*` directives,
which produce `.debug_line`, `.debug_frame`, and `.eh_frame`), and the MIPS
`.set` options (`push`, `pop`, `reorder`, `noreorder`, `at`, `noat`, `macro`,
`nomacro`, `mips0` ... `mips64r2`), `.option pic0`, `.ent`, `.end`, `.frame`,
`.mask`, `.fmask`, and `.nan`. The instructions are the integer, multiply
and divide, load and store, branch, jump, FPU, FPU control, CP0, TLB, cache,
and `sync` instructions of MIPS I-V, MIPS32, and MIPS64 Release 1 and 2,
each accepted where the current ISA level has it; `madd`, `msub`,
`mul`, `ext`, `ins`, `seb`, `seh`, traps, `pref`, `round`, `ceil`, `floor`,
coprocessor 2, MIPS16, microMIPS, and Allegrex instructions other than `wsbw`
are not. The relocation operators are `%hi`, `%lo`, `%gp_rel`, `%higher`, and
`%highest`; the macros are `li`, `dli`, `move`, `b`, `bal`, `beqz`, `bnez`,
`beqzl`, `bnezl`, `negu`, `neg`, and `not`, expanded as LLVM MC expands them.
Objects carry the MIPS ELF header flags, `.MIPS.abiflags`, `.reginfo` (ELF32)
or `.MIPS.options` (ELF64), and `.pdr` for each `.end`. Unlike GNU as:

- `.word`, `.half`, `.dword`, `.float`, and `.double` do not align their data;
- in `.set reorder` mode a branch's delay slot gets a `nop`, but instructions
  are not reordered and no MIPS I hazard `nop`s are inserted;
- `.L` local labels stay out of the symbol table unless a relocation must
  name one;
- the `div` and `divu` macros, address macros such as `la` and `lw REG,
  symbol`, and PIC code (`-mabicalls`, `.cpload`) are not available: write
  `div $zero, rs, rt` and `%hi`/`%lo` pairs.

## Inspecting the compiler

These options print what the selected target and models provide. Combine them
with `-target`, `-march`, or `-mprofile` to inspect another configuration.

| Option | Lists |
| --- | --- |
| `--print-targets` | Compiled-in architectures and their triples. |
| `--print-profiles`, `--print-abis` (`-mabi=help`), `--print-manglings`, `--print-optimizations`, `--print-models` | Model entries. |
| `--print-cpus` | The target's CPU entries with their parents, features, and issue widths. |
| `--print-costs` | The latency, issue cost, and availability of each of the target's instructions for `-march` and `-mtune`: a `cpu: NAME  tune: NAME  issue_width: N` line, then one `INSTRUCTION  latency: N  issue: N  available: yes` (or `no`) line per instruction. |
| `--print-data-bases` | The `data_base` entries: one `NAME  base: REGISTER  sections: LIST  max_size: N  kinds: LIST  source: FILE:LINE` line each. |
| `--print-options[=all\|optimization\|common\|target]` | Options, values, and origins. |
| `--print-keywords`, `--print-builtins`, `--print-attributes` | The language surface. |
| `--print-features`, `--print-instructions`, `--print-registers` | Target feature queries, machine instructions, and register names. |

## Diagnostics

Diagnostics have the form `file:line:column: error: message`, followed by the
source line and a caret. Errors in generated code also show the macro or
syntax expansion chain; `as` reports an error inside a macro at the line that
invokes it. Errors in a model file have the form
`file:line: error: message`, the same under `cc`, `cpp`, and `as`, and errors
that belong to no file, such as an unknown option, have the form
`cc: error: message`, `cpp: error: message`, or `as: error: message`. The
tools exit with status 1 after an error and 0 otherwise.

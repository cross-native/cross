# Using cc, cpp, as, and ld

```text
cc [options] input.x ...
cpp [options] input.x ...
as [options] input.s ...
ld [options] file ...
```

`cc` preprocesses and compiles Cross source (`.x`) to assembly or object
files. `cpp` only preprocesses, writing Cross to standard output or to `-o`.
`as` assembles hand-written GNU-syntax MIPS assembly into an object file
([Assembling](#assembling)), and `ld` links MIPS objects into a static
executable ([Linking](#linking)). `cc --help`, `cpp --help`, `as --help`, and
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
`ld` or another linker, and with your own startup code and libraries. Nothing
is added implicitly, and `main` has no special meaning: choose the entry
symbol with the linker.

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
| `-fpeel-loops`, `-fpeel-limit=N` | Replace a loop whose trip count is known at compile time with one copy of its body per iteration when the copies have at most N operations, so that each copy computes with constant induction values; like GCC's `-fpeel-loops` with `--param max-completely-peeled-insns=N`. A loop qualifies when it has one exit, tested on every iteration, and integer arithmetic on its induction variables decides the trip count. With `-floop-bounds-from-extents`, a loop whose exit test reads memory also qualifies when its passes are bounded by an access to an array of known extent: each copy keeps its exit test, and the last copy leaves the loop. The presets enable it at `-O2` with N = 96 and at `-O3` with N = 192. |
| `-fno-loop-bounds-from-extents` | Do not bound a loop's trip count by its accesses. By default from `-O1`, when every pass of a loop begins by accessing `a[i]`, an element of an array object of known extent (a static or automatic array, not a pointer), with no call, store, volatile access, or operation that may trap before it, and integer arithmetic on the loop's induction variables decides `i`, the loop is assumed to make only the passes that keep `a[i]` inside the array, since an access past an object's extent is undefined; `-fpeel-loops` uses that bound, and `-fpeel-limit` still decides whether the copies are made. This is no relaxation: a program whose loop would make another pass already has undefined behavior. GCC's `-faggressive-loop-optimizations` corresponds to this option together with the no-wrap assumption for signed arithmetic that Cross makes unless `-fwrapv` is given. |
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
| `-T FILE`, `--script=FILE` | The linker script. |
| `-e SYMBOL`, `--entry=SYMBOL` | The entry point, a symbol or an address. Without it, `ENTRY` in the script, then `_start`, then the start of `.text` (with a warning). |
| `-L DIR`, `-l NAME` | `-l NAME` links `libNAME.a` (`-l:NAME` links `NAME`) from the `-L` directories and the script's `SEARCH_DIR`s. |
| `-R FILE`, `--just-symbols=FILE` | The addresses of the global symbols that the ELF file FILE defines, without its contents. |
| `-b binary`, `--format=binary` | Read the following inputs as raw data, each as a writable `.data` section with the symbols `_binary_NAME_start`, `_binary_NAME_end`, and `_binary_NAME_size`, where NAME is the path as written with each character other than a letter or digit replaced by `_`. `-b elf32-bigmips` (any ELF format) or `--format=default` ends raw input. |
| `-u SYMBOL` | Require SYMBOL, linking an archive member that defines it. |
| `--defsym SYMBOL=EXPRESSION` | Define SYMBOL with a script expression. |
| `-Ttext=ADDRESS`, `-Tdata=ADDRESS`, `-Tbss=ADDRESS`, `--section-start=NAME=ADDRESS` | Place an output section at ADDRESS, read as hexadecimal. |
| `-r`, `--relocatable` | Write a relocatable object instead of an executable. |
| `--gc-sections`, `--print-gc-sections` | Remove the allocated sections that nothing reaches, and report each one removed. |
| `-Map FILE`, `-M` | Write a link map to FILE, or to standard output. |
| `-m EMULATION`, `--oformat=FORMAT`, `-EB`, `-EL` | The output class and byte order instead of the first object's: the emulations `elf32btsmip`, `elf32ltsmip`, `elf32ebmip`, `elf32elmip`, `elf64btsmip`, and `elf64ltsmip`, or the formats `elf32-bigmips`, `elf32-littlemips`, `elf32-tradbigmips`, `elf32-tradlittlemips`, and their `elf64-` counterparts. |
| `-z max-page-size=SIZE` | The alignment of loadable segments in the file: by default 64 KiB for ELF64 and for the `ts` emulations and `trad` formats, otherwise 4 KiB. |
| `-s`, `--strip-all` | Omit the symbol table. |
| `--fatal-warnings` | Treat warnings as errors. |
| `--start-group`, `--end-group` | Accepted; archives are searched in any order. |

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

**Linker scripts.** The accepted commands are `SECTIONS`, `MEMORY`, `PHDRS`,
`ENTRY`, `INCLUDE` (searched in the current directory, the `-L` and
`SEARCH_DIR` directories, and the including script's directory),
`SEARCH_DIR`, `EXTERN`, `ASSERT`, `OUTPUT_ARCH`, `OUTPUT_FORMAT` (both
ignored), and assignments, also inside `PROVIDE`, `HIDDEN`, and
`PROVIDE_HIDDEN`. An output section
description may give an address, `(NOLOAD)`, `AT(...)`, `ALIGN(...)`,
`>REGION`, `AT>REGION`, `:PHDR`, and `=FILL` (a 32-bit big-endian pattern);
its contents are input section descriptions, `BYTE`, `SHORT`, `LONG`, `QUAD`,
`SQUAD`, assignments to symbols and to `.`, and `ASSERT`. An input section
description is `FILE(SECTION ...)` with the wildcards `*`, `?`, and `[...]`,
optionally in `KEEP(...)`, with `EXCLUDE_FILE(...)`, `SORT`, `SORT_BY_NAME`,
`SORT_BY_ALIGNMENT`, `SORT_NONE`, and `ARCHIVE:MEMBER` file patterns; a file
pattern matches the path as written, and an archive member matches its
archive's path. `COMMON` names the common symbols and `/DISCARD/` drops what
it matches. Expressions have C's operators and the functions `ABSOLUTE`,
`ADDR`, `ALIGN`, `ALIGNOF`, `BLOCK`, `CONSTANT`, `DEFINED`, `LENGTH`,
`LOADADDR`, `LOG2CEIL`, `MAX`, `MIN`, `NEXT`, `ORIGIN`, `SEGMENT_START`,
`SIZEOF`, and `SIZEOF_HEADERS`. Other commands, such as `OVERLAY`, `INSERT`,
`INPUT`, `GROUP`, `STARTUP`, `NOCROSSREFS`, `REGION_ALIAS`, `SUBALIGN`,
`ONLY_IF_RO`, `FILL`, output section types other than `NOLOAD`, and the
`DATA_SEGMENT_*` functions, are diagnosed.

**Layout.** Addresses, load addresses, empty sections, and the location
counter follow GNU ld: an output section without contents is omitted (its
address still counts when it defines symbols); a section without an address
follows the location counter, or with `>REGION` that region, at its
alignment; a plain number assigned to `.` inside an output section is an
offset from the section's start. A section without `AT` or `AT>` keeps the
difference between load and virtual address of the previous section in its
region, and one without them that follows a section with `AT>` in the same
region also loads in that section's load region. Sections without `>REGION`
follow the location counter even when `MEMORY` defines regions. An input
section that no rule matches keeps its name and follows the last output
section of its kind (code, read-only data, data, or zero-filled data), or of
an earlier kind; non-allocated ones come last, and `COMMON` goes to `.bss`.
Without `-T` the layout is a subset of GNU ld's default for MIPS: the ABI
sections and code at 0x400000 (0x120000000 for ELF64), the data on the next
page at the same page offset, `_gp` 32 KiB past the start of the small data,
and `_etext`, `_edata`, `__bss_start`, and `_end`.

**Segments.** Without `PHDRS`, allocated sections share a loadable segment
unless a section's load and virtual addresses differ by another amount, it
overlaps the previous section or starts more than a page after its end, it
holds data after a zero-filled section, or it is writable after read-only
sections and on another page. With `PHDRS`, a section without `:PHDR` takes
the program headers of the previous section unless it is `NOLOAD`. The file
and program headers join the first loadable segment when there is room for
them below its first section in that page, without `PHDRS` or when the
segment's entry has `FILEHDR` or `PHDRS`. A `.MIPS.abiflags` or `.reginfo`
output section also gets a `PT_MIPS_ABIFLAGS` or `PT_MIPS_REGINFO` header.

**MIPS.** `ld` applies `R_MIPS_16`, `_32`, `_64`, `_26`, `_HI16`, `_LO16`,
`_GPREL16`, `_GPREL32`, `_LITERAL`, `_PC16`, `_PC32`, `_HIGHER`, and
`_HIGHEST`, the n64 combinations with `R_MIPS_64` and with `R_MIPS_SUB` and
`_HI16` or `_LO16`, and ignores `R_MIPS_JALR` hints; PIC, TLS, MIPS16, and
microMIPS relocations are diagnosed. A REL `R_MIPS_HI16` takes the low half
of its addend from the next `R_MIPS_LO16` of the same symbol, so several may
share one; one without such a `R_MIPS_LO16` gets a warning. `R_MIPS_26`
requires the target in the 256 MiB region of the delay slot. In ELF32 output
every address is a 32-bit value, so symbol addresses read with
`--just-symbols` and script addresses compare as such. gp-relative
relocations need a definition of `_gp`, and those against local symbols add
the gp value an object's `.reginfo` or `.MIPS.options` records. `R_MIPS_16`
fills the 16-bit field at its offset, where GNU ld fills the low half of the
32-bit word there. The header flags of the objects must agree on the ABI, the
NaN encoding, and FP64, and their ISAs must extend one another; the output
takes the newest ISA. `.reginfo` and `.MIPS.options` merge into one register
mask with the final `_gp`, and `.MIPS.abiflags` into the largest ISA and
register sizes with the floating-point ABI that all objects accept (the
objects' `.MIPS.abiflags`, where GNU ld takes it from `.gnu.attributes`;
objects with incompatible floating-point ABIs get a warning, as in GNU ld).
A `.pdr` record whose function a file does not contribute, such as a
duplicate COMDAT copy, is dropped, and `.gnu.attributes` sections are not
copied to the output.

**Other rules.** Relocation-free `SHF_MERGE` sections of one output section
are merged as GNU ld merges them: one copy of each string or constant, and
strings that end other strings share their tails. `--gc-sections` keeps the
entry, `-u`, `EXTERN`, and script symbols, `KEEP` sections, sections whose
symbols those reach through relocations, COMDAT groups as wholes, note, init
and fini array sections, `.ctors`, `.dtors`, OS- and processor-specific
sections, and `.eh_frame` whole (relocations in it that refer to removed code
resolve to zero, as they do in non-allocated sections). The symbol table
keeps local symbols except assembler-local labels in mergeable sections,
makes the symbols a script defines with `HIDDEN` or `PROVIDE_HIDDEN` local,
keeps the binding and visibility objects give their symbols, and leaves out
undefined weak symbols. The map lists discarded input sections, then each output section
with its input sections, the files they come from, their symbols, and the
script's assignments, in a format like GNU ld's. With `-r`, input sections
of one name share an output section, except that the members of a section
group keep their own and the group stays; relocations remain, against output
section symbols with their addends adjusted, and common symbols stay common;
`-r` takes `--defsym` but no linker script.

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
invokes it, and `ld` an error in a linker script at its position. Errors in a
model file have the form `file:line: error: message`, the same under `cc`,
`cpp`, and `as`, and errors that belong to no file, such as an unknown option,
have the form `cc: error: message`, `cpp: error: message`, `as: error:
message`, or `ld: error: message`. `ld` names the object (`archive(member)`
for an archive member), the section and offset, and the symbol of an error in
the input, as in `ld: error: start.o:(.text+0x8): undefined reference to
'main'`. The tools exit with status 1 after an error and 0 otherwise.

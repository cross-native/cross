# Using cc and cpp

```text
cc [options] input.x ...
cpp [options] input.x ...
```

`cc` preprocesses and compiles Cross source (`.x`) to assembly or object
files. `cpp` only preprocesses, writing Cross to standard output or to `-o`.
`cc --help` and `cpp --help` list every option. Options taking a value accept
it joined or separate (`-oout.s`, `-o out.s`).

## Output

| Option | Output | Default name |
| --- | --- | --- |
| `-S` | Target assembly | `input.s` |
| `-c` | Relocatable object (ELF, COFF, or Mach-O, by target) | `input.o` |
| `-E` | Preprocessed Cross, also accepted by `cc` as `.i` input | `input.i` |
| `-emit-llvm` | LLVM IR text of the compiled program, for debugging and comparison | `input.ll` |
| `-emit-gimple`, `-emit-gimple=rtl` | GCC GIMPLE text, for comparison with GCC's back end | `input.gimple.c` |

`-o FILE` names the output. `-c` runs `llvm-mc` from `PATH` as the assembler;
`cc` never calls a C compiler, LLVM's code generator, or a linker.

`cc` does not link: running it without `-S`, `-c`, or `-E` is an error, and
linker options such as `-e`, `-L`, and `-l` are rejected. Link the objects with
your own linker, startup code, and libraries. Nothing is added implicitly, and
`main` has no special meaning: choose the entry symbol with the linker.

All inputs of one command form a single compilation group with one output, named
after the first input when `-o` is absent. Definitions without `static` or
`global` are shared within the group but not exported to the linker. Compile
separately and link only `global` entities to combine objects.

The output depends only on the inputs, options, and models, not on the working
directory or the environment. Internal symbols of `static` entities derive from
each input's path as written on the command line, so the same command in a
copy of the tree produces identical files; `-ffile-prefix-map=OLD=NEW` gives
absolute input paths a location-independent spelling.

## Targets, ABIs, and profiles

| Option | Selects |
| --- | --- |
| `-target TRIPLE` | Target architecture, object format, and default ABI. |
| `-march=CPU`, `-mtune=CPU` | Instruction set and, independently, scheduling and cost tuning. |
| `-mFEATURE`, `-mno-FEATURE` | Target features, such as `-mavx2` or `-mno-red-zone`. |
| `-mOPTION=VALUE` | Other target options, such as `-mcmodel=large`. |
| `-mabi=NAME` | ABI for global, unresolved, and indirect calls (normally the Cross ABI). |
| `-mmangling=NAME` | Link-name encoding: `cross` (default), `simple`, `itanium`, or `msvc`. `itanium` and `msvc` match C++ names only for scalar and pointer-to-scalar types. |
| `-mprofile=NAME` | A profile: target, ABI, mangling, optimization preset, and options together. |

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
| `-ffunction-sections`, `-fdata-sections` | Put each function or object in its own section. |
| `-ffast-math` | Relax floating-point rules: finite math only, no signed zeros, `-ffp-contract=fast`. Under a profile that traps floating exceptions ([models.md](models.md#profiles)), it reassociates floating operations only when no exception but divide-by-zero traps, and `-ffp-contract=fast` fuses a multiply and an add only when overflow and underflow do not trap. |
| `-funwind-tables` | Emit unwind tables (CFI or SEH). No unwinder is linked. |
| `-fno-inline-functions` | Disable discretionary inlining; `[[always_inline]]` still applies. |
| `-fno-shrink-wrap` | Set up the stack frame and save preserved registers at function entry, not only on the paths that need them (the default from `-O1`). |
| `-fno-div-by-constant` | Keep the divide instruction for division and remainder by a constant. From `-Og`, such a divide becomes a multiplication by a fixed-point reciprocal with shifts and adds when the target prices that sequence lower; `-Os` and `-Oz` replace only a divide that a single shift or mask performs. |
| `-fno-eval-calls` | Stop evaluating ordinary calls during compilation; required evaluation still happens. |
| `-fwrapv` | Make signed `+`, `-`, `*`, and `<<` wrap modulo 2^N, at run time and in compile-time evaluation alike. Division of the minimum value by -1 and invalid shift counts stay undefined. No preset sets it. |
| `-fbounds-trap` | Check every subscript of an array whose bound is known, fixed or variable-length, and execute the inline `$::trap()` sequence when the index is outside the array; `&a[n]` may still form the address one past the end. No helper is called and no preset sets it. Functions with `[[naked]]` or `[[no_sanitize("bounds")]]` are not checked. |
| `-feval-step-limit=N`, `-feval-depth-limit=N`, `-feval-memory-limit=N`, `-feval-byte-limit=N` | Limits for compile-time evaluation and macro expansion. |
| `-fgeneric-instance-limit=N`, `-fgeneric-depth-limit=N` | Limits for generic instances per compilation (default 4096) and nested instantiation (default 128). |
| `-fzero-init-in-data` | Put zero-valued static objects with the initialized data instead of zero-filled storage such as `.bss`, so that an image patcher can change them. |
| `-ffile-prefix-map=OLD=NEW` | Spell source paths that start with `OLD` as `NEW` in `$::source::file` and internal symbol names; line markers, dependency files, and diagnostics keep the real paths. Repeatable; the last matching mapping applies. |

Options that have no meaning without a hosted runtime, such as `-fno-builtin`,
`-nostdlib`, or `-Ofast`, are rejected rather than ignored.

## Preprocessing

`-DNAME[=VALUE]`, `-UNAME`, `-I DIR`, `-isystem DIR`, `-include FILE`, and
`-imacros FILE` work as in C compilers. Quoted includes search the including
file's directory, then `-I`, then `-isystem`; angle includes search only `-I`
and `-isystem`. There are no implicit include directories. `-include` and
`-imacros` files are looked up relative to the current directory first and
apply to every input; all `-imacros` files are processed before the `-include`
files. `-M`, `-MM`, `-MD`, `-MMD`, `-MF`, `-MT`, and `-MQ` write Make
dependencies, including files read by `$::embed`. `cpp` writes one rule per
input and `cc` one rule for its output, whose prerequisites are the files of
every input. `-MP` adds an empty rule for each prerequisite other than an
input, so that Make does not fail when a header is deleted.

## Inspecting the compiler

These options print what the selected target and models provide. Combine them
with `-target`, `-march`, or `-mprofile` to inspect another configuration.

| Option | Lists |
| --- | --- |
| `--print-targets` | Compiled-in architectures and their triples. |
| `--print-profiles`, `--print-abis` (`-mabi=help`), `--print-manglings`, `--print-optimizations`, `--print-models` | Model entries. |
| `--print-options[=all\|optimization\|common\|target]` | Options, values, and origins. |
| `--print-keywords`, `--print-builtins`, `--print-attributes` | The language surface. |
| `--print-features`, `--print-instructions`, `--print-registers` | Target feature queries, machine instructions, and register names. |

## Diagnostics

Diagnostics have the form `file:line:column: error: message`, followed by the
source line and a caret. Errors in generated code also show the macro or
syntax expansion chain. Errors in a model file have the form
`file:line: error: message`, the same under `cc` and `cpp`, and errors that
belong to no file, such as an unknown option, have the form
`cc: error: message` or `cpp: error: message`. `cc` and `cpp` exit with
status 1 after an error and 0 otherwise.

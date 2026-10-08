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

## Targets, ABIs, and profiles

| Option | Selects |
| --- | --- |
| `-target TRIPLE` | Target architecture, object format, and default ABI. |
| `-march=CPU`, `-mtune=CPU` | Instruction set and, independently, scheduling and cost tuning. |
| `-mFEATURE`, `-mno-FEATURE` | Target features, such as `-mavx2` or `-mno-red-zone`. |
| `-mOPTION=VALUE` | Other target options, such as `-mcmodel=large`. |
| `-mabi=NAME` | ABI for global, unresolved, and indirect calls (normally the Cross ABI). |
| `-mmangling=NAME` | Link-name encoding: `cross` (default), `simple`, `itanium`, or `msvc`. |
| `-mprofile=NAME` | A profile: target, ABI, mangling, optimization preset, and options together. |

Without `-target` or `-mprofile`, `cc` uses the default target configured at
build time; `cc --version` prints it. Explicit options override a profile's
settings, independently of their order. [targets.md](targets.md) describes
each target, profile, and target option.

Profiles, ABIs, link-name encodings, and optimization presets are declarative
models built into `cc`; the readable shipped definitions are in `model/`.
`--model=FILE` loads an additional model file, and `--model-path=DIR` adds a
directory in which to find `--model` names. The model format is not yet
stable.

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
| `-ffast-math` | Relax floating-point rules: finite math only, no signed zeros, `-ffp-contract=fast`. |
| `-funwind-tables` | Emit unwind tables (CFI or SEH). No unwinder is linked. |
| `-fno-inline-functions` | Disable discretionary inlining; `[[always_inline]]` still applies. |
| `-fno-eval-calls` | Stop evaluating ordinary calls during compilation; required evaluation still happens. |
| `-feval-step-limit=N`, `-feval-depth-limit=N`, `-feval-memory-limit=N`, `-feval-byte-limit=N` | Limits for compile-time evaluation and macro expansion. |

Options that have no meaning without a hosted runtime, such as `-fno-builtin`,
`-nostdlib`, or `-Ofast`, are rejected rather than ignored.

## Preprocessing

`-DNAME[=VALUE]`, `-UNAME`, `-I DIR`, `-isystem DIR`, `-include FILE`, and
`-imacros FILE` work as in C compilers. Quoted includes search the including
file's directory, then `-I`, then `-isystem`; angle includes search only `-I`
and `-isystem`. There are no implicit include directories. `-include` and
`-imacros` files are looked up relative to the current directory first and
apply to every input; all `-imacros` files are processed before the `-include`
files. `-M`, `-MM`, `-MD`, `-MMD`, `-MF`, `-MT`, and
`-MQ` write Make dependencies, including files read by `$::embed`.

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
syntax expansion chain. `cc` and `cpp` exit with status 1 after an error and 0
otherwise.

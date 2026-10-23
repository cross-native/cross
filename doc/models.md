# Compiler models

A model file declares ABIs, link-name manglings, optimization presets, and
target profiles for `cc` and `cpp`. The language is defined in
[spec.md](spec.md) and the compiler options in [invoke.md](invoke.md); this
page defines the model-file syntax and semantics.

Model files are not Cross source; only `--model` loads them. Their names need
no particular suffix, and the shipped files use `.xm`. The format has no
version number and can change between compiler releases; keep custom model
files with the release that reads them. A file starts directly with an entry;
there is no header.

A model file contains entries of these kinds:

- `abi`: a calling convention;
- `mangling`: the rules that form link names;
- `optimization`: a named preset of option values;
- `profile`: a named set of target, ABI, mangling, preset, and option
  defaults;
- `debug`: the contents of debugging information;
- `instruction`: a form of a machine instruction;
- `cpu`: a processor's instruction set and instruction costs;
- `data_base`: data reached through a base register;
- `init_table`: a table of functions that a program's runtime calls; and
- `address_space`: a pointer representation with a stored width, a base,
  and a byte order.

Models cannot add keywords, expressions, functions, libraries, runtime
helpers, source macros, intrinsics, attributes, compiler options, or
optimization passes. `instruction` entries add instructions that the
assembler accepts and machine instruction built-ins expose, but the compiler
emits only the instructions it selects itself, and models cannot weaken
diagnostics or reorder the compiler's work. Unknown entry kinds,
properties, options, and entry names are errors.

## Loading and selection

The shipped models are built into `cc` and `cpp`, so no data files need to be
installed. Their readable sources are `model/common.xm` (the optimization
presets), `model/x86_64.xm`, `model/mips.xm`,
`model/mips_instructions.xm` (the MIPS instruction table), and
`model/aarch64.xm`. These options
load more files:

```text
--model=FILE          load one model file
--model-path=DIR      add a directory in which to find --model files
```

The shipped models load first, then each `--model` file in command-line
order. A `--model` argument that names an existing file is used as is;
otherwise a relative name is looked up in the `--model-path` directories in
command-line order, wherever those options appear. A `--model-path`
directory is searched only for `--model` names; nothing else in it is
loaded. Loading the same file twice has no further effect. Include paths
(`-I`) play no part in model lookup. Once every file is loaded, all entries
are checked, whether or not the compilation selects them, except for the
properties that a base may omit ([Inheritance](#inheritance)); an error
stops the compilation and is reported at the file and line of the entry or
property, the same under `cc` and `cpp`. An entry may name entries of any
loaded file.

A later file cannot replace an entry: reusing the name of a loaded entry, or
an ABI alias, is an error. Mangling, optimization, and profile names are
unique across all architectures. ABI names and aliases are unique per
`architecture` and address width, so ABIs with different address widths may
share a name. A shared name resolves to the entry with the address width of
the target triple's objects: 32 bits for `mips`, `mipsel`, and
`mipsallegrexel` triples, and 64 bits for `mips64`, `mips64el`, x86-64, and
AArch64 triples. An entry without `address_bits` ([Entry
properties](#entry-properties)) has every address width of its
architecture's triples. One of its aliases may also name an entry of the
same file that states `address_bits`; at that width, the alias denotes the
entry that states it. On MIPS, `x` therefore denotes `x32` for 32-bit
triples and `x64` for `mips64` triples.

`-mprofile=NAME`, `-O=NAME`, `-mabi=NAME`, and `-mmangling=NAME` select
entries; `-mabi` also accepts an alias. Naming an entry that is not loaded
is an error; nothing falls back to another entry. Without `-mprofile`, or
with `-mprofile=default`, the profile whose `default_for` pattern matches the
target triple most specifically applies: a pattern with more non-`*`
characters is more specific, and two profiles with equally specific matches
are an error that names them, even when `-mprofile` names a profile, because
the default profile also fixes the triple's default ABI. A profile supplies its `optimization` only when no `-O` option
is given. Explicit `-f` and `-m` options override profile and preset values
wherever they appear relative to `-O`.

`cpp` reads the same models, so the queries `$::has_abi`, `$::has_mangling`,
and `$::has_profile` ([Queries and inspection](#queries-and-inspection)) see
the entries that `cc` uses. Objects that call each other or share mangled
names must be compiled with compatible ABI, target-option, and mangling
entries.

## Syntax

Model files are UTF-8. An entry is a kind, a quoted name, and a body in
braces:

```text
kind "name" {
    property = value;
    block "name" {
        property = value;
    }
}
```

The kinds are `abi`, `mangling`, `optimization`, `profile`, `debug`,
`instruction`, `cpu`, `data_base`, `init_table`, and `address_space`. Only `abi` entries (`bank`, `rule`, `variadic_state`, and
`variadic_shadow`) and `cpu` entries (`instruction`) contain blocks, and a
block has no `;` after its closing brace. Every
property and every mangling rule ends in `;`. `#` and `//` start comments
that end at the end of the line.

Property names are identifiers: ASCII letters, digits, `_`, `-`, and `.`,
starting with a letter or `_`. A value is:

- a string in double quotes, with the escapes `\"`, `\\`, `\n`, `\r`, and
  `\t`; a string cannot span lines;
- an identifier other than `true` and `false`, which stands for the string
  with the same spelling;
- a decimal integer, which may start with `-` only where a property says
  so;
- `true` or `false`; or
- a list of strings or identifiers, such as `["a0", "a1"]`; `[]` is empty.

Names and string comparisons are case-sensitive. A property may appear only
once in an entry or block.

### Option properties

Properties of `optimization` and `profile` entries whose names begin with
`f.`, `m.`, or `w.` set the `-f`, `-m`, and `-W` options that
`cc --print-options=all` lists ([invoke.md](invoke.md),
[targets.md](targets.md)): `f.tree-ccp = true` means `-ftree-ccp`,
`m.red-zone = false` means `-mno-red-zone`, and `w.all = true` means
`-Wall`. Models use canonical option names, not aliases such as `f.inline`
for `f.inline-functions`. Presets cannot set warnings.

A value must suit its option: `true` or `false` for an on/off option, an
integer in the option's range, or one of the option's text values. An
integer option also accepts `true`, which selects the option's default, and
`false`, which selects `0`; `f.align-loops = true` aligns loops to the
default 16 bytes.

Profiles may set `f.unwind-model = "platform"` and `f.unwind-tables = true`
for platforms whose unwinder must walk Cross frames; the defaults are
`"none"` and `false`. Presets cannot set these two options but may set
`f.elide-noreturn-saves`. Models cannot describe unwind record formats.

### Inheritance

`optimization` and `cpu` entries name their bases with `inherits`: a text
naming one entry, or a list naming several entries, each once. A base is an
entry of the same kind, and for a `cpu` entry of the same architecture,
declared anywhere in any loaded file; an inheritance cycle is an error. An
entry starts from its bases, in list order, and then applies its own
properties and blocks:

- A property or block that the entry sets replaces what its bases give.
- Any other property, and any block of a given name such as
  `instruction "mult"`, comes from the nearest bases that set it. Direct
  bases have depth 1, their bases depth 2, and so on; a base reached along
  several paths counts once, at its smallest depth. Nearest bases that set
  different values are an error that the entry resolves by setting the
  property or block itself; deeper bases do not take part.
- A list that holds a set, the `features` of a `cpu` entry, is the union of
  what the bases give, in order, unless the entry sets it. A base gives its
  own list or, without one, the union of its bases' lists. The union is
  checked like a list that the entry states.

A base used only for composition need not be complete: a property that an
entry needs only when it is selected, such as the `targets` of a preset with
`m.*` properties, may come from the selected entry or any of its bases, and
is checked only for the entry that `-O`, `-march`, `-mtune`, a profile, or a
preset selects. Every other check applies to every entry.

In this example, `board` combines the shipped ISA level `isa-mips3`, whose
features are `mips3`, `llsc`, `branch-likely`, and `load-interlocks`, with
an FPU. Its features are the union of its bases' features, and it has the
`div.d` costs of `fpu-board`; if `isa-mips3` also described `div.d`, with
other costs, `board` would have to describe it too:

```text
cpu "fpu-board" {
    architecture = "mips";
    features = ["hard-float", "fp32"];
    instruction "div.d" { latency = 58; issue = 58; }
}

cpu "board" {
    architecture = "mips";
    inherits = ["isa-mips3", "fpu-board"];
    instruction "mult" { latency = 5; issue = 5; }
}
```

## ABI entries

An ABI entry describes a calling convention: its register banks, the ordered
rules that place each argument and result in registers or on the stack, the
stack layout, the storage that a call may change, and how variadic functions
work. ABI names have no built-in meaning; shipped and user entries are
interpreted the same way. An entry cannot contain instructions or runtime
calls.

The following simplified entry passes integers, pointers, `f32`, and `f64`
in the registers that the System V AMD64 ABI uses for them, passes every
other argument on the stack, and rejects other result types. It is not the
System V ABI, which the shipped `sysv_abi` entry in `model/x86_64.xm`
describes.

```text
abi "example-regs" {
    architecture = "x86-64";
    address_bits = 64;
    aliases = ["example"];

    stack_alignment = 16;
    stack_slot_bytes = 8;
    return_address_bytes = 8;
    call_clobbers = [
        "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "memory", "flags"
    ];

    bank "integer" {
        class = "integer";
        register_bits = 64;
        arguments = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"];
        results = ["rax", "rdx"];
    }

    bank "floating" {
        class = "simd";
        register_bits = 128;
        arguments = [
            "xmm0", "xmm1", "xmm2", "xmm3",
            "xmm4", "xmm5", "xmm6", "xmm7"
        ];
        results = ["xmm0", "xmm1"];
    }

    rule "zero" {
        match = ["zero"];
        action = "ignore";
    }

    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        min_bits = 1;
        max_bits = 128;
        unit_bits = 64;
        carrier_bits = 32;
    }

    rule "floating" {
        match = ["floating"];
        action = "direct";
        bank = "floating";
        min_bits = 32;
        max_bits = 64;
    }

    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
```

### Entry properties

| Property | Type | Default | Required |
| --- | --- | --- | --- |
| `architecture` | text | | yes |
| `address_bits` | integer | the triple's | no |
| `stack_alignment` | integer | | yes |
| `stack_slot_bytes` | integer | | yes |
| `aliases` | list | `[]` | no |
| `compilation_selectable` | boolean | `true` | no |
| `function_selectable` | boolean | `true` | no |
| `stack_layout` | text | `"packed"` | no |
| `stack_order` | list | `["arguments"]` | no |
| `argument_stack_base` | integer | `0` | no |
| `return_address_bytes` | integer | `0` | no |
| `argument_register_failure` | text | `"stack"` | no |
| `result_register_failure` | text | `"error"` | no |
| `call_clobbers` | list | `[]` | no |
| `pic_call_clobbers` | list | `[]` | no |
| `call_preserved_views` | list | `[]` | no |
| `elf_abi_tag` | text | none | no |
| `features` | list | `[]` | no |
| `private_carrier_bits` | integer | none | no |
| `natural_alignment_limit` | integer | the target's | no |
| `llvm_calling_convention` | text | `""` | no |
| `gcc_calling_attribute` | text | `""` | no |
| `type.NAME` | text | none | no |

An entry also needs at least one `bank` and one `rule` block. It may have
`variadic_state` and `variadic_shadow` blocks and the properties described
in [Variadic functions](#variadic-functions).

- `architecture` is an architecture name that `cc --print-targets` prints:
  `x86-64`, `mips`, or `aarch64`. Any other name is an error.
- `address_bits` is the nonzero width of addresses: of pointers, of the
  addresses passed for `out` and `inout` parameters, and of the addresses
  that `indirect` rules pass. Without it, the entry takes the address width
  of each target triple of its architecture
  ([Loading and selection](#loading-and-selection)), and loading checks the
  entry at each of those widths.
- `aliases` lists further names for the entry.
- `compilation_selectable` allows `-mabi` and a profile's `abi` to make the
  entry the ABI of a compilation. `function_selectable` allows
  `[[abi("name")]]` to name it on a function or function-pointer type.
- `stack_alignment` is the alignment of the outgoing argument area, and
  `stack_slot_bytes` is the size of one stack slot; both are powers of two.
  Every value on the stack starts on a slot boundary and fills at least one
  slot.
- `stack_layout` is `"packed"` or `"slots"`. With `"packed"`, stack values
  follow one another. With `"slots"`, argument position `n` owns the slot at
  `n * stack_slot_bytes`, but not below `argument_stack_base`, and a value
  that finds no register goes to the slot of its position; all banks used
  for arguments must then share one cursor (see
  [Register banks](#register-banks)).
- `stack_order` orders the stack regions `"arguments"` (stack-passed
  arguments), `"results"` (stack-passed results), and `"argument_spills"` (a
  reserved slot for each register argument). It must include `"arguments"`
  when a rule can place an argument on the stack, and `"results"` when a
  rule can place a result there. Go's internal register ABI, for example,
  uses `["arguments", "results", "argument_spills"]`.
- `argument_stack_base` is the number of bytes reserved at the start of the
  outgoing argument area: 32 in the shipped `ms_abi`, and 16 in `o32`.
- `return_address_bytes` is the size of the return address that a call
  pushes: 8 on x86-64 and 0 on MIPS.
- `argument_register_failure` and `result_register_failure` are `"stack"`,
  `"partial"`, `"exhaust"`, or `"error"`; see [Rules](#rules).
- `llvm_calling_convention` and `gcc_calling_attribute` name the convention
  that `-emit-llvm` and `-emit-gimple` output uses. They do not affect native
  code and may be empty.
- `type.NAME` names a scalar arithmetic type of the ABI, such as `i64` or
  `f80`, for translation-time code: `$::meta::abi_type` gives the type that
  the ABI of a checked call names `NAME`
  ([spec.md](spec.md#call-checkers)). `NAME` is any property-name text after
  `type.`. The shipped C ABIs name the C types `char`, `short`, `int`, `long`,
  `long_long`, `size_t`, `ptrdiff_t`, `intmax_t`, `wchar_t`, `wint_t`,
  `float`, `double`, and `long_double` ([targets.md](targets.md)), which the
  format checkers read; the Cross ABIs name none.

`call_clobbers` lists the storage that a call under the ABI may change; the
callee preserves everything else. An item is a register name, which stands
for the whole register it belongs to (`xmm0` covers `ymm0` and `zmm0`, and
`eax` covers `rax`), or `memory` or `flags`, which stand for memory and the
condition flags. When a function's own ABI preserves storage that a callee's
ABI may change, the compiler saves and restores that storage in the calling
function. Of a general register that the list omits, other than the stack
and frame pointers, a call preserves only as many low bits as the
`register_bits` of the entry's narrowest `"integer"` bank: on a MIPS CPU with
64-bit registers, a call under `o32` or `x32` may change the upper 32 bits
of `s0`-`s7`. `pic_call_clobbers` lists, in the same form, the storage that
a call may also change in position-independent code, such as MIPS
`-mabicalls` code, where a callee computes its own GOT pointer: `o32` lists
`gp`. `call_preserved_views` lists views of registers that `call_clobbers`
omits, each register once: a call preserves only the bits of the named view
and may change the rest of the register. AAPCS64 preserves only the low 64
bits of `v8`–`v15`, so the shipped AArch64 ABIs list `d8`–`d15`.

`elf_abi_tag` sets the ELF ABI tag of objects compiled with the entry as
their ABI (`-mabi` or the profile's `abi`); without it, objects get the tag
implied by the triple and `address_bits`. Each architecture defines the tags
it accepts, and any other tag is an error when the file loads. x86-64 and
AArch64 define none. MIPS defines `"eabi32"`, which requires
`address_bits = 32` and writes `EF_MIPS_ABI_EABI32` and GNU's
`.mdebug.eabi32` and `.gcc_compiled_long32` marker sections.

`features` lists target features, as `cc --print-features` names them,
that compilations with the entry as their ABI select in place of the
features of the `-march` CPU entry that conflict with them. A feature whose
prerequisites the CPU lacks, or that conflicts with a CPU fact that options
cannot select, is not selected, and `-m` options override the list. Each
name may appear once, the listed features and their prerequisites may not
conflict, and a CPU fact that options cannot select, such as `allegrex`, is
an error. The shipped MIPS `o32`, `x32`, and `eabi32` list `fp32` and `n64`
lists `fp64`, as GCC derives the FPR width from the ABI's general registers;
`x64` keeps the CPU's, and so does every ABI on a Release 6 CPU, whose
`mips32r6` or `mips64r6` conflicts with `fp32`.

`private_carrier_bits` marks the entry as the convention that
`-fprivate-abi` uses on MIPS for calls to functions defined without `global`
in the same compilation
([language.md](language.md#abis-and-interoperability)). It applies on CPUs
whose general registers have that many bits: the shipped `x32` sets 32 for
MIPS I and II, and `x64` sets 64 for MIPS III and later. At most one entry
per `architecture`, address width, and register width may set it. A
function whose signature the entry cannot place keeps its normal ABI.
x86-64 ignores the property.

`natural_alignment_limit`, a power of two, is the largest alignment in bytes
that a scalar type has by its size in compilations with the entry as their
ABI; records, arrays, and alignment requests follow from it. Without it the
target's applies: 8 on MIPS and 16 on x86-64 and AArch64. The shipped
`n64` and `x64` set 16, so `i128` and `u128` have the 16-byte alignment of
GCC's `__int128`.

On MIPS, every entry with 64-bit addresses, shipped or user-defined, produces
ELF64 objects as described in [targets.md](targets.md#mips).

### Register banks

A `bank` block names registers of one class:

| Property | Type | Default | Required |
| --- | --- | --- | --- |
| `class` | text | | yes |
| `register_bits` | integer | | yes |
| `cursor` | text | the bank's name | no |
| `arguments` | list | `[]` | no |
| `results` | list | `[]` | no |

`class` is the register class that `cc --print-registers` shows:
`"integer"`, `"simd"`, `"x87"`, or `"mask"` on x86-64, `"integer"`,
`"floating"`, or `"special"` on MIPS, and `"integer"`, `"simd"`, or
`"special"` on AArch64. Every register in `arguments` and
`results` must have that class and at least `register_bits` bits, the width
that the bank uses.

`arguments` and `results` list the registers for arguments and results in
order. An empty or omitted list leaves the bank unused in that direction,
and no rule may use the bank there: the shipped x86-64 entries give their
`x87` bank `arguments = []`, because `f80` arguments travel on the stack.

A cursor counts argument positions. `cursor` names the bank's cursor and
defaults to the bank's name. An argument at position `n` of a cursor uses
the register at index `n` of its bank's `arguments`, so a register may
appear more than once: the shipped `o32` entry lists `["f12", "f14", "f14"]`
because an `f32` takes one position and an `f64` two, so the second floating
argument uses `f14` after either. Banks that share a cursor share its
positions: the Microsoft x64 ABI gives its integer and floating banks one
cursor, so the third argument uses `r8` or `xmm2` according to its type,
while the System V ABI gives each bank its own cursor.

### Value kinds

Rules match a value by its kind:

- `"integer"`: integer types, `bool`, and enumerations;
- `"floating"`: `f32`, `f64`, `f80`, `f128`, and `fptr`;
- `"pointer"`: pointers, including function pointers, and `label` values;
- `"aggregate"`: structures and unions;
- `"structure"` and `"union"`: structures only and unions only;
- `"array"`: arrays;
- `"vector"`: vector types;
- `"zero"`: values without bits, such as `void`;
- `"pair"`: no value on x86-64 or MIPS, so it matches nothing there; and
- `"any"`: every value.

A value's width is its size in bits; a pointer's width is the entry's
address width. An `out` or `inout` parameter passes the address of its value,
so rules see it as a `"pointer"` of that width.

### Rules

A `rule` block places values:

| Property | Type | Default | Required |
| --- | --- | --- | --- |
| `match` | list | | yes |
| `action` | text | | yes |
| `bank` | text | none | see below |
| `applies_to` | list | all | no |
| `min_bits` | integer | `0` | no |
| `max_bits` | integer | `0` (no limit) | no |
| `max_elements` | integer | no limit | no |
| `argument_limit` | integer | `0` (no limit) | no |
| `requires_unused_banks` | list | `[]` | no |
| `requires_features` | list | `[]` | no |
| `forbids_features` | list | `[]` | no |
| `unit_bits` | integer | see below | no |
| `carrier_bits` | integer | `0` | no |
| `extension` | text | `"none"` | no |
| `register_failure` | text | the entry's | no |
| `cursor_alignment` | integer or `"value"` | `1` | no |
| `cursor_advance` | integer | `0` | no |
| `stack_alignment` | integer | `0` | no |
| `stack_size` | integer | `0` | no |
| `max_stack_alignment` | integer | `0` (no limit) | no |
| `merge_banks` | list | `[]` | no |
| `member_banks` | list | `[]` | no |
| `members` | text | `"any"` | no |
| `padding_bank` | text | none | no |
| `require_natural_alignment` | boolean | `false` | no |
| `uniform_members` | boolean | `false` | no |
| `max_members` | integer | no limit | no |

Rules are tried in order. The first rule whose filters all match and whose
action can carry the value places it. An action that cannot carry a value,
such as a `"direct"` rule whose registers are narrower than the value or a
`"flatten"` rule that meets a field it cannot place, leaves the value to the
next rule. Running out of registers does not select another rule;
`register_failure` decides what happens.

The filters are:

- `match`: a nonempty list of [value kinds](#value-kinds);
- `applies_to`: a nonempty subset of `"arguments"`, `"fixed_arguments"`,
  `"variadic_arguments"`, and `"results"`. `"arguments"` stands for both
  kinds of argument; the arguments of a function without `...` are fixed,
  and the unnamed arguments of a variadic call are variadic;
- `min_bits` and `max_bits`: inclusive limits on the value's width, where a
  `max_bits` of `0` sets no limit;
- `max_elements`: the largest element count of an `"array"` value, and the
  largest member count of a structure or union; it does not filter other
  kinds;
- `members`: `"only"` limits the rule to the fields and elements that a
  `"flatten"` rule places, `"none"` to whole values, and the default
  `"any"` matches both;
- `argument_limit`: when nonzero, only arguments whose zero-based position
  in the parameter list is below it;
- `requires_unused_banks`: banks that no earlier argument and no hidden
  result pointer may have used. The shipped `o32` entry combines it with
  `argument_limit = 2`, so floating-point registers carry only the first two
  arguments, and only when no integer argument precedes them; and
- `requires_features` and `forbids_features`: target features that must be
  enabled or disabled. Feature names are the target extensions that
  `cc --print-features` lists, without the `$::feature::` prefix, such as
  `avx`, `avx512f`, `mips3`, and `hard-float`; other names are errors. To
  accept any of several
  feature combinations, repeat the rule once per combination. The features
  are those of the compilation (`-march` and `-m` options), so objects that
  call each other through such a rule must be compiled with the same
  features.

The actions are:

- `"direct"`: the whole value in one register of `bank`;
- `"split"`: the value cut into `unit_bits` pieces, each in its own register
  of `bank` and at its original bit offset;
- `"coerce"`: the value's bits, whatever its kind, in `unit_bits` pieces of
  `bank`, as when a small structure travels in integer registers;
- `"flatten"`: each field or element placed by the rules on its own, then
  merged as described below;
- `"indirect"`: the value in memory and its address in a register of
  `bank`. With `unit_bits`, which must be a multiple of 8, each `unit_bits`
  piece gets its own address. For a result, the caller passes the address
  as a hidden argument before all other arguments;
- `"stack"`: the whole value in its stack region; and
- `"ignore"`: nothing.

`"direct"`, `"split"`, `"coerce"`, and `"indirect"` need `bank`; the other
actions must not name one. `unit_bits` defaults to the bank's
`register_bits` for `"split"` and `"coerce"`, and an `"indirect"` rule
without it passes one address for the whole value. A hidden result pointer
takes the first position of its bank's cursor, so it moves the arguments of
every bank that shares that cursor.

`carrier_bits` is the smallest register width that a piece occupies: a
narrower piece fills the low part of a `carrier_bits`-wide carrier, and the
carrier bits above the piece are undefined. In an `"integer"`-class bank, a
`"direct"`, `"split"`, or `"coerce"` rule may set `extension` to `"zero"` or
`"sign"`, which fills the register bits above `carrier_bits` by zero- or
sign-extending the carrier; the default `"none"` leaves them undefined.
Extension belongs to the transfer, not to the source type: the shipped
`o32` entry sign-extends every 32-bit integer, signed or not, which matters
on 64-bit CPUs. Stack pieces and the copies made by a `variadic_shadow` are
not extended, and a `"flatten"` rule does not extend fields; use `"direct"`
or `"coerce"` for an aggregate that needs extension.

When a value's bank has too few registers left, `register_failure`, which
defaults to the entry's `argument_register_failure` or
`result_register_failure`, decides: `"stack"` passes the whole value on the
stack and frees the registers already given to it, `"partial"` keeps the
pieces that found registers and passes the rest on the stack (each such piece
takes at least one slot and the bytes of its carrier, and in a `"packed"`
area is aligned to that size, up to `stack_alignment`), `"exhaust"` passes
the whole value on the stack as `"stack"` does and moves each cursor that
the value would have used past the last register of its banks, so no later
value takes a register of them, as AAPCS64 does, and `"error"` rejects the
function's signature. A `"flatten"` value counts as one value.

`cursor_alignment` aligns the position of every cursor that the rule uses
before the value is placed: to a power of two of positions, or, with
`"value"`, to the alignment of the value the rule places (for an
`"indirect"` rule, an address), at most the entry's `stack_alignment`,
counted in positions of `stack_slot_bytes` and at least one. A nonzero
`cursor_advance` makes the value use exactly that many positions of each such
cursor; a value that needs more is an error. Without them, a value starts at
the next position and uses one position per register. The shipped `o32`
entry aligns its arguments to the value, so 64-bit scalars and records with
8-byte alignment start at an even slot and an `f64` in floating-point
registers takes two positions; `eabi32` aligns its 64-bit values and its
128-bit integers to two positions, and so its `f32` values on an FPU without
odd single-precision registers.

`stack_alignment`, a power of two, and `stack_size`, both in bytes,
override the alignment and the minimum size of a value that the rule places
on the stack. `max_stack_alignment`, a power of two in bytes, caps that
alignment, as AAPCS64 starts an argument aligned above 16 bytes at a
16-byte boundary. The outgoing argument area is aligned to the largest of the
entry's `stack_alignment` and the alignments of the values on the stack,
and its size is rounded up to that alignment.

A `"flatten"` rule may set `require_natural_alignment`, `member_banks`,
`merge_banks`, `padding_bank`, `uniform_members`, and `max_members`. With
`require_natural_alignment = true`, the rule does not apply unless
every field offset and array stride meets the alignment of the nested
value. `merge_banks`, which requires `unit_bits`, divides the value into
`unit_bits` chunks and places each chunk in one register of the first bank
in the list that a field in the chunk uses, as when the System V ABI passes
a chunk that holds an `i32` and an `f32` in an integer register. A field
whose bank is not in the list makes the rule not apply, so a later rule such
as `"indirect"` or `"stack"` handles the value. A chunk that no field
overlaps takes no register, as in the System V ABI, unless `padding_bank`
names the bank whose register carries it, as GCC's n64 ABI does with every
64-bit word of a structure. A field that by itself fits one register of a
listed bank, such as a 128-bit vector, stays in one register. `member_banks` lets the rule apply only when every field and
element takes one of the listed banks, and keeps one piece for each, as when
GCC's n64 ABI returns a structure of two `f32` members in `f0` and `f2`.
With `uniform_members = true`, the rule applies only when the fields and
elements that it places, counting those at one offset once, as the members
of a union, have one value kind and width and fill the value without gaps,
padding, or overlap; each takes a register of its own. `max_members` is the
largest number of fields and elements that the rule places, so that, with
`member_banks`, a rule can state AAPCS64's homogeneous floating-point and
short-vector aggregates of up to four members. Such a rule cannot also have
`merge_banks`. A `"flatten"` value's cursors are those of the banks its
pieces use; its `cursor_alignment` and `cursor_advance` apply to each of
them.

Loading checks each ABI entry against its architecture, whatever the
selected target: register names, classes, and widths; feature names; the
ELF ABI tag; references to banks, cursors, and stack regions; the directions
that rules use; bit ranges; clobbers; and alignments. An error in any entry
stops the compilation.

### Variadic functions

| Property | Type | Default | Required |
| --- | --- | --- | --- |
| `variadic_supported` | boolean | `false` | no |
| `variadic_count_cursor` | text | none | no |
| `variadic_count_register` | text | none | no |
| `variadic_count_bits` | integer | `0` | no |
| `variadic_save_banks` | list | `[]` | no |
| `variadic_save_alignment` | integer | `1` | no |
| `variadic_home_bank` | text | none | no |
| `variadic_home_base` | integer | `0` | no |
| `variadic_home_stride` | integer | `0` | no |
| `variadic_va_list_bytes` | integer | see below | no |
| `variadic_va_list_alignment` | integer | `1` | no |

`variadic_supported = true` allows variadic functions (`...`) under the
entry; the variadic blocks, the count, the save banks, and the home bank
require it. The ordinary banks and rules place named and unnamed arguments
alike, and `applies_to` can tell them apart. These properties add the rest
of the convention:

- `variadic_count_cursor`, `variadic_count_register`, and
  `variadic_count_bits` go together. At each call to a variadic function,
  the number of positions of that cursor that all arguments use, named and
  unnamed, is written to the register as a `variadic_count_bits`-bit value.
- `variadic_save_banks` makes a variadic function store the argument
  registers of the listed banks, in list order, into one area aligned to
  `variadic_save_alignment`, a power of two, with one slot of the bank's
  `register_bits` per register.
- `variadic_home_bank` makes a variadic function store its incoming argument
  registers of that bank at `variadic_home_base + index *
  variadic_home_stride`, counted like a `"cursor_address"` state from the
  start of the caller's argument area; the stride must not be 0. A negative
  `variadic_home_base` places the slots in the function's own frame directly
  below the incoming stack arguments, which every slot must precede, and
  stores only the registers of unnamed arguments there, so that they and the
  stack arguments form one sequence.
- A `variadic_shadow` block copies each argument passed in its
  `source_bank` into the register at the same position of its
  `target_bank`. Both banks must share a cursor, and `target_bank` needs at
  least as many argument registers. `fixed_arguments` and
  `unnamed_arguments`, both `true` by default and not both `false`, select
  named and unnamed arguments.

The shipped `sysv_abi` entry uses, among others, these properties for the
vector-register count in `al` and the register-save area:

```text
variadic_supported = true;
variadic_count_cursor = "floating";
variadic_count_register = "al";
variadic_count_bits = 8;
variadic_save_banks = ["integer", "floating"];
variadic_save_alignment = 16;

variadic_state "gp_offset" {
    type = "u32";
    kind = "cursor_offset";
    cursor = "integer";
    base = 0;
    stride = 8;
}
```

The shipped `ms_abi` entry uses these for the home area and for copying
floating arguments into integer registers:

```text
variadic_supported = true;
variadic_home_bank = "integer";
variadic_home_base = 0;
variadic_home_stride = 8;

variadic_shadow "floating-integer" {
    source_bank = "floating";
    target_bank = "integer";
    fixed_arguments = true;
    unnamed_arguments = true;
}
```

A variadic definition reads its unnamed arguments through the bindings of
a `[[variadic(...)]]` attribute
([spec.md](spec.md#variadic-calls-and-implementation)); each binding names
a `variadic_state` block of the function's ABI:

| Property | Type | Default | Required |
| --- | --- | --- | --- |
| `type` | text | | yes |
| `kind` | text | | yes |
| `cursor` | text | none | see below |
| `base` | integer | `0` | no |
| `stride` | integer | `0` | see below |
| `limit` | integer | none | no |
| `alignment` | integer | `1` | no |
| `llvm_va_list_offset` | integer | none | no |
| `fields` | list | none | see below |

With `n` the number of positions of `cursor` that the named arguments use,
but no more than `limit`, or 0 without `cursor`, the kinds are:

- `"cursor_offset"`: the integer `base + n * stride`;
- `"cursor_address"`: the address `base + n * stride` in the incoming stack
  arguments, counted from the start of the caller's argument area, including
  its `argument_stack_base` bytes;
- `"register_save_address"`: the address `base + n * stride` in the area of
  `variadic_save_banks`, which must not be empty; and
- `"stack_address"`: the address just past the stack arguments of the named
  parameters, rounded up to `alignment`, plus `base`; and
- `"record_address"`: the address of a record in the definition's frame
  that holds the values of the states that `fields` lists, in list order,
  each at the next offset that its type's alignment allows; the record is
  aligned to the larger of `alignment` and its members' alignments.

A state with `cursor` needs a nonzero `stride`, and one without `cursor`
has neither `stride` nor `limit`; `base` and `stride` may be negative.
`fields` is required by and valid only for a `"record_address"` state,
which names in it other states of the entry, each once and none of kind
`"record_address"`; such a state has a pointer `type` and none of
`cursor`, `base`, `stride`, `limit`, and `llvm_va_list_offset`. The shipped
C ABIs other than `eabi32` define a state `va_list` that holds the value C
passes for a `va_list` argument: in `sysv_abi` a `"record_address"` state
of `gp_offset`, `fp_offset`, `overflow_arg_area`, and `reg_save_area`, the
address of a `__va_list_tag`, and in `ms_abi`, `o32`, and `n64` the address
of the next argument slot.
`cursor` names a cursor, which need not be the name of a bank. `alignment`
is a power of two. `type` is the exact Cross scalar or pointer type, without
qualifiers, that the binding must have: typedefs and generic arguments are
resolved first, and no conversion applies. State names, and the names of a
definition's bindings, must be unique. A definition without bindings
reserves no storage for them.

`-emit-llvm` describes the `llvm.va_start` area with
`variadic_va_list_bytes`, `variadic_va_list_alignment`, and each state's
`llvm_va_list_offset`, which must be less than `variadic_va_list_bytes`.
Without `variadic_va_list_bytes`, the area is a record of the entry's
variadic states in order, each of its `type`: a state with
`llvm_va_list_offset` at that offset, any other at the next offset after the
state before it that its alignment allows; its size is the end of the last
field, rounded up to the larger of `variadic_va_list_alignment` and the
largest field alignment, and 0 without states. Its pointer fields follow the
entry's address width. These properties do not affect native code.

### Shipped ABIs

The shipped x86-64 model defines the default Cross ABI `x` and the C ABIs
`sysv_abi` and `ms_abi`. The shipped MIPS model defines the Cross ABIs `x32`,
with 32-bit registers and addresses, and `x64`, with 64-bit registers and the
address width of the triple, and the C ABIs `o32`, `n64`, and `eabi32`.
The shipped AArch64 model defines the Cross ABI `x` and the C ABI `aapcs64`.
[targets.md](targets.md) describes them.

## Mangling entries

A mangling entry computes the link names of the functions, objects, and
global labels that the linker sees. Mangling names have no built-in meaning;
shipped and user manglings follow the same rules. The shipped `cross` and
`simple` manglings are:

```text
mangling "cross" {
    entity = name;
    label = name;
    generic = concat(entity, "::<", arguments(",", text, text), ">");
}

mangling "simple" {
    entity = concat(
        "_XN",
        path("", concat(decimal(bytes(text)), text))
    );
    label = concat(
        "_XL",
        path("", concat(decimal(bytes(text)), text))
    );
    generic = concat(
        entity,
        "G",
        decimal(count),
        arguments(
            "",
            concat("_t", decimal(bytes(text)), text),
            concat("_v", decimal(bytes(text)), text)
        )
    );
}
```

A mangling has three rules, which must produce text: `entity` gives the link
name of a function or object, `label` that of a global label, and `generic`
that of one instance of a generic function. Other rule names are errors.

A `helper` is a named expression with a result type and typed parameters;
the types are `text`, `integer`, and `boolean`. This helper, written inside
a mangling entry, encodes pointer prefixes recursively:

```text
helper text type_code(text value) = select(
    starts_with(value, "P"),
    concat(
        "P",
        type_code(slice(value, 1, subtract(length(value), 1)))
    ),
    lookup(value, concat("u", decimal(bytes(value)), value), "i32", "i")
);
```

A helper may call any helper of its entry, including itself, and cannot be
named `entity`, `label`, or `generic`. A helper body sees its parameters and
the declaration values `name`, `kind`, `result`, `variadic`, and
`parameter_count`, but not mapper values such as `text`. Helper calls nest
at most 128 deep; deeper nesting makes the link name fail.

The expression grammar is:

```text
expression =
    string | unsigned-integer | true | false
  | name | entity | kind | result | variadic | parameter_count
  | text | mode | index | count | substitution_index
  | helper-name(expression, ...)
  | concat(expression, ...)
  | decimal(integer-expression)
  | hex(integer-expression)
  | radix(integer-expression, radix)
  | bytes(text-expression)
  | length(text-expression)
  | path(separator, item-expression[, reverse-boolean])
  | arguments(separator, type-item-expression, value-item-expression)
  | parameters(separator, item-expression)
  | select(boolean, when-true, when-false)
  | equal(expression, expression)
  | not(boolean) | and(boolean, ...) | or(boolean, ...)
  | add(integer, integer) | subtract(integer, integer)
  | slice(text, offset, count)
  | replace(text, sought, replacement)
  | lookup(key, default, match, result, ...)
  | lower(text) | upper(text)
  | starts_with(text, prefix) | ends_with(text, suffix)
  | contains(text, fragment)
  | substitute(key, expansion, reference)
```

Every expression has the type text, unsigned integer, or boolean. The
branches of `select` and the operands of `equal` must have the same type.
Arithmetic saturates: `subtract` stops at zero. Text positions and lengths
count UTF-8 bytes. `slice` clamps to the available text, `replace` replaces
all nonoverlapping occurrences, and `lookup` returns the result of the first
exact match or the default. Case conversion is ASCII-only. `radix` accepts
bases 2 through 36 and uses lowercase digits. A wrong operand type, an
unavailable value or helper, a wrong number of helper arguments, an unknown
operation or rule, a duplicate or missing rule, and nesting deeper than 64
expressions are errors.

The values and operations mean:

- `name` is the qualified source name, such as `net::send`.
- `entity` is the result of the `entity` rule; only `generic` can use it.
- `kind` is `"function"`, `"object"`, or `"label"`.
- `result` is the type of a function's result or of an object, spelled as
  in [Type spellings](#type-spellings); it is empty for labels.
- `variadic` tells whether a function takes `...`, and `parameter_count`
  gives its number of parameters.
- `text`, `index`, `count`, and `mode` describe the current item of a
  mapper (`path`, `arguments`, or `parameters`): `index` is zero-based, and
  `count` is the number of items. At the root of `generic`, `count` is the
  number of generic arguments. `mode` exists only in `parameters`.
- `substitution_index` exists only in the `reference` operand of
  `substitute`.
- `concat` joins one or more texts.
- `decimal`, `hex`, and `radix` render an integer.
- `bytes` returns the UTF-8 byte count of a text.
- `path(separator, item[, reverse])` splits `name` at `::`, evaluates
  `item` with each component as `text`, and joins the results with
  `separator`; a true third operand visits the components in reverse order.
- `arguments(separator, type-item, value-item)` evaluates `type-item` for
  each type argument and `value-item` for each value argument, in order and
  with the argument's spelling as `text`, and joins the results.
- `parameters(separator, item)` evaluates `item` for each parameter in
  order, with its type spelling as `text` and its mode as `mode` (`"in"`,
  `"out"`, or `"inout"`, and `"in"` when no mode is written), and joins the
  results. The spelling omits a top-level `const` of an `in` parameter but
  keeps a `const` on a pointee, and an `out` or `inout` parameter is spelled
  with its declared type.

Mapper items may use every value of the enclosing rule. The `generic` rule
receives the generic arguments in order, whether they were written `name<...>`
or `name::<...>` or deduced. The link name of a generic instance does not
depend on that spelling, on a top-level `const` of an `in` parameter, or on the
names of the generic function's type parameters. In the `generic` rule,
`result` and the parameters' `text` spell the instance's types: each generic
argument replaces its parameter.

### Type spellings

`result`, the `text` of `parameters`, and type arguments spell types as
follows:

- Qualifiers come first, in the order `K` (`const`), `V` (`volatile`), `R`
  (`restrict`), and `A` (atomic).
- A scalar type is spelled by its name: `void`, `bool`, `i8` to `i128`, `u8`
  to `u128`, `iptr`, `uptr`, `f32`, `f64`, `f80`, `f128`, `fptr`, or
  `label`.
- `struct`, `union`, or `enum`, a space, and the qualified name spell a
  record or enumeration, as in `struct net::packet`; an instance of a generic
  record or union appends `<`, its argument spellings separated by `,`, and
  `>`, as in `struct list<u32>`.
- A type that an instance of a `global` generic function defines in its
  header or body is qualified by the instance: the function's qualified
  name, its argument spellings in `<` and `>`, `::`, and the tag, as in
  `struct make_pair<u64>::Pair`. The anonymous records and unions that the
  member list of a generic record or union defines are qualified the same
  way by its instance, as in `union variant<u64>::{0}`. Within one instance,
  in source order, the N-th anonymous type is `{N}` and the N-th repetition
  of a tag appends `#N`, counting from zero. The spelling of another type
  defined inside a function identifies its source unit and is otherwise
  unspecified.
- `P` followed by the pointee spells a pointer, as in `PKu8` for
  `const u8 *`. A pointer into address space `N` other than 0 is `PU`, `N`,
  `_`, and the pointee.
- `A`, the element count, `_`, and the element spell an array, as in
  `A4_u32`; `A*_` starts an array of unknown length.
- `Dv`, the lane count, `_`, and the element spell a vector, as in
  `Dv4_u32`; `Ds` starts a scalable vector.
- A function type is `F`, its ABI, result, result location, stack cleanup,
  clobbers, and parameters, and then `zE` when it is variadic or `E`
  otherwise. A text field is its byte length, `_`, and the text. The ABI
  is its canonical name, or that of the compilation's ABI when the type
  names none; the result location is a register name or `auto`; the stack
  cleanup is `caller` or `callee`. The clobbers are their count, `_`, and
  the sorted names. The parameters are `_`, their count, `_`, and for each
  parameter `i`, `o`, or `b` (`in`, `out`, or `inout`), its type, and its
  location. For example, `u32 (*)(in u32 x, out u32 y)` under the `x` ABI
  is `PF1_x3_u324_auto6_caller0__2_i3_u324_autoo3_u324_autoE`.

A value argument is spelled as its value followed by its type, as in
`4uptr` and `-3i32`; a `bool` is spelled `0u8` or `1u8`. A value of an
enumeration type is preceded by the type and `=`, as in `enum color=1u8`,
and an address constant is its pointer type, `=&`, the object's name, `+`,
and a byte offset, as in `Pu32=&table+4`.

These spellings are the input of a mangling, not a platform's link names; a
mangling maps them with operations such as `lookup` and `replace`. Distinct
types have distinct spellings. A mangling may still map different spellings
to one link name, and then the usual rules for link-name collisions apply.

### Substitutions

`substitute(key, expansion, reference)` is the only operation with state.
Each evaluation of `entity`, `label`, or `generic` starts with an empty,
ordered substitution table. For a new key, the operation records the key
with the next zero-based index and then evaluates and returns `expansion`.
For a key already in the table, it evaluates only `reference`, in which
`substitution_index` is the key's index. Keys and the produced text are
independent.

Evaluation has no other state and no input or output, so a link name
depends only on its declaration. An empty result from a root rule makes the
link name fail.

### Shipped manglings and link names

The shipped profiles all select `cross`, which keeps the qualified source
name, as in `net::send` and `larger::<u64>`. `simple` prefixes each name
component with its byte length, as in `_XN3net4send`. `itanium` and `msvc`
produce Itanium C++ and Microsoft C++ names for functions and objects whose
types are scalars and pointers to scalars, using `substitute` for the
back-references of those schemes. For records, arrays, vectors, function
pointers, `out` and `inout` parameters, and generic value arguments they
produce names that no C++ compiler produces, and `itanium` mangles objects
of the global namespace, such as `_Z7counter`, which C++ leaves unmangled. The
object format may prefix every link name; Mach-O adds `_`.

`[[link_name("...")]]` ([language.md](language.md)) gives one symbol an
exact link name and replaces its mangling. Assembly output quotes a link
name where its characters require it. `static` definitions, and definitions
without `global` that stay within the compilation group
([invoke.md](invoke.md#output)), get local names that no mangling affects.
Changing the mangling changes link names, not the lookup of source names.

The ABI and the mangling are independent: the ABI determines how a call
passes values, and the mangling determines symbol names. A profile may
combine any ABI of its target with any mangling, and `[[abi("name")]]` on a
function does not change its mangling.

## Optimization entries

An optimization entry is a named preset of option values, not a list of
passes. This example preset takes every value of `O2`, raises two limits,
and tunes for Zen 3; `-O=O2-tuned` selects it on x86-64:

```text
optimization "O2-tuned" {
    inherits = "O2";
    targets = ["x86-64"];
    f.inline-limit = 160;
    f.unroll-factor = 4;
    m.tune = "znver3";
}
```

| Property | Type | Default | Required |
| --- | --- | --- | --- |
| `inherits` | text or list | none | no |
| `targets` | list | `[]` | with `m.*` |
| `f.*`, `m.*` | option value | | no |

`inherits` names the entry's bases ([Inheritance](#inheritance)). The entry
replaces individual option values of its bases, and options that neither the
entry nor a base sets keep their defaults.

A preset may set only the options that `cc --print-options` marks
`presettable: yes`. `targets` is a list of architecture names as printed by
`cc --print-targets`; each `m.*` property of the entry must be an option of
every listed architecture, or without `targets` of some architecture.
Selecting a preset for an architecture that it or one of its bases does not
list is an error, and so is selecting a preset whose settings include `m.*`
properties when neither it nor a base has `targets`. The presettable target
options are
`m.tune` and `m.risc-cisc-balance`, and on x86-64 also
`m.prefer-vector-width`; `m.arch`, `m.cmodel`, `m.red-zone`, and the
instruction-set features belong in a profile or on the command line.

A preset only sets options: it cannot change the order of passes or skip
required compilation steps. Numeric settings such as `f.ccp-rounds`,
`f.unroll-factor`, `f.vector-interleave`, and `m.risc-cisc-balance` are
ordinary options; there is no separate level number.

The shipped presets `O0`, `Og`, `O1`, `O2`, `O3`, `Os`, and `Oz` are in
`model/common.xm` and set no `m.*` options; `-O` means `O1`.
`cc --print-optimizations` lists their settings, and
[invoke.md](invoke.md#optimization) describes how explicit options override
them. User models may add presets under other names, selected by
`-O=NAME` or a profile's `optimization`.

## Profiles

This example profile configures kernel code:

```text
profile "x86_64-kernel" {
    target = "x86_64-unknown-elf";
    abi = "x";
    mangling = "cross";
    optimization = "O2";

    f.omit-frame-pointer = false;
    f.function-sections = true;
    f.data-sections = true;
    m.arch = "x86-64";
    m.tune = "generic";
    m.red-zone = false;
    m.cmodel = "kernel";
}
```

| Property | Type | Default | Required |
| --- | --- | --- | --- |
| `default_for` | list | `[]` | no |
| `target` | text | none | no |
| `abi` | text | none | no |
| `mangling` | text | none | no |
| `optimization` | text | none | no |
| `f.*`, `m.*` | option value | | no |
| `debug` | text | `dwarf` | no |
| `data_bases` | list | `[]` | no |
| `constructors` | text | none | no |
| `destructors` | text | none | no |
| `fp_traps` | list | `[]` | no |
| `fp_denormal_operand` | text | `ieee` | no |
| `fp_denormal_result` | text | `ieee` | no |

`default_for` lists target-triple patterns in which `*` matches any
sequence of characters; the profile is the default for the triples it
matches (see [Loading and selection](#loading-and-selection)). `target` is
a target triple of a compiled-in architecture, `abi` an ABI name or alias,
`mangling` a mangling name, and `optimization` a preset name; an `m.*`
property must be an option of `target`'s architecture or, without `target`,
of some compiled-in architecture. The option properties are defaults:
explicit `-f` and `-m` options override them.

With `-mprofile=NAME`, the profile's `target` applies only when the command
line has no `-target`. A profile chosen by `default_for` never changes the
target. The target is fixed before the profile's `m.*` options are checked.

`debug` names the entry `-g` selects, and `data_bases` the `data_base`
entries in use ([Data-base entries](#data-base-entries)). `constructors`
and `destructors` name the `init_table` entries that `[[constructor]]` and
`[[destructor]]` use ([Initializer-table entries](#initializer-table-entries));
without one, the attribute is an error. `fp_traps` lists the distinct
floating-point exceptions the program runs with enabled, from `invalid`, `divide-by-zero`,
`overflow`, `underflow`, and `inexact`; `fp_denormal_operand` is `ieee` or
`trap`; `fp_denormal_result` is `ieee` or `flush`. The language specification
defines what the compiler may and may not do under each environment.

`-O` replaces the profile's `optimization` but not its `f.*` and `m.*`
settings, which override the preset's values. Explicit `-f` and `-m`
options override both, wherever they appear; when one option is given more
than once, the last one counts.

## Debug entries

A `debug` entry describes the debugging information a compilation emits with
`-g`. `format` names an emitter the compiler implements; the other properties
select the contents the emitter supports:

```text
debug "dwarf" {
    format = "dwarf";
    version = 5;
    frame_section = "debug_frame";
    lines = true;
    frames = true;
    variables = true;
    types = true;
}
```

| Property | Type | Default | Meaning |
| --- | --- | --- | --- |
| `format` | text | none | `dwarf` is the only shipped emitter; it implements version 5. |
| `version` | integer | emitter default | The format version. |
| `frame_section` | text | `debug_frame` | Where call-frame information goes: `debug_frame` (not loaded) or `eh_frame` (loaded; `-funwind-tables` emits the latter independently). |
| `lines` | bool | `true` | Line tables and inlining origins for every emitted function, including clones. |
| `frames` | bool | `true` | Call-frame information for every emitted function. |
| `variables` | bool | `true` | Locations of parameters, including the pointer channels of `out` and `inout` parameters at ABI boundaries, and of locals. |
| `types` | bool | `true` | Type descriptions, namespaces, and generic instances. |

`-g=NAME` selects an entry; `-g` selects the profile's `debug` entry, or the
shipped `dwarf` when the profile names none; `-g0` emits nothing. The shipped
`dwarf-lines` entry enables only `lines` and `frames`. A `debug` entry contains
no block.

## Instruction entries

An `instruction` entry describes one form of a machine instruction: how the
assembler, `as` and `cc -c` alike, parses and encodes it; the effects that
the assembler's reordering and the compiler follow; its default costs; and
the machine instruction built-in (`$::_NAME`,
[spec.md](spec.md#machine-instruction-built-ins)) that exposes it. The entries of
an architecture form its instruction table, which the assembler, the
built-ins, `cpu` entries, `cc --print-instructions`, and `cc --print-costs`
share. Architectures whose instructions have one width take entries: MIPS,
whose shipped table is `model/mips_instructions.xm`. The x86-64 table is
built into the compiler, and an x86-64 entry is an error.

```text
instruction "addu" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "000000 rs:5 rt:5 rd:5 00000 100001";
    reads = ["rs", "rt"];
    writes = ["rd"];
}
```

| Property | Type | Default | Meaning |
| --- | --- | --- | --- |
| `architecture` | text | none | Required. |
| `mnemonic` | text | the entry name | The assembler spelling: lower-case letters, digits, `.`, and `_`. |
| `operands` | list | `[]` | The operands in assembler order ([Operands](#operands)). |
| `encoding` | text | none | The instruction word ([Encodings](#encodings)); required unless the entry is an alias or a macro. |
| `features` | list | `[]` | The form exists on a CPU with any one of these features, or on every CPU of the architecture without any. |
| `excludes` | list | `[]` | A CPU with any one of these features lacks the form. |
| `reads`, `writes` | list | `[]` | The operands, registers, and other resources that the form reads and writes ([Effects](#effects)). |
| `keeps` | list | `[]` | Register operands that the form writes only in part or only on a condition, keeping the rest of their value. |
| `hazards` | list | `[]` | The target's hazard classes of the form. |
| `control` | text | none | A control transfer: `jump`, `branch`, `exit`, or `trap`. |
| `condition` | text | none | The comparison of a `branch`: of its one register operand with zero, `equal`, `not-equal`, `less-than-zero`, `greater-equal-zero`, `less-equal-zero`, or `greater-than-zero`, an ordering as a signed integer; or of its two, `equal`, `not-equal`, `less-than` and `greater-equal` as signed integers, or `less-than-unsigned` and `greater-equal-unsigned`. |
| `delay_slot` | bool | `false` | The next instruction executes before the transfer. |
| `likely` | bool | `false` | A `branch` whose delay slot executes only when it is taken. |
| `privileged` | bool | `false` | The form executes only in a privileged mode. |
| `volatile` | bool | `false` | Every execution is observable: the compiler does not merge, remove, or reorder the form across another effect. |
| `hazard_uses`, `hazard_writes` | list | `[]` | Pipeline facts of resources that the hardware does not interlock ([Effects](#effects)). |
| `latency`, `issue` | integer | `1` | The default costs, from 0 and from 1 to 65535, that `cpu` entries replace. |
| `constraints` | list | `[]` | Relations that operands keep, immediates by value and general registers by number, such as `"pos+size <= 32"` or `"rt-rs > 0"`: a sum of operands and integers compared with an integer by `<=`, `<`, `>=`, `>`, `==`, or `!=`. |
| `builtin` | text or `false` | the mnemonic | The name of the form's built-in, `$::_NAME` ([Machine instruction built-ins](#machine-instruction-built-ins)); `false` gives it none. |
| `alias` | text | none | Another entry, which this one spells under its own mnemonic ([Aliases and macros](#aliases-and-macros)). |
| `macro` | text | none | A macro of the target's assembler that the form expands to. |
| `expands`, `otherwise` | list | `[]` | The instructions that the form expands to. |

Entry names are unique per architecture. The forms of a mnemonic are tried
in table order: a statement assembles as the first form whose operands it
has and that the current CPU has, and is an error, naming the ISA level,
when the CPU has none of the forms whose operands it has. A file loaded
later can add instructions and forms but cannot replace an entry. The
instructions that the compiler itself emits keep the forms of the shipped
table: after loading, the compiler checks that the table defines each of
them with the operands and effects it expects, and an entry of another file
that adds a form to one of their mnemonics is an error.

### Operands

An operand is `NAME:TYPE`. `NAME:TYPE=DEFAULT` makes the operand, and every
operand after it, optional; the default is an integer or the name of an
earlier register operand, whose value it takes. A literal register such as
`$zero` is an operand that a statement spells as that register and that the
encoding does not hold. The operand types are those that the architecture
lists below; another type is an error.

A statement separates its operands with commas. A form takes the statement
when it has at least the form's required operands and at most all of them,
a register where the form has a register operand, and no register where the
form has an immediate. Of several such forms that the CPU has, the
statement assembles as the first whose immediate operands, where their
values are known when it is assembled, hold those values and whose
constraints those values and its registers satisfy; when none does, the
first reports the values that do not fit.

### Encodings

An encoding lists the bits of the instruction word from the most significant
down, in runs of `0` and `1` and in fields:

- `EXPR:WIDTH`, the low `WIDTH` bits of `EXPR`: an operand name, a part of a
  memory operand (`NAME.base`, `NAME.offset`), or a sum of names and
  integers with `+` and `-`, such as `size-1` or `pos+size-1`;
- `NAME[HIGH:LOW]` or `NAME[BIT]`, those bits of an operand's value; and
- `REGISTER:WIDTH`, the number of a register, such as `zero:5`, which counts
  as a register that the instruction uses.

Spaces separate the runs and fields, whose widths add up to the
architecture's instruction width. Each operand other than a literal register
appears in a field. A field of one operand holds each value of the operand,
a signed one in two's complement, and the slices of an operand hold each bit
of its values once; a field of several operands must hold their value when
an instruction is assembled. A value that a relocation gives or layout
settles goes into a field of its own in the low bits that the architecture
names. Fields cannot overlap.

### Effects

`reads` and `writes` name register operands, registers as
`cc --print-registers` lists them, and other resources: any other name,
such as `memory` or a coprocessor's state. A memory operand's base register
is read. A `clobber` attribute ([spec.md](spec.md)) may name a resource that
the entry of a built-in names. A register operand in `keeps` is one that
`writes` names and whose old value can survive the form, such as the
destination of a conditional move or of a load that merges into its
register: its built-in reads it, and the assembler's reordering, as GNU as
does, does not count that as a read.

`control` makes the form a control transfer: `jump` transfers
unconditionally, calls included, and `branch` conditionally; `exit` leaves
the function, as a return or a jump through a register does; and `trap` has
no next instruction. The built-in of a control transfer is usable only in a
naked function: a `jump` to a label and a `branch` with a `condition` branch
within it, and an `exit` or `trap` form leaves it.

`hazard_uses` and `hazard_writes` list facts `RESOURCE@STAGE`. A form that
writes a resource makes it available at its stage, and a form that uses it
reads it at its stage; a use needs the writer's stage minus the user's stage
minus one instructions after the write. `RESOURCE[OPERAND]@STAGE` names the
resource with an immediate operand's value: `cp0[rd]@7` names `cp0.12` for
`rd` 12. A fact followed by ` if OPERAND=V|V...` applies only while the
operand has one of those values.

### Aliases and macros

An entry with `alias` names an entry that is not an alias and takes only
`architecture` and `mnemonic`: it spells that entry, with its operands,
encoding, features, effects, and costs, under its own mnemonic.

An entry with `macro` names a macro of the target's assembler, and its
operands suit the macro. An entry with `expands` or `otherwise` lists the
lines it expands to: a mnemonic and operands, each an operand of the entry,
an immediate operand negated (`-value`), or a register (`$zero`). A line
emits the first form of its mnemonic that takes those operands, and may
leave out optional operands. The lines of `expands` apply when every
immediate fits the operand that takes it, and those of `otherwise` apply
otherwise: before them, the assembler loads the entry's one `imm` operand
into the target's scratch register, as wide as the operand of `otherwise`
that the register takes. Macro entries have no encoding and no effects of
their own; their instructions have theirs.

### Costs

`cpu` entries and `cc --print-costs` name instructions by mnemonic, in table
order, aliases aside. The first form of a mnemonic gives its default costs;
a later form cannot state costs.

### Machine instruction built-ins

Each form gives a machine instruction built-in, named `$::_` and its
mnemonic with `_` for each `.`, as `$::_add_s` for `add.s`, or `$::_NAME`
with `builtin = "NAME"`; an alias gives the forms of the entry that it
spells under its own mnemonic. The built-in's operands are the form's
operands in order without its literal registers, which it prints: a
register operand that the form writes is an output, one that it also reads
or keeps, or whose value an omitted operand takes as its default, is both,
and the others are inputs; an immediate operand is a constant in the range
of its type that satisfies the form's constraints; a memory operand is an
lvalue, which must be writable where the form writes `memory`; and a label
operand is a label of the function. A form of the built-in exists for each
feature in `features`, for each register width that its operand types
take, one width for all of them, with the features those types need, and
for each number of its optional operands. The forms of one built-in may
differ in their operand types and immediate ranges; two that take the same
operands under the same features are an error, unless both are branches
that compare and branch alike, such as forms whose register constraints
choose an encoding, and the first stands for both.

No built-in is given by a macro or an expansion; by a form whose operand
types cannot be built-in operands; by a `jump` or `branch` without a label
operand, with effects other than reading the registers it compares, or, for
a `branch`, without a `condition`; by a form other than a branch whose
constraints name its registers, which register allocation does not
arrange; or by a form with `builtin = false`. A built-in of a branch is a
comparison and a branch to a label, whatever instruction does them, so its
register constraints do not apply. Calling a name that no form gives is an
error that says why, and so is `builtin = "NAME"` on such a form.
`cc --print-instructions` lists each form's built-in or why it has none.

### MIPS instructions

MIPS instructions have 32 bits. The operand types:

| Type | Assembler operand | Field value | As a built-in operand |
| --- | --- | --- | --- |
| `gpr`, `gpr32`, `gpr64` | A general register, such as `$4` or `$a0`. | Its number. | A general register of 32 or 64 bits, 32 bits, or 64 bits; 64 bits need `mips3`. |
| `fpr32`, `fpr64` | `$f0` to `$f31`. An odd register holds a 32-bit value only where odd single-precision registers exist, and a 64-bit value only with 64-bit FPRs. | Its number. | A floating register of 32 or 64 bits; needs `hard-float`, and for 64 bits not `single-float`. |
| `cpr`, `cpr64` | As `fpr32` and `fpr64`, or a coprocessor register `$0` to `$31`, which counts in no register mask and no hazard. | Its number. | As `fpr32` and `fpr64`. |
| `fcc` | `$fcc0` to `$fcc7`. | Its number. | None. |
| `cp0`, `hwr`, `fcr` | A coprocessor 0 register `$0` to `$31`; a hardware register of `rdhwr` `$0` to `$31`; an FPU control register `$0` to `$31` or `$f0` to `$f31`. | Its number. | A constant written in register syntax; `fcr` needs `hard-float`. |
| `uimmN`, `simmN` | An unsigned or signed integer of `N` bits, from 1 to 32. | The value. | A constant of `N` bits. |
| `LOW..HIGH` | An integer from `LOW` to `HIGH`. | The value. | A constant. |
| `imm` | An integer of up to 64 bits. | Macros only. | None. |
| `mem`, `mem8`, `mem16`, `mem32`, `mem64` | `offset(base)`, `(base)`, or an absolute `offset`, whose base is `$zero`; the offset is signed 16 bits. | Parts `.base` and `.offset`. | An lvalue of any width, or of that width. |
| `mem_simmK`, `memN_simmK` | As `mem` and `memN`, with a signed offset of `K` bits, from 1 to 16, that takes no relocation operator. | Parts `.base` and `.offset`. | As `mem` and `memN`. |
| `branch`, `jump` | A label, reached PC-relative in 16 bits, or in the current 256 MB region in 26. | The relocated value. | A label of the function. |
| `branch21`, `branch26` | A label, reached PC-relative in 21 or 26 bits, as a `branch` counting words from the next instruction. | The relocated value. | A label of the function. |
| `pcrel19s2`, `pcrel18s3` | A label, or a byte offset that is a multiple of 4 or 8, reached from the instruction in 19 bits of words or 18 bits of doublewords; the doubleword's counts from the instruction's doubleword. | The relocated value, or the offset in words or doublewords. | None. |

`simm16`, `uimm16`, and memory offsets also take `%hi`, `%lo`, `%gp_rel`,
`%higher`, and `%highest` and values that only layout settles; their field
is bits 15-0, that of a `branch` too, that of a `jump` bits 25-0, and those
of `branch21`, `branch26`, `pcrel19s2`, and `pcrel18s3` their low bits.

The hazard classes are those of GNU as's reordering in `.set reorder` code.
`load`: a load's register is ready one instruction late without
`load-interlocks`. `coprocessor-load`: so is a loaded coprocessor register.
`from-coprocessor` and `to-coprocessor`: so is the destination of a transfer
to or from a coprocessor without `fpu-transfer-interlocks`, or for
coprocessor 0 `load-interlocks`. `coprocessor`: a coprocessor instruction
that waits one instruction after such a transfer that writes no FPR.
`condition-read` and `condition-write`: the coprocessor condition, which a
reader uses two instructions after a transfer that writes it and one after a
compare without `fpu-compare-interlocks`. `no-delay-slot`: never moved into
a delay slot. Without `hilo-interlocks`, a write of `hi` or `lo` follows a
read of it by two instructions. The resource `cp0` marks a coprocessor 0
instruction. A `branch` without a delay slot, a Release 6 compact branch,
has a forbidden slot: in `.set reorder` code a `nop` follows it before a
control transfer, data, an alignment, and the end of the input, and an
instruction in it does not move into a later delay slot.

The shipped `dext` and `dins` also spell `dextm` and `dextu`, and `dinsm`
and `dinsu`, so that a field that ends or starts above bit 31 assembles as
the instruction that takes it, as in GNU as. The forms that Release 6
removes or re-encodes exclude `mips32r6` and `mips64r6`, and its own forms
need them. The macros `li` and `dli` take
a register and an `imm` operand and expand to the table's `addiu`, `ori`,
`lui`, `dsll`, and `dsrl32` as GNU as expands 32- and 64-bit constants. The
scratch register of `otherwise` is `$at`, which a macro under `.set noat`
cannot use. Under `.set nomacro` a statement that expands to several
instructions draws a warning.

This user model adds a coprocessor 2 instruction that `as` assembles, and
that code calls as `$::_vmix(d, a, b)`:

```text
instruction "vmix" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "010010 1 0000 rs:5 rt:5 rd:5 000000";
    reads = ["rs", "rt", "cop2"];
    writes = ["rd"];
    volatile = true;
    latency = 4;
}
```

## CPU entries

A `cpu` entry describes a processor: the instruction-set features it
implements, which decide what the compiler may emit, and the cost of its
instructions, which guides the compiler's choice among correct instruction
sequences and their order. An entry states which of the architecture's
instructions, those of its instruction table
([Instruction entries](#instruction-entries)) or on x86-64 those the
compiler implements, a processor has and what they cost. This entry
describes part of the VR4300:

```text
cpu "vr4300" {
    architecture = "mips";
    features = ["mips3", "hard-float", "fp32", "llsc", "branch-likely",
                "load-interlocks"];
    instruction "div.s" { latency = 29; issue = 29; }
    instruction "mul.s" { latency = 5; }
    instruction "div" { latency = 37; issue = 37; }
}
```

| Property | Type | Default | Required |
| --- | --- | --- | --- |
| `architecture` | text | none | yes |
| `inherits` | text or list | none | no |
| `features` | list | `[]` | no |
| `issue_width` | integer | `1` | no |
| `function_alignment` | integer | the target's | no |

`architecture` names a compiled-in architecture as `cc --print-targets`
prints it; CPU names are unique per architecture. `inherits` names the
entry's bases ([Inheritance](#inheritance)), whose properties and blocks the
entry starts from; the entry replaces individual properties and
`instruction` blocks, and its `features` replace its bases' union.
`features` lists the instruction-set
features the processor implements by the names of the `-m` feature options
(`cc --print-options=target`); each is enabled as its option would enable it,
and two features that conflict, directly or through what they enable, are an
error. `issue_width`, from 1 to 65535, is the number of instructions the
processor starts per cycle. `function_alignment`, a power of two from 1 to
65536, is the alignment in bytes that functions compiled for speed get when
the entry is the `-mtune` CPU; `-falign-functions` overrides it, and size
goals keep the target's smaller alignments.

An `instruction "NAME"` block describes one instruction, named as
`cc --print-costs` lists the target's instructions:

| Property | Type | Default | Meaning |
| --- | --- | --- | --- |
| `latency` | integer | the target's | Cycles, from 0 to 65535, from the start of the instruction until a dependent instruction can use its result. |
| `issue` | integer | the target's | Cycles, from 1 to 65535, before the processor can start another instruction of the same kind. |
| `available` | bool | `true` | `false` removes an instruction that the features would allow. |

The instruction table gives each instruction a default latency and issue
cost, 1 unless it states otherwise; an instruction that neither the entry
nor a base describes, or a property that its block omits, has the default. `cc --print-costs` shows
the costs in effect.

`-march=NAME` (`m.arch`) selects the entry whose `features` and `available`
properties decide which instructions are legal; explicit `-mFEATURE` and
`-mno-FEATURE` options adjust its features. `-mtune=NAME` (`m.tune`) selects
the entry whose costs guide instruction selection and scheduling; without it
the `-march` entry's costs apply. Costs never change which instructions are
legal or what a program computes. An operation with an inline alternative,
such as `$::clz`, uses the instruction only where the `-march` entry has it
and the `-mtune` costs favor it; a machine instruction built-in (`$::_name`)
that needs an unavailable instruction is an error, and so is an operation
that the target implements only with one. The `m.arch` and `m.tune`
properties of profiles and presets name `cpu` entries of their
architectures. `m.arch`, like `-march`, may follow the name with `+FEATURE`
and `+noFEATURE` modifiers ([invoke.md](invoke.md#targets-abis-and-profiles)),
which change the entry's features in order.

The shipped CPUs are `cpu` entries in `model/mips.xm`, `model/x86_64.xm`,
and `model/aarch64.xm`.

## Data-base entries

A `data_base` entry describes data that code reaches through a register
holding a fixed base address, with one instruction and a short offset,
instead of a full address, such as MIPS small data. This is the shipped
`mips-gp`:

```text
data_base "mips-gp" {
    base = "gp";
    sections = [".sdata", ".sbss"];
    max_size = 8;
    kinds = ["writable"];
}
```

| Property | Type | Default | Meaning |
| --- | --- | --- | --- |
| `base` | text | none | Required: the register that holds the base address, by a name `cc --print-registers` lists. |
| `sections` | list | none | Required: one or two sections the base reaches; the first holds objects with initial values and, without a second section, also objects that start as zero. |
| `max_size` | integer | `0` | Definitions of one to this many bytes that have no `section` attribute are placed in those sections; thread-local, weak, `noinit`, alias, and `weakref` objects are not. |
| `kinds` | list | `["writable"]` | Which objects `max_size` places: `writable` objects, `readonly` objects (defined `const`), and `literal` constants that the compiler creates for string and byte-sequence literals and for constant pools. |

A profile's `data_bases` property or `-mdata-bases=NAME[,NAME...]` selects
entries; `-mdata-bases=none`, the default, selects none. While an entry is
selected, the compiler does not allocate its base register, places the
qualifying objects defined in the compilation group in its sections (in the
first listed entry that admits them), and reaches those objects, and every
object defined or declared with a `section` attribute naming one of its
sections, through the base. An object defined in another group is reached by
its full address unless its declaration names such a section, so `section`
opts an object in or out. With `-fdata-sections` a placed object's section is
the entry's section followed by `.` and its link name, such as
`.sdata.counter`; the first section is writable unless the entry admits only
`readonly` and `literal` objects. A target accepts as `base` only the
registers it can address data from (on MIPS `gp`); a target without
base-relative data addressing, such as x86-64 or AArch64, rejects every
entry, and two selected entries cannot name the same section. No ABI that the compilation
uses may carry arguments or results in the base register, and no manual
location or register binding may name it. A profile's `data_bases` sets
`-mdata-bases` (`[]` selects none); a profile cannot also set
`m.data-bases`. The program sets
the base register before such code runs, and the linker keeps the sections
within the base's reach; on MIPS, `$gp` holds the address the linker gives
`_gp` and reaches 32 KiB on either side of it.

## Initializer-table entries

An `init_table` entry describes a table of functions that the startup or
exit code of a program's runtime calls, and in which `[[constructor]]` and
`[[destructor]]` place entries ([spec.md](spec.md#initializer-tables)), as
a profile's `constructors` and `destructors` properties select. This is the
shipped table that MinGW-w64 calls at exit:

```text
init_table "x86_64-mingw-fini" {
    section = ".dtors";
    priority_section = ".dtors.";
    invert_priority = true;
    abi = "ms_abi";
}
```

| Property | Type | Default | Meaning |
| --- | --- | --- | --- |
| `section` | text | none | Required: the section of the entries with priority 65535 and of those without a priority. |
| `priority_section` | text | none | The start of the section names of the other entries: the name of an entry with priority `P` follows it with `P` as five decimal digits, such as `.init_array.00101`. Without it, such priorities are errors. |
| `invert_priority` | bool | `false` | With `priority_section`: the five digits are 65535 minus `P`. |
| `abi` | text | the compilation's | The function-selectable ABI with which the runtime calls the entries; with a profile's `target`, an ABI of its architecture. |

Each entry is a pointer to a function without parameters or result under
`abi`, at the alignment of a pointer, in a section that holds writable
data. Section names have the object format's spelling of `section` names,
and the section takes the type that the format gives its name, as GNU `as`
does: on ELF, `.init_array`, `.fini_array`, and their prioritized forms are
SHT_INIT_ARRAY and SHT_FINI_ARRAY sections, and a Mach-O name such as
`__DATA,__mod_init_func,mod_init_funcs` gives its type. Section names are
not checked when the file loads but where an entry uses them, as `section`
names are. An `init_table` entry contains no block. The shipped tables are
listed in [targets.md](targets.md#object-formats).

## Address-space entries

An `address_space` entry declares a pointer representation that
`[[address_space("NAME")]]` selects ([spec.md](spec.md)): pointers into the
space are stored in `bits` bits as the distance of their target from a base.
This entry describes an arena of 32-bit offsets kept in big-endian order:

```text
address_space "arena32" {
    bits = 32;
    base_object = "game_arena";
    byte_order = "big";
}
```

| Property | Type | Default | Meaning |
| --- | --- | --- | --- |
| `bits` | integer | none | Required: the stored width, at most the target's pointer width. |
| `base` | integer | `0` | A constant base address. |
| `base_object` | text | none | The link name of a `global` object of pointer type that holds the base at run time; it excludes `base`. |
| `byte_order` | text | the target's | `big` or `little`: the order of the stored pointer's bytes. |

The shipped `flat32` entry has 32 bits and base 0; on a 32-bit target its
pointers equal native ones. An `address_space` entry contains no block.

## Queries and inspection

The preprocessor queries `$::has_abi("NAME")`, `$::has_mangling("NAME")`,
and `$::has_profile("NAME")` expand to `1` when the loaded models define
the entry, for ABIs as a name or alias of the selected target, and to `0`
otherwise. They work in `#if` and `#require` and anywhere else in source
([language.md](language.md#preprocessor)):

```x
#if $::has_abi("sysv_abi") && $::has_mangling("cross")
[[abi("sysv_abi")]]
global i32 add(i32 a, i32 b) {
    return a + b;
}
#endif

#if $::has_profile("x86_64-kernel")
global u32 kernel_profile_loaded = 1;
#endif
```

These options list what the loaded models define:

- `--print-models` lists the loaded model sources: `shipped:NAME` for each
  shipped file and the path of each `--model` file.
- `--print-abis` lists the ABIs of the selected target with the aliases
  that name them for its triple and whether they are selectable, and the
  target's default ABI; it does not reflect `-mabi`.
- `--print-manglings` lists the manglings.
- `--print-profiles` lists each profile's target, ABI, mangling, preset,
  tables, and option settings.
- `--print-optimizations` lists each preset, its bases, and its own
  settings.
- `--print-cpus` lists the selected target's `cpu` entries with their
  bases and features, and `--print-costs` prints the latency, issue cost,
  and availability of every instruction for the selected `-march` and
  `-mtune`.
- `--print-instructions` lists the selected target's machine instruction
  built-ins, then each form of an instruction table that models give, with
  its operands, the features of which it needs one and those it excludes,
  whether it is an alias or a macro, and the file and line of its entry.
- `--print-data-bases` lists the `data_base` entries, and
  `--print-init-tables` the `init_table` entries.
- `--print-options[=all|optimization|common|target|warning]` lists each option
  with its value for the compilation, its origin (`default`, `preset`,
  `profile`, or `command-line`), whether presets may set it, and the model
  file and line that set it.

Only `--print-options` shows the values in effect for a compilation.
Dependency output (`-M`, `-MD`) does not list model files; add them to the
dependencies of your build.

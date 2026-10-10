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

A model file contains entries of four kinds:

- `abi`: a calling convention;
- `mangling`: the rules that form link names;
- `optimization`: a named preset of option values; and
- `profile`: a named set of target, ABI, mangling, preset, and option
  defaults.

Models cannot add keywords, expressions, functions, libraries, runtime
helpers, macros, instructions, intrinsics, attributes, compiler options, or
optimization passes, and they cannot weaken diagnostics or reorder the
compiler's work. Unknown entry kinds, properties, options, and entry names
are errors.

## Loading and selection

The shipped models are built into `cc` and `cpp`, so no data files need to be
installed. Their readable sources are `model/common.xm` (the optimization
presets), `model/x86_64.xm`, and `model/mips.xm`. These options load more
files:

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
are checked, whether or not the compilation selects them; an error stops the
compilation and is reported at the file and line of the entry or property,
the same under `cc` and `cpp`. An entry may name entries of any loaded file.

A later file cannot replace an entry: reusing the name of a loaded entry, or
an ABI alias, is an error. Mangling, optimization, and profile names are
unique across all architectures. ABI names and aliases are unique per
`architecture` and `address_bits`, so ABIs with different address widths may
share a name. A shared name resolves to the entry whose `address_bits`
equals that of the triple's default ABI, which is the `abi` of the profile
that `default_for` selects for the triple. On MIPS, `cross` therefore
denotes `cross32` for 32-bit triples and `cross-n64` for `mips64` triples. A
profile that is a triple's default must name its ABI unambiguously, for
example by its canonical name.

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

The kinds are `abi`, `mangling`, `optimization`, `profile`, and `debug`.
Only `abi` entries contain blocks (`bank`, `rule`, `variadic_state`, and
`variadic_shadow`), and a block has no `;` after its closing brace. Every
property and every mangling rule ends in `;`. `#` and `//` start comments
that end at the end of the line.

Property names are identifiers: ASCII letters, digits, `_`, `-`, and `.`,
starting with a letter or `_`. A value is:

- a string in double quotes, with the escapes `\"`, `\\`, `\n`, `\r`, and
  `\t`; a string cannot span lines;
- an identifier other than `true` and `false`, which stands for the string
  with the same spelling;
- an unsigned decimal integer;
- `true` or `false`; or
- a list of strings or identifiers, such as `["a0", "a1"]`; `[]` is empty.

Names and string comparisons are case-sensitive. A property may appear only
once in an entry or block.

### Option properties

Properties of `optimization` and `profile` entries whose names begin with
`f.` or `m.` set the `-f` and `-m` options that `cc --print-options=all`
lists ([invoke.md](invoke.md), [targets.md](targets.md)): `f.tree-ccp = true`
means `-ftree-ccp`, and `m.red-zone = false` means `-mno-red-zone`. Models
use canonical option names, not aliases such as `f.inline` for
`f.inline-functions`.

A value must suit its option: `true` or `false` for an on/off option, an
integer in the option's range, or one of the option's text values. An
integer option also accepts `true`, which selects the option's default, and
`false`, which selects `0`; `f.align-loops = true` aligns loops to the
default 16 bytes.

Profiles may set `f.unwind-model = "platform"` and `f.unwind-tables = true`
for platforms whose unwinder must walk Cross frames; the defaults are
`"none"` and `false`. Presets cannot set these two options but may set
`f.elide-noreturn-saves`. Models cannot describe unwind record formats.

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
| `address_bits` | integer | | yes |
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
| `elf_abi_tag` | text | none | no |
| `private_carrier_bits` | integer | none | no |
| `llvm_calling_convention` | text | `""` | no |
| `gcc_calling_attribute` | text | `""` | no |

An entry also needs at least one `bank` and one `rule` block. It may have
`variadic_state` and `variadic_shadow` blocks and the properties described
in [Variadic functions](#variadic-functions).

- `architecture` is an architecture name that `cc --print-targets` prints:
  `x86-64` or `mips`. Any other name is an error.
- `address_bits` is the nonzero width of addresses: of pointers, of the
  addresses passed for `out` and `inout` parameters, and of the addresses
  that `indirect` rules pass.
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
  `"partial"`, or `"error"`; see [Rules](#rules).
- `llvm_calling_convention` and `gcc_calling_attribute` name the convention
  that `-emit-llvm` and `-emit-gimple` output uses. They do not affect native
  code and may be empty.

`call_clobbers` lists the storage that a call under the ABI may change; the
callee preserves everything else. An item is a register name, which stands
for the whole register it belongs to (`xmm0` covers `ymm0` and `zmm0`, and
`eax` covers `rax`), or `memory` or `flags`, which stand for memory and the
condition flags. When a function's own ABI preserves storage that a callee's
ABI may change, the compiler saves and restores that storage in the calling
function. Of a general register that the list omits, other than the stack
and frame pointers, a call preserves only as many low bits as the
`register_bits` of the entry's narrowest `"integer"` bank: on a MIPS CPU with
64-bit registers, a call under `o32` or `cross32` may change the upper 32
bits of `s0`-`s7`.

`elf_abi_tag` sets the ELF ABI tag of objects compiled with the entry as
their ABI (`-mabi` or the profile's `abi`); without it, objects get the tag
implied by the triple and `address_bits`. Each architecture defines the tags
it accepts, and any other tag is an error when the file loads. x86-64 defines
none. MIPS defines `"eabi32"`, which requires `address_bits = 32` and writes
`EF_MIPS_ABI_EABI32` and GNU's `.mdebug.eabi32` and `.gcc_compiled_long32`
marker sections.

`private_carrier_bits` marks the entry as the convention that
`-fprivate-abi` uses on MIPS for calls to functions defined without `global`
in the same compilation
([language.md](language.md#abis-and-interoperability)). It applies on CPUs
whose general registers have that many bits: the shipped `cross32` sets 32
for MIPS I and II, and `cross64` and `cross-n64` set 64 for MIPS III and
later. At most one entry per `architecture`, `address_bits`, and width may
set it. A function whose signature the entry cannot place keeps its normal
ABI. x86-64 ignores the property.

On MIPS, every entry with `address_bits = 64`, shipped or user-defined,
produces ELF64 objects as described in [targets.md](targets.md#mips).

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
`"integer"`, `"simd"`, `"x87"`, or `"mask"` on x86-64, and `"integer"`,
`"floating"`, or `"special"` on MIPS. Every register in `arguments` and
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
- `"array"`: arrays;
- `"vector"`: vector types;
- `"zero"`: values without bits, such as `void`;
- `"pair"`: no value on x86-64 or MIPS, so it matches nothing there; and
- `"any"`: every value.

A value's width is its size in bits; a pointer's width is `address_bits`.
An `out` or `inout` parameter passes the address of its value, so rules see
it as a `"pointer"` of `address_bits` width.

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
| `merge_banks` | list | `[]` | no |
| `require_natural_alignment` | boolean | `false` | no |

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
- `max_elements`: the largest element count of an `"array"` value; it does
  not filter other kinds;
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
area is aligned to that size, up to `stack_alignment`), and `"error"`
rejects the function's signature. A `"flatten"` value counts as one value.

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
registers takes two positions; `eabi32` aligns its 64-bit values to two
positions.

`stack_alignment`, a power of two, and `stack_size`, both in bytes,
override the alignment and the minimum size of a value that the rule places
on the stack. The outgoing argument area is aligned to the largest of the
entry's `stack_alignment` and the alignments of the values on the stack,
and its size is rounded up to that alignment.

A `"flatten"` rule may set `require_natural_alignment` and `merge_banks`.
With `require_natural_alignment = true`, the rule does not apply unless
every field offset and array stride meets the alignment of the nested
value. `merge_banks`, which requires `unit_bits`, divides the value into
`unit_bits` chunks and places each chunk in one register of the first bank
in the list that a field in the chunk uses, as when the System V ABI passes
a chunk that holds an `i32` and an `f32` in an integer register. A field
whose bank is not in the list makes the rule not apply, so a later rule such
as `"indirect"` or `"stack"` handles the value. A field that by itself fits
one register of a listed bank, such as a 128-bit vector, stays in one
register.

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
| `variadic_va_list_bytes` | integer | `0` | no |
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
  registers of that bank in the caller's stack at
  `variadic_home_base + index * variadic_home_stride`; the stride must not
  be 0.
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
| `alignment` | integer | `1` | no |
| `llvm_va_list_offset` | integer | none | no |

With `n` the number of positions of `cursor` that the named arguments use,
the kinds are:

- `"cursor_offset"`: the integer `base + n * stride`;
- `"cursor_address"`: the address `base + n * stride` in the incoming stack
  arguments, counted from the start of the caller's argument area, including
  its `argument_stack_base` bytes;
- `"register_save_address"`: the address `base + n * stride` in the area of
  `variadic_save_banks`, which must not be empty; without `cursor`, `n` is
  0; and
- `"stack_address"`: the address just past the stack arguments of the named
  parameters, rounded up to `alignment`, plus `base`.

`"cursor_offset"` and `"cursor_address"` need `cursor` and a nonzero
`stride`. `cursor` names a cursor, which need not be the name of a bank.
`alignment` is a power of two. `type` is the exact Cross scalar or pointer
type, without qualifiers, that the binding must have: typedefs and generic
arguments are resolved first, and no conversion applies. State names, and
the names of a definition's bindings, must be unique. A definition without
bindings reserves no storage for them.

`-emit-llvm` describes the `llvm.va_start` area with
`variadic_va_list_bytes`, `variadic_va_list_alignment`, and each state's
`llvm_va_list_offset`, which must be less than `variadic_va_list_bytes`.
These properties do not affect native code.

### Shipped ABIs

The shipped x86-64 model defines the default `cross` ABI and the C ABIs
`sysv_abi` and `ms_abi`. The shipped MIPS model defines the Cross ABIs
`cross32` and `cross64`, with 32-bit addresses, and `cross-n64`, with 64-bit
addresses, and the C ABIs `o32`, `n64`, and `eabi32`.
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
  location. For example, `u32 (*)(in u32 x, out u32 y)` under the `cross`
  ABI is `PF5_cross3_u324_auto6_caller0__2_i3_u324_autoo3_u324_autoE`.

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
| `inherits` | text | none | no |
| `targets` | list | `[]` | with `m.*` |
| `f.*`, `m.*` | option value | | no |

An entry inherits from at most one parent, named by `inherits`, which may
be declared anywhere in any loaded file; an inheritance cycle is an error.
A child replaces individual values of its parent, and options that no
entry of the chain sets keep their defaults.

A preset may set only the options that `cc --print-options` marks
`presettable: yes`. An entry with `m.*` properties needs `targets`, a list
of architecture names as printed by `cc --print-targets`; each `m.*`
property must be an option of every listed architecture, and selecting the
entry for another architecture is an error. The presettable target options are
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
    abi = "cross";
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

`debug` names the entry `-g` selects. `fp_traps` lists the floating-point
exceptions the program runs with enabled, from `invalid`, `divide-by-zero`,
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
| `format` | text | none | `dwarf` is the only shipped emitter. |
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
- `--print-abis` lists the ABIs of the selected target with their aliases
  and whether they are selectable, and the target's default ABI; it does
  not reflect `-mabi`.
- `--print-manglings` lists the manglings.
- `--print-profiles` lists each profile's target, ABI, mangling, preset,
  and option settings.
- `--print-optimizations` lists each preset, its parent, and its settings.
- `--print-options[=all|optimization|common|target]` lists each option
  with its value for the compilation, its origin (`default`, `preset`,
  `profile`, or `command-line`), whether presets may set it, and the model
  file and line that set it.

Only `--print-options` shows the values in effect for a compilation.
Dependency output (`-M`, `-MD`) does not list model files; add them to the
dependencies of your build.

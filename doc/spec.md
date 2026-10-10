# The Cross Programming Language

Draft language specification, version 0.9

This is a pre-1.0 draft. Drafts do not promise source or binary compatibility
with one another; an implementation targets its advertised draft, not a mixture
of drafts. The compiler implements part of this draft; [targets.md](targets.md)
lists its current limitations.

## Scope and conformance

Cross (**x**) is a low-level ahead-of-time language. Source files use `.x`;
the public tools are `cpp` (Cross preprocessor) and `cc` (Cross compiler).
*Must*, *must not*, *shall*, and *shall not* are normative.

Cross resembles C, but no C or C++ standard is incorporated by reference. This
document defines source semantics, and [models.md](models.md) defines the
compiler-model format; together they are the Cross specification. The
compiler's options are described in [invoke.md](invoke.md). Syntax or behavior
not defined by the specification, a selected target description, or an explicit
implementation extension is invalid; familiarity with another language never
fills a gap.

Cross has no hosted environment or C++ object model. A target description is a
normative part of an implementation and supplies only the facts explicitly
delegated to it: ABIs, register files, instructions, object format, address
spaces, and data-layout details. For the shipped targets, the target description
consists of [targets.md](targets.md), the shipped models described in
[models.md](models.md), and the output of `cc --print-features`,
`--print-instructions`, `--print-registers`, and `--print-abis`.

### Core invariants

These rules override any less-specific text:

1. **Strictly standalone.** Cross supplies no header, library, runtime, startup,
   entry point, or reserved external symbol. Generated code contains no call or
   undefined helper symbol without an explicit source-level call.
2. **One compiler namespace, one attribute context.** Compiler callables,
   instructions, macros, queries, types, and stream markers are rooted at
   `$::`. Attributes are contextual names inside `[[...]]` and never use that
   prefix.
3. **Cross-to-Cross preprocessing.** `cpp` performs lexical inclusion,
   conditionals, and source-macro expansion. It preserves namespace and
   `using` syntax; semantic resolution and link-name construction belong to
   `cc`. Its output is Cross, never C.
4. **Forward by default.** A file-scope declaration allocates nothing unless it
   has an initializer/body or is a `static` or `global` object definition.
5. **Result-bearing parameters.** An omitted parameter mode is `in`. `in`,
   `out`, and `inout` select boundary copy-in/copy-out; `const` controls local
   immutability. Each parameter is a distinct cell and copy-out occurs only on
   normal return.
6. **Cross-first external ABI.** Dynamic ABIs exist only within a compilation
   group. Global, unresolved, and indirect calls use the target's stable Cross
   ABI by default. A platform C ABI or complete manual ABI is explicit.
7. **Typed machine access.** Hardware built-ins expose target semantics and
   constraints to optimization; runtime-free intrinsics emit no call and at
   most a documented finite inline sequence.
8. **Target facts are normative.** Unsupported target-dependent operations are
   diagnosed; they are not implemented by hidden runtime fallback.
9. **Stage-polymorphic functions.** An ordinary visible call may be evaluated
   during translation when its inputs and effects permit; otherwise it remains
   a runtime call. `$::eval`, `$::runtime`, `eval_only`, and
   `runtime_only` make that choice mandatory.
10. **Explicit generation.** Source generation occurs at a visible `name!`
    invocation or a lexically activated syntax prefix. `$::quote` and
    `$::unquote` construct a replacement inside its declared boundary; they
    do not perform ambient rewriting.
11. **No host ambient state.** Translation-time code has no implicit access to
    files, environment variables, clocks, randomness, processes, networks, or
    compiler-private state. `$::embed` is the sole core operation that reads one
    explicitly named, dependency-tracked host file.

### Terms

| Term | Meaning |
| --- | --- |
| primary input | One `.x` file named to `cc`, including textually included files. |
| source unit | One primary input after preprocessing; bounds `static` visibility. |
| compilation group | Source units producing one combined output; bounds dynamic ABI and undecorated-definition visibility. |
| visible implementation | A compatible definition resolvable for a direct call in the compilation group. |
| normal return | Return through the managed Cross epilogue, including fallthrough from `void`; excludes traps, non-local transfers, and raw returns. |
| translation-time value | A typed value known by `cc` without executing the output program. |
| translation-only function | An explicit `eval_only` function, procedural macro, syntax expander, or translation-only helper (a function whose signature uses a `$::meta` type); it runs only during translation. |
| runtime expression | An expression emitted into the output program rather than executed by `cc`. |
| token tree | A balanced delimiter group and its contents; each construct specifies its permitted outer delimiters. |
| compiler model | A declarative ABI, mangling, optimization, profile, or debug entry loaded from a model file ([models.md](models.md)); never Cross source or executable code. |
| registered ABI | An ABI defined by a loaded compiler model, as opposed to a dynamic or manual ABI. |
| target registry | The facts about the selected target that `cc` and the loaded models provide: register views, instruction forms and their effects, address spaces, ABIs, and options. `cc --print-registers`, `--print-instructions`, `--print-abis`, and `--print-options=target` list them. |

### The language in one page

This example uses no implicit library or runtime:

```x
static u64 square(in u64 value) {
    return value * value;
}

global u64 answer = square(6) + 6; // `cc` stores 42

global u64 calculate(in u64 input) {
    u64 known = square(8);          // normally becomes 64
    u64 later = square(input);      // remains a call when `input` is unknown
    return known + later;
}
```

These rules explain most Cross code:

| Source form | Meaning |
| --- | --- |
| `static` / `global` | Select source-unit-private / linker-visible linkage. Either spelling defines an object even without an initializer; a function still needs a body to be a definition. |
| `in` / `out` / `inout` | Copy a parameter in / out / both. An omitted mode means `in`; top-level `const` makes the callee's cell immutable. |
| `$::name` | Use a compiler-owned feature. Cross itself provides no ordinary function or library name. |
| `[[name(args)]]` | Apply a contextual compiler attribute; attribute names omit `$::`. |
| `$::eval(x)` / `$::runtime(x)` | Require translation-time evaluation / keep the operand as runtime code. Ordinary calls choose automatically. |
| `transform! { tokens }` | Give one explicit token tree to a user procedural macro and splice its result at that position. |
| `syntax flow::unless;` | Activate a declared syntax prefix for subsequent source in this lexical scope. |
| `$::embed("asset.bin")` | Read one declared file into translation-only bytes; materialize them in an eligible static byte array. |

Types say their size: `i32`, `u64`, `f32`, and so on. `iptr` and `uptr` are
pointer-width integers. `fptr` is the pointer-width *numeric floating type*; it
is not a pointer and does not mean “stored in a floating-point register.”
Physical register placement is an ABI/location choice independent of value
type.

### Canonical spelling for readers and generators

The following style is non-normative, but examples, formatters, and
source generators should follow it:

- write the parameter mode when an interface's dataflow should be immediately
  visible, especially for `out` and `inout`;
- use exact-width types and suffix interface-sensitive literals (`4u32`,
  `0iptr`);
- put one declaration or statement on a line and always use braces for a
  controlled statement;
- qualify expression/type compiler facilities with their complete `$::` name,
  but write contextual attributes without `$::`;
- use `$::eval` only when failure must stop translation and `$::runtime` only
  when the call must remain in emitted code; and
- keep generated syntax inside a visibly named `macro! { ... }` or explicitly
  activated syntax boundary.

These four forms make file-scope ownership explicit:

| Form | Meaning |
| --- | --- |
| `u64 next(in u64 value);` | Forward declaration; defines no function. |
| `u64 next(in u64 value) { ... }` | Definition visible within the compilation group. |
| `static u64 next(in u64 value) { ... }` | Definition private to one source unit. |
| `global u64 next(in u64 value) { ... }` | Definition exported to the linker. |

C syntax that this document does not define is not inherited. When adapting C or
generating Cross, use this direct mapping:

| Familiar spelling | Cross spelling |
| --- | --- |
| `char`, `short`, `int`, `long`, signed/unsigned combinations | An exact type such as `i8`, `u16`, `i32`, or `u64`. |
| `float`, `double`, `_Bool` | `f32`, `f64`, `bool`. |
| `extern T name;` | `T name;`; forward declarations need no storage keyword. |
| `__builtin_name(...)` | The documented `$::name(...)`, if Cross defines an equivalent. |
| `__attribute__`, `__declspec`, `_Alignas` | `[[attribute(...)]]`, for example `[[aligned(16)]]`. |
| `asm` / compiler-specific computed goto | Typed `$::_instruction` calls / a `label` value and `goto value;`. |
| An assumed libc or compiler helper | An explicit user declaration and definition or linked library. |

`cc --print-keywords`, `--print-builtins`, `--print-attributes`,
`--print-features`, `--print-instructions`, and the model and target queries
listed in [invoke.md](invoke.md#inspecting-the-compiler) provide the exact
implemented surface to tools. A generator should query a target-dependent
facility instead of guessing from the target triple.

### Grammar notation

Grammar uses EBNF: `[ x ]` is optional, `{ x }` repeats, `|` separates
alternatives, lowercase names name rules defined in this document, quoted text
is literal source, and `/* ... */` is descriptive text.

## Source text and lexical structure

Cross source is UTF-8. A compiler may ignore one UTF-8 byte-order mark at the
start of a file and must reject a byte-order mark elsewhere. NUL bytes are not
permitted. CRLF and CR are normalized to LF. Space, horizontal tab, vertical
tab, form feed, and newline are whitespace.

`//` begins a comment through the next newline. `/*` begins a comment through
the next `*/`; block comments do not nest. A comment is replaced by one space
before tokens are formed, except that its newlines remain newlines. Comments
are not recognized inside string or character literals.

After preprocessing directives are handled, source is a sequence of:

- identifiers;
- compiler built-in names beginning `$::`;
- integer, floating, character, and UTF-8 string literals;
- punctuators; and
- whitespace, which only separates tokens.

An identifier starts with an ASCII letter or `_` and continues with ASCII
letters, digits, or `_`. Unicode may occur in comments and strings, but not in
identifiers. Adjacent characters form the longest valid token. The punctuators
are:

```text
[ ] ( ) { } . -> ++ -- & * + - ~ ! / % << >> < <= > >=
== != ^ | && || ? : ; ... = *= /= %= += -= <<= >>= &= ^= |=
, # ## :: [[ ]]
```

`#` and `##` are valid only while processing source macros. `[[` and `]]`
delimit attributes; two separately whitespace-separated `[` or `]` tokens do
not. A source character that begins no token is an error.

Character and string escapes are `\'`, `\"`, `\\`, `\?`, `\a`, `\b`, `\f`,
`\n`, `\r`, `\t`, `\v`, one to three octal digits, and `\x` followed by one or
more hexadecimal digits. An escape value must fit the destination character
unit. A raw newline cannot occur inside a literal. Backslash immediately
followed by a normalized newline removes both characters before tokenization.

The following list is the complete set of reserved keywords:

```text
bool      break     case      const     continue   default    do
else      enum      f32       f64       f80        f128       for        fptr
global    goto      i8        i16       i32        i64        i128       if
in        inline    inout     iptr      label      namespace out
register  restrict  return    sizeof    stack      static    struct
switch    syntax    typedef   u8        u16       u32        u64       union
u128      uptr      using     void      volatile  while
```

A reserved keyword cannot name a declared entity, member, parameter, generic
parameter, tag, enumerator, label, or namespace.
No other word is reserved. In particular, `auto`, `char`, `double`, `extern`,
`float`, `int`, `long`, `short`, `signed`, `unsigned`, and every
underscore-prefixed compatibility spelling are ordinary identifiers, not type
specifiers or keywords. Compiler-controlled expression/type facilities use `$::`
names instead of consuming keywords. Attribute names are contextual and consume
no keywords either.

The punctuators `::` and `->` have Cross meanings described in this document.
`->` accesses a member through a pointer, as in C; after a function declarator
it introduces a result location.

### The built-in namespace

A built-in name has this lexical form:

```text
builtin_name = "$" "::" identifier { "::" identifier } .
```

Examples are `$::_movabs`, `$::_add`, `$::expect`, and
`$::target::special_operation`. `$::` is a compiler-owned root namespace, not
an ordinary namespace declaration. Every entry has a compiler-defined
category: hardware instruction, runtime-free intrinsic, predefined macro,
compile-time query, target-defined scalar type, compile-time meta type, or
preprocessed-stream marker. A full name has exactly one category and shall not
collide with an entry in another category.

Every compiler-provided callable operation, macro, query, and
compile-time/target type is named below `$::`. Attributes occupy only the
`[[...]]` grammar context and are listed separately. Core runtime types,
keywords, and punctuation remain language syntax rather than namespace
entries.

A core full name is never shadowed by a target entry. The final component of a
machine-instruction built-in begins with `_`, for example `$::_add`; compiler
intrinsics, queries, and macros do not use that prefix. Machine
built-ins remain instruction-category entries reported by
`$::has_instruction`.

Built-in names are reserved and cannot be declared, defined or redefined as
source macros, imported with `using`, or have their address taken. A program
shall not declare or reopen namespace `$`, and `using $;` is ill-formed. A
target may use nested names below `$::`, but all built-ins remain rooted there.

Cross defines no `__builtin_*` function namespace and reserves no identifier
merely because it begins with one or two underscores. Compiler-owned source
names use `$::` instead.

No other source identifier may contain `$`.

## Core program syntax and execution

This section defines the C-shaped part of Cross. Later sections refine it for
namespaces, parameter modes, ABIs, labels, translation-time execution, and
machine operations.

### Declarations and declarators

A declaration introduces one or more names with a type. A name is visible from
the end of its declarator; Cross has no implicit declarations. The common
forms read as follows:

```x
u32 value;                         // object
const u8 *text;                    // pointer to const u8
u8 buffer[256];                    // array
u64 hash(in const u8 *text);       // function
u64 (*operation)(in u64 value);    // pointer to function
typedef u64 word;                  // type alias
```

Parentheses in a declarator override binding. Postfix `[]` and `()` bind more
tightly than prefix `*`, so `T *a[N]` is an array of pointers and
`T (*p)[N]` is a pointer to an array. Every array bound except an omitted first
bound must be a positive integer expression. An omitted first bound in an
initialized array is inferred from the initializer; in a file- or
namespace-scope declaration without an initializer it declares an incomplete
array type, which indexing and decay may use, which `sizeof` may not, and
which a definition of the same object in the compilation group completes.
File-scope array bounds are translation-time values. A runtime block-scope
bound creates a variable-length array under the managed-frame rules.

`const`, `volatile`, and `restrict` qualify the type immediately built at their
declarator position. A `const` object cannot be changed through that qualified
lvalue. `volatile` and `restrict` have the contracts defined later.
`typedef` declares aliases, not objects. An alias does not create a distinct
type, except that an alias declared with `aligned` is distinct for layout only,
as described under alignment and packing.

`struct`, `union`, and `enum` tags occupy a tag-name space; a record or union
tag may carry a generic parameter list (see “Generic records and unions”). A
structure stores
members in declaration order; a union overlays all members at offset zero.
Members may be objects, arrays, records, or bit-fields, but not functions,
incomplete objects, or variable-length arrays. A record is complete after its
closing `}`. A pointer may refer to an incomplete type; an object cannot have
incomplete type.

```x
struct point {
    i32 x;
    i32 y;
};

union bits {
    u64 integer;
    f64 floating;
};
```

Objects have size, alignment, storage duration, lifetime, and a sequence of
`u8` object-representation bytes. Distinct live non-bit-field objects occupy
disjoint bytes unless they are union members. Padding bytes have unspecified
values. Reading an indeterminate non-byte value is undefined; copying its
representation through `u8` is permitted.

### Definitions and initialization

A block-scope object declaration is a definition. Its lifetime begins when
execution reaches the declaration and ends when its block exits. A file-scope
definition and a block-scope `static` definition have static duration.
Linkage and the exact file-scope definition rules appear under
“Declarations, definitions, and linkage.”

A scalar initializer is one assignment expression. An aggregate initializer is
a brace-enclosed, comma-separated list. Entries initialize successive members
or elements; `.member = value` and `[constant_index] = value` select a
destination and reset the successive position after it. A nested brace list
initializes a nested aggregate. Missing members/elements and padding are zero
initialized, except that a brace initializer of a structure declared
`exhaustive` must initialize each named member, by position or by designator.
Excess entries, duplicate designators, invalid member names, and out-of-range
indices are errors.

For an automatic variable-length array, the bound is evaluated once before
initialization. The runtime bound must include every explicitly initialized
element (including the terminating zero of a direct `u8` string initializer);
otherwise execution traps before initializer expressions are evaluated. The
whole runtime extent is zero initialized before explicit entries are applied
in source order.

```x
struct point { i32 x; i32 y; };
struct point origin = { .x = 0, .y = 0 };
u32 powers[4] = { 1, 2, [3] = 8 };
```

An array of `u8` may be initialized by a UTF-8 string when the array is large
enough for its bytes and terminating zero; an omitted bound is inferred.
Static-duration initializers must be translation-time values or
target/object-format relocatable address expressions. No initializer invokes a
hidden initialization routine. A static-duration object whose value is all
zero bits may be placed in storage the loader zeroes; `-fzero-init-in-data`
places every such object, including a thread-local one, with the initialized
data instead, so an image patcher can change it.

### Values, conversions, and casts

An **lvalue** designates an object or function. Except as the operand of `&`,
`sizeof`, `$::alignof`, or assignment's destination, an object lvalue is read
and becomes a value. An array lvalue becomes a pointer to its first element
except under `sizeof`, `&`, and string-array initialization. A function name
becomes a function pointer except under `&`.

Assignment converts the source to the destination type. A cast `(T) value`
requests the same conversion explicitly. Conversions are permitted between
arithmetic types, between compatible pointer types, between `void *` and
object pointers, and between pointers and sufficiently wide integers as
specified below. An explicit cast may also convert between any two
function-pointer types; calling through a pointer whose callable interface
differs from the called function's own is undefined unless the target
documents that the two interfaces are physically identical. A scalar converts to `bool` as false when it is numeric zero
or null and true otherwise. Converting to a narrower integer keeps the low
bits; converting an unsigned integer wider zero-extends it; converting a signed
integer wider sign-extends it. Floating/integer conversion truncates toward
zero and is undefined when the finite result is not representable.

An expression of type `void` has no value. It cannot supply an initializer,
assignment source, value-producing cast, `in`/`inout` argument, or unnamed
variadic argument. A void expression statement and void return forwarding keep
their effects without a value; an `out` non-lvalue actual is evaluated only for
effects as specified below. These source constraints also apply in untaken
branches and unevaluated operands.

Initialization, assignment, arguments, return, and parameter copy-out all use
assignment conversion and permit arithmetic narrowing. An implicit pointer
conversion may add pointee qualifiers but cannot discard `const` or `volatile`,
change atomic qualification, or convert unrelated pointee types except through
`void *`. Adding nested pointee qualifiers requires `const` on every intervening
destination pointer; for example, `T **` cannot become `const T **`.

### Conditional pointer result types

After ordinary value conversions, `condition ? yes : no` with two pointer
arms has the least qualification-preserving common pointer type described
below. The result type does not depend on the condition's value or arm order;
both arms must satisfy its source constraints even when one is not evaluated.

- Corresponding pointee types must have the same structure, nominal identity
  and array/vector bounds. A genuinely incomplete array is not a wildcard for
  a fixed bound. An extent that requires an expansion context defers only the
  dependent compatibility check; independently different types still fail.
- At corresponding pointee levels, `const` and `volatile` are combined. If a
  deeper qualification is added, every intervening destination pointer is also
  `const`, as required by implicit qualification conversion. Thus `T **` and
  `const T **` join as `const T *const *`, not `const T **`. Atomic
  qualification must agree. No new `restrict` contract is inferred: nested
  `restrict` is retained only where both types have it; top-level value
  qualifiers do not qualify the result pointer value.
- If an arm is already a pointer to `void`, an object-pointer arm may join it
  as a pointer to correspondingly qualified `void`. This bridge applies only
  at the immediate pointee level, never between function and object pointers
  or recursively between `T **` and `void **`. Unrelated object pointers do
  not implicitly become `void *`; an explicit cast can request that choice.
- Function-pointer arms must have the same complete canonical callable
  interface, including ABI, modes, endpoints, cleanup and clobbers. A
  conditional does not choose a function adapter or erase an interface
  difference.
- The outer address space must be the same, or the selected target registry
  must provide a unique least common destination through its declared implicit
  conversions. Nested address spaces must agree. Equal pointer widths alone
  never establish a conversion or common space. Ambiguous or unavailable joins
  require an explicit cast; representability and supported lowering remain
  target requirements.

With one pointer arm and one integer arm, the integer arm must be a
source-constant integer zero (see “Pointer and object-access model”). The result
is the pointer arm's unqualified value type and uses that type's target-defined
null representation. A nonzero or runtime integer does not establish a common
pointer type.

### Expressions and sequencing

The following table is ordered from tightest to loosest binding. Operators on
the same row associate left-to-right except unary, conditional, and assignment,
which associate right-to-left.

| Class | Forms |
| --- | --- |
| primary/postfix | `name`, literal, `(e)`, `e[a]`, `e(args)`, `e.m`, `e->m`, `e++`, `e--`, generic `name<...>` or `name::<...>` |
| unary | `++e`, `--e`, `&e`, `*e`, `+e`, `-e`, `~e`, `!e`, `(T)e`, `sizeof e`, `sizeof(T)` |
| multiplicative | `* / %` |
| additive | `+ -` |
| shift | `<< >>` |
| relational | `< <= > >=` |
| equality | `== !=` |
| bitwise AND/XOR/OR | `&`, then `^`, then `|` |
| logical AND/OR | `&&`, then `||` |
| conditional | `condition ? yes : no` |
| assignment | `= *= /= %= += -= <<= >>= &= ^= \|=` |

The six unary operators `&`, `*`, `+`, `-`, `~` and `!` accept a cast expression
as their operand, so `-(u32)value` and `*(u32 *)address` need no extra grouping.
Prefix `++`/`--` and expression-form `sizeof` still take unary expressions.
This grammar does not make a cast result an lvalue or relax operand type rules.

Comma separates declarations, arguments, initializers, generic parameters, and
the expressions of a `for` clause; it is not a Cross expression operator.

Operands and call arguments are evaluated left-to-right. Assignment first
evaluates and remembers its destination, then evaluates its source, then
stores. `&&` evaluates its right operand only when its left operand is true;
`||` only when its left operand is false; `?:` evaluates only its selected
arm. A completed full expression is sequenced before the next full expression.
No operand or argument evaluation is unsequenced.

Arithmetic uses the promoted common type. Integer division truncates toward
zero and remainder has the dividend's sign. Bitwise operators require
integers. A shift requires integer operands; its result has the promoted left
type. Comparisons and logical operators produce `bool`. Prefix update returns
the new value; postfix update returns the old value. Assignment and compound
assignment return the stored lvalue value.

`sizeof(T)` and `sizeof expression` produce `uptr` and do not evaluate the
expression, except that a runtime variable-length-array bound is evaluated when
its type is formed. `sizeof(void)`, `sizeof(function)`, and `sizeof` an
incomplete or scalable-vector type are invalid.

### Operator binding

Cross has no function overloading. A uniquely named ordinary function may bind
one operator spelling to one exact nominal signature:

```x
enum Count [[underlying(u32)]] {
    count_zero,
};

[[operator("+")]]
global enum Count add_counts(
    in enum Count left, in enum Count right) {
    return left;
}

global enum Count combine(
    in enum Count left, in enum Count right) {
    return left + right; // exactly add_counts(left, right)
}
```

The binding key is the operator token, unary/binary arity, and the exact
adjusted operand types. At least one operand must be a direct nominal record,
union, or enumeration type. A pointer to a nominal type is still a pointer, so
a binding cannot replace arithmetic on core scalars or pointers. All operands
are ordinary automatic `in` parameters; the ordinary function result is the
operator expression's result.

Cross permits unary `+ - ~ !` and binary
`+ - * / % << >> & ^ | == != < <= > >=`. It does not bind assignment or
compound assignment, `&&`, `||`, `?:`, address-of, dereference, update,
subscript, call, member access, comma, or any control operation. A binding
function is nonvariadic and nongeneric and has no manual parameter/result
location. It remains directly callable, addressable when its linkage permits,
and compatible with `eval_only`, `runtime_only`, `always_inline`, or ordinary
automatic evaluation.

There is no conversion ranking, fallback candidate set, argument-dependent
lookup, hidden namespace search, or declaration-name overload set. Two
different functions cannot bind the same exact key. Operator binding resolves
before translation-time evaluation and lowers as an ordinary direct call, so
argument order, ABI, effects, standalone rules, and diagnostics are exactly
those of that call. If no exact binding exists, the built-in operator rules
apply.

### Statements and control flow

A function body and every explicit block are compound statements:

```text
{ declaration-or-statement* }
```

A declaration is permitted wherever a statement is permitted. Its name is
visible through the end of that block. Inner declarations may shadow ordinary
outer names; labels use their function-wide label space and are not shadowed.

The statements are:

```text
;
expression ;
{ ... }
if (expression) statement [ else statement ]
switch (expression) statement
while (expression) statement
do statement while (expression) ;
for ([declaration-or-expression-list] ; [expression] ; [expression-list]) statement
break ;
continue ;
return [expression] ;
goto target ;
label:
case constant_expression:
default:
```

Conditions use scalar truth conversion. `while` tests before its body;
`do` tests after it; `for` executes initializer once, then test, body, and
increment in that order. The initializer and increment clauses of `for` may
list several expressions separated by commas, evaluated left to right. `break` exits the nearest loop or switch. `continue`
starts the next iteration of the nearest loop. A non-`void` return requires a
convertible value; a `void` return has none. Falling off a `void` function is a
normal return. Other fallthrough from a value-returning function is undefined.

A switch controlling value is promoted to an integer or enumeration type.
Each case is a distinct representable translation-time constant; at most one
`default` occurs. In a translation-only function, a case value that depends on
the invocation is computed, and checked against the other cases, each time the
switch executes. Control transfers to the selected label and then proceeds
normally until another transfer. A case/default label belongs to the nearest
enclosing switch. A switch shall not transfer control into the scope of a
variably modified object: when a case or default label lies in such a scope,
the switch statement lies in it too.

The selector is evaluated once. If no case matches, control enters `default`,
or leaves the switch if no default exists. These semantics apply at every
optimization setting. A compiler may omit a jump-table range check only when
it proves the index valid or the excluded path undefined. Use
`$::assume(op < 10u32)` for an externally established unsigned range, or
`default: $::unreachable();` when every unmatched value violates the contract.
Neither form inserts a runtime check; a false promise is undefined behavior.

`goto` and label values have the additional rules in “Labels, code addresses,
and computed goto.” Cross has no `asm`, exception, class, method-call, general
function overload, or implicit coroutine statement.

### Invalid and undefined programs

A **constraint violation** is a source error and requires a diagnostic.
**Undefined behavior** is reached only by executing a well-formed operation
whose stated runtime precondition is false, such as an invalid dereference,
division by zero, signed overflow, or invalid shift. `cc` must diagnose
undefined behavior that it necessarily reaches during required
translation-time evaluation. At runtime the compiler may assume it is not
reached. A target-defined behavior must be documented by the selected target;
an unspecified value may vary without documentation but still obeys every
stated type and safety constraint.

## Types and object model

Cross has records, arrays, pointers, functions, labels, enumerations, and these
scalar keywords. One byte is eight bits.

| Types | Representation |
| --- | --- |
| `bool` | Eight bits; values 0 and 1. |
| `i8/u8`, `i16/u16`, `i32/u32`, `i64/u64`, `i128/u128` | Exact width, no padding; signed range -2^(N-1)..2^(N-1)-1, unsigned range 0..2^N-1. |
| `iptr/uptr` | Signed/unsigned object-pointer width; alias an exact-width type when equal. |
| `f32/f64` | IEC 60559 binary32/binary64. |
| `f80` | x87 extended format: 1 sign bit, 15-bit biased exponent, and an explicit 64-bit significand. |
| `f128` | IEC 60559 binary128: 1 sign bit, 15-bit biased exponent, and 113 bits of precision. |
| `fptr` | Pointer-width numeric floating type: exactly `f32` for 32-bit object pointers and `f64` for 64-bit object pointers; another pointer width requires a target-defined format or a diagnostic. |

A target-registered scalar built-in name, such as
`$::target::patch_address`, is also a type specifier and may appear wherever a
scalar `type_name` is accepted. Its target entry defines representation,
alignment, conversions, permitted operations, and ABI classification. Merely
registering another kind of built-in below `$::target` does not make that name
a type.

Integers are two's-complement; unsigned arithmetic is modulo 2^N. Signed
overflow, minimum/-1 division, invalid shift counts, and unrepresentable signed
left shifts are undefined; right shift of a negative value is target-defined.
The x86-64 and MIPS targets define it as arithmetic (sign-extending)
right shift, including during translation-time evaluation.
Wrapping/saturating/checked operations require user code or instructions. The
registered option `-fwrapv` instead makes signed `+`, `-`, `*`, and `<<` wrap
modulo 2^N, at runtime and during translation-time evaluation alike; division
of the minimum value by -1 and invalid shift counts remain undefined.

`bool`, 8-bit, and 16-bit integers promote to `i32`. Other integer ranks
increase with width; equal-width signed/unsigned types have equal rank;
pointer-width integers use the rank of their resolved width. Two integer
operands choose a common type as follows:

1. after promotion, equal types remain unchanged;
2. equal signedness selects the higher rank;
3. if the unsigned type's rank is at least the signed type's rank, select the
   unsigned type;
4. otherwise select the signed type when it represents every value of the
   unsigned type, or the corresponding unsigned type when it does not.

Floating rank is exactly `f32 < f64 < f80 < f128`; `fptr` is resolved to its
target numeric type before ranking. Two floating operands use the higher rank.
Mixed integer/floating operands first convert the integer to the floating
operand's type. An implementation shall diagnose an operation for which the
target has no runtime-free lowering.

`i128` and `u128` are ordinary scalar integers, not SIMD vectors. Their object
representation, arithmetic, conversions, comparisons, atomics when registered,
and ABI classification follow the ordinary integer rules. A target without a
single 128-bit arithmetic instruction shall use a finite inline instruction
sequence or diagnose the operation; it shall not call a helper. Cross defines
no scalar 256-bit integer. Data-parallel 128/256/512-bit operations use fixed
vector types.

`f80` is the x87 binary extended value format, not a spelling for a platform C
`long double`. Its normal exponent range is -16382 through 16383 and its
precision is 64 bits. It has signed zeros, subnormals, infinities, and NaNs. A
target registers object allocation size, alignment, and padding independently
of the 80 value bits. Noncanonical x87 encodings are not `f80` values.

Ordinary floating expressions round to their nominal result type using
round-to-nearest, ties-to-even; Cross grants no implicit excess precision. The
floating environment is a property of the selected profile
([models.md](models.md#profiles)): which exceptions trap, whether a denormal
operand traps, and whether a denormal result flushes to zero. The shipped
profiles declare a masked environment with gradual underflow, which is not
source observable. Under a profile that declares traps or flushing, an
operation that may raise an enabled trap is executed only where the source
executes it, never speculatively, and translation-time evaluation follows the
declared environment: a result flushes to zero where the profile says so, and
an operation that would trap is not folded, so a mandatory evaluation of it
is diagnosed. `-ffast-math` never introduces an operation that can trap where
the source has none. Machine operations that change x87 precision, rounding, or control
state must restore the target default before ordinary floating code or a
managed call. Cross never inserts a hidden floating-environment helper.

`f128` always denotes binary128, independently of a platform `long double`.
Its literals, object layout, conversions, and ABI classification are available
when the target registers them. Arithmetic is available only when the selected
target supplies a finite inline implementation and
`$::feature::binary128_arithmetic` is present. Storage and bit-preserving copy
do not by themselves require that feature.

### Literal types

| Literal | Type |
| --- | --- |
| Unsuffixed decimal integer | First representable of `i32, i64, i128`. |
| Unsuffixed binary/octal/hex integer | First representable of `i32, u32, i64, u64, i128, u128`. |
| Integer with `i8/u8/i16/u16/i32/u32/i64/u64/i128/u128/iptr/uptr` suffix | Named type; value must fit. |
| Unsuffixed floating | `f64`; suffix `f32`, `f64`, `f80`, `f128`, or `fptr` selects exactly. |
| Character | One character/escape, type `u32`; multicharacter is invalid. |
| UTF-8 string | `const u8[N]`, including zero terminator; adjacent strings concatenate. |

C's numeric suffixes (such as `u`, `l`, and `ul`) and prefixed or wide strings
do not exist. Cross provides no implicit Boolean-literal names: `true` and
`false` are ordinary identifiers that user code or source macros may define if
desired.

### Enumerations

Omitting a record, union, or enumeration tag name requires a definition body.
Such a definition introduces a distinct nominal type without a lookup name
and cannot have a generic parameter list.
Declarators sharing that definition share its type; a separate definition is
distinct even if its members and representation are identical. A typedef may
name the type. Omitting an enumeration's tag does not suppress its enumerator
names: they bind in the declaration's ordinary scope.

An enumeration uses `i32` unless `[[underlying(T)]]` selects another
non-`bool` integer type. Values must fit; constants have the enumeration type;
all declarations agree. The first implicit value is zero and later implicit
values increment the predecessor; duplicates are allowed. Representation,
promotion, arithmetic, bit-fields, and ABI follow the underlying type.

### Bit-fields

A bit-field base is `bool`, an integer, or an enumeration. Width is a
nonnegative integer constant no greater than the base width; zero width requires
an unnamed field. Its declared type fixes signedness and shall not request
alignment. Atomic bit-fields are invalid; allocation, ordering, crossing, and
zero-width effects are target ABI facts.

### Size, alignment, and translation checks

Layout and aggregate passing are target properties. `sizeof` returns `uptr`;
`$::alignof` returns an unevaluated `uptr`; `aligned` requests minimum
alignment. `$::static_assert(constant, string);` requires a nonzero scalar constant and
emits no code. At file or namespace scope and in an ordinary function it is a
declaration-time check: parameter and local values are unavailable, untaken
branches and unused nongeneric functions do not suppress it, and a generic
function's
assertions are checked for each instance. In a translation-only function (an
explicit `eval_only` function, procedural macro, syntax expander, or
translation-only helper) it is instead a statement that executes each time it
is reached,
with that invocation's parameters, locals, and generic arguments, under the
invocation's resource limits; an unreached assertion keeps its source
constraints but is not evaluated.

Atomic and thread-local facilities are attributes;
generic selection and underscore-prefixed compatibility aliases do not exist.

### Function types

A function type includes result/parameter types, each fixed parameter mode and
manual endpoint, result location, stable ABI, clobbers, and cleanup ownership;
parameter names are excluded. Differences are incompatible after ABI aliases
are canonicalized. `()` and `(void)` both mean a zero-parameter prototype;
unprototyped and identifier-list functions do not exist.

A named ordinary parameter becomes visible after its declarator, including in
later parameters, following result-declarator suffixes and header attributes,
and the body of a definition. Each function declarator has its own parameter
scope; nested callable parameters do not enter the surrounding declarator's
scope, and comma-separated sibling declarators do not share parameter names.
Unevaluated type queries such as `sizeof(input)` may use an earlier parameter's
type in a fixed bound. This does not make that parameter's runtime value a
translation-time constant, or give ordinary parameters the header-wide scope
of generic parameters. Identifiers taken from a macro or syntax-extension input
keep the lookup context they were captured with.

Top-level `const` on an `in` parameter qualifies only the callee's local
parameter cell and is omitted from callable identity, ABI classification, and
mangling descriptors. The defining declarator supplies that cell's local
qualification; another declaration may spell it differently. Qualification of
a pointed-to type remains part of parameter type and compatibility. Other
qualifiers retain their ordinary type and access rules. Likewise, the `aligned`
request of a typedef used as a parameter or result type applies only to the
parameter's callee cell or to the object that receives the result: callable
identity, ABI classification, mangling descriptors, deduction, and
function-pointer compatibility use the base types.

Declarations, definitions, and pointer-to-function declarators can state the
same complete callable contract. A parameter location follows its declarator;
`-> "location"` follows the function suffix to which it belongs. `abi`,
`clobber`, and `stack_cleanup` on a function type describe that callable
component, including when it is nested under a pointer or `typedef`. When
multiple callable components are nested, an attribute after a function suffix
binds to that suffix; a leading callable attribute is valid only when it
selects one unambiguous callable component. Function-only body/code-generation
attributes do not become part of a pointer type. The location strings and
clobber resources are validated against the selected target and ABI model,
never assumed to name a default-profile register.

```x
typedef i32 (*transform_fn)(
    in u64 source "rdi",
    out u64 flags "rdx"
) -> "eax" [[abi("sysv_abi"), clobber("memory")]];
```

The spellings in this example are target-dependent. A target may instead
transport the ordinary result through any declared register, stack endpoint,
or indirect-memory channel. A fixed or address-observable callable interface
is not rewritten by dynamic-ABI optimization. A complete type permits
compatibility checking; it does not imply that an unavailable adapter or tail
transfer can be synthesized.

### Storage selection

| Form | Storage contract |
| --- | --- |
| `T x;` | Compiler-selected automatic storage; may spill. |
| `register T x;` | Register-class while live; no address; cannot spill merely for pressure. |
| `stack T x;` | Distinct addressable managed-frame slot with automatic lifetime. |
| `register T x "reg";` | Hard-bound target register view. |

`register` and `stack` are block-only; `stack` cannot combine with `register`,
`static`, or `global`.
Intersecting hard bindings may not overlap. A hard binding affects storage, not
the function ABI. Impossible register allocation is diagnosed.

## Pointer and object-access model

`&lvalue` produces a pointer to the designated object or function; `*pointer`
designates its target. Object-pointer addition/subtraction scales by the
complete pointed-to type. Within one array object, pointers may range from its
first element through one past its last element. The one-past pointer may be
formed, compared, and subtracted but not dereferenced. Subtracting two pointers
requires positions in the same array and yields `iptr`; relational pointer
comparison has defined ordering only within that array. Equality also compares
null pointers and addresses outside a common array.

Unary `*` requires a pointer to an object or function type, not `void`. Forming
an incomplete object designator does not read its representation; for example,
`&*p` may retain an incomplete-record pointer. An object value read requires a
complete object type. Function designators remain usable as callees or under
address-taking without an object read. Subscript selection requires a complete
object element type because its offset scales by that type, even when the
selection appears under `&`, `sizeof`, or `$::alignof`. These source constraints
apply before execution, including untaken branches and unevaluated operands.

Integer constant zero converted to a pointer is a null pointer. `==` and `!=`
permit a pointer operand and an integer constant zero in either order; the zero
is converted to the pointer operand's type before comparison. Other mixed
pointer/integer comparisons are invalid. A null pointer
compares unequal to every pointer to an object/function. Object pointers
convert to/from `void *` without changing their address and recover their
original value when converted back. Function and object pointers are distinct;
conversion between them, and explicit conversion between a function pointer
and `uptr`, exist only when the target defines them. The shipped x86-64 and
MIPS targets define the `uptr` conversion as the code address, like `label`.

An object's lifetime begins after its storage and initialization are
established and ends when its block exits, its dynamic stack region is
restored, or program execution ends for static storage. Using a pointer to
access storage outside the pointee object's lifetime is undefined.

An object pointer contains a target address and an address-space identity.
Converting an object pointer to `uptr` produces its target address. Converting
that value, or any representable integer value, to an object pointer produces
an address-derived pointer in the destination type's address space. Cross does
not attach hidden allocation provenance to an address-derived pointer and an
optimizer shall not assume that two pointers with the same address and address
space cannot alias merely because they were derived differently. A pointer to
integer type narrower than `uptr`, or an integer to pointer conversion whose
value is not representable, has target-defined results.

Dereferencing a pointer is valid only when all of the following hold:

- the address range is mapped for that access by a live Cross object or by a
  target, linker-script, operating-system, or device-memory contract;
- the pointer satisfies the pointee type's alignment without its typedef
  `aligned` request, which neither raises this requirement nor asserts
  pointee alignment, unless the access is explicitly declared unaligned by a
  target built-in;
- the address space permits the requested read, write, or instruction fetch;
- the access follows the effective-type and qualifier rules below.

Thus forming a pointer such as `(volatile u32 *)0x40001000` is defined; whether
reading it is valid is a property of the selected platform. Forming or
comparing an invalid address is not itself an access. Null representations and
the effect of dereferencing a null pointer remain target-defined only where the
target contract explicitly maps address zero; otherwise null dereference has
undefined behavior.

### Effective type and aliasing

An object declared with a type has that effective type for its lifetime. Raw
allocated storage acquires an effective type when written through a non-byte
lvalue. Storage written only through `i8` or `u8`
lvalues has no acquired effective type; for a later non-modifying access its
effective type is the lvalue type used for that access. An lvalue access must
otherwise use a compatible type, the corresponding signed or unsigned type,
an aggregate containing such a type, or an `i8` or `u8` type. The byte types
may inspect or modify any object representation.

Reading a non-active union member is a defined representation reinterpretation
when the bytes form a valid value of that member and its alignment is
satisfied. Otherwise the behavior is undefined. User code may instead copy
object-representation bytes through `u8` arrays; no library routine has special
effective-type status merely because it is named `memcpy` or similarly.

During each execution of a block in which a `restrict`-qualified pointer is
declared, if an object is modified through a value based on that pointer, every
access to the same object in that block must use a value based on that pointer;
otherwise behavior is undefined. “Based on” includes copies, pointer
arithmetic, and conversions that preserve the address. For a restricted
parameter, the block is that call and the contract concerns the copied
parameter value; it does not grant exclusivity over the caller object used only
as an `out` or `inout` result destination. `[[may_alias]]` disables
effective-type alias assumptions for accesses through the attributed type, but
does not relax alignment, lifetime, address-space, atomic, or volatile rules.
The attribute follows a typedef's declarator or a record's tag in its
definition, or qualifies a type in a declaration specifier or type name; on a
record definition it qualifies every use of that record type, and accesses to
members and elements of a `may_alias` aggregate are accesses through it. It does not make a distinct
type: compatibility, callable identity, deduction, and mangling ignore it.

A typed memory operand of a `$::_mnemonic` form performs an access through that
operand's lvalue type. An instruction form explicitly documented as a raw or
unaligned memory operation instead uses its target-defined byte access and
does not establish an effective type. Its volatile, atomic, ordering, and
fault behavior must be present in the target registry.

## Atomic and concurrent access

Cross itself creates no threads, but user or platform code may execute Cross
functions concurrently. Within one thread, the expression/statement sequencing
rules establish **sequenced-before**. Two memory accesses **conflict** when
their byte ranges overlap and at least one writes. A release operation
synchronizes with an acquire operation that reads its value (or its release
sequence). **Happens-before** is the transitive closure of sequenced-before and
synchronizes-with. Conflicting accesses in different threads form a data race
when at least one is non-atomic and neither happens before the other; a data
race is undefined behavior. `volatile` does not prevent it.

An atomic-qualified type is written `T [[atomic]]`. An atomic object has one
per-object modification order observed consistently by all threads and shall
be accessed only by atomic operations, except during initialization before it
is published. Its lvalue-to-value conversion is a sequentially consistent
atomic load; simple assignment is a sequentially consistent atomic store; and
compound assignment or increment/decrement is a sequentially consistent atomic
read-modify-write.

The following compiler constants select ordering:

| Order | Contract |
| --- | --- |
| `$::memory::relaxed` | Atomicity and modification order only. |
| `$::memory::acquire` | Later operations cannot move before a successful load/read. |
| `$::memory::release` | Earlier operations cannot move after a store/write. |
| `$::memory::acq_rel` | Both acquire and release; read-modify-write only. |
| `$::memory::seq_cst` | Acquire/release as applicable plus one total order of all sequentially consistent operations. |

Portable runtime-free atomic intrinsics are:

```text
$::atomic_load(pointer, order)
$::atomic_store(pointer, value, order)
$::atomic_exchange(pointer, value, order)
$::atomic_compare_exchange(pointer, expected_pointer, desired,
                            success_order, failure_order)
$::atomic_fetch_add(pointer, value, order)
$::atomic_fetch_sub(pointer, value, order)
$::atomic_fetch_and(pointer, value, order)
$::atomic_fetch_xor(pointer, value, order)
$::atomic_fetch_or(pointer, value, order)
$::atomic_thread_fence(order)
$::atomic_signal_fence(order)
$::atomic_is_lock_free(type-or-expression)
```

The pointer shall point to a suitably aligned atomic-qualified scalar.
`$::atomic_load` returns the read value; `$::atomic_store` returns `void`;
`$::atomic_exchange` and fetch operations return the value before modification.
Fetch arithmetic/bitwise operations require integer objects.
Returned scalar values have the object's unqualified value type; store and
desired-value arguments use ordinary assignment conversion to that type.
Argument counts, pointer/value types and order constraints apply in untaken
branches and unevaluated operands without executing an atomic access.

Compare-exchange compares the object with `*expected_pointer` bit-for-bit. On
success it writes `desired` and returns true. On failure it writes the observed
object value to `*expected_pointer` and returns false. Failure ordering cannot
be `release` or `acq_rel` and cannot be stronger than success ordering.
The expected pointer designates a modifiable, non-atomic object of the same
unqualified value type; it may retain volatile qualification.
A load accepts relaxed/acquire/seq_cst; a store accepts
relaxed/release/seq_cst; read-modify-write accepts every order.

`$::atomic_thread_fence` orders thread-visible memory according to its order.
`$::atomic_signal_fence` is a compiler ordering barrier and emits no instruction
unless the target requires one. `$::atomic_is_lock_free` is a translation-time
`bool` query for the selected type, size, alignment, and target.
Its operand is a type name or one unevaluated scalar expression; no object read,
array/function decay or call execution occurs. Ordinary type-name lookup,
qualification, generic substitution and custom-syntax precedence apply.

Every accepted atomic operation lowers to one instruction or a finite inline
sequence. It never calls a helper, allocates a lock, or refers to hidden state.
If an operation is unavailable for the selected type/alignment/address space,
`cc` diagnoses it. Target-specific coherence scopes and operations remain
available through typed `$::_mnemonic` forms.

An atomic object passed by value follows the normal Cross parameter-cell
model. Copy-in is an atomic load and copy-out is an atomic store, both
sequentially consistent. Operations on the callee's atomic-qualified parameter
cell do not operate directly on the caller's shared object. Pass an atomic
pointer as an `in` parameter to operate on shared storage:

```x
void increment(in u32 [[atomic]] *counter) {
#if $::has_intrinsic($::atomic_fetch_add)
    $::atomic_fetch_add(counter, 1u32, $::memory::relaxed);
#else
#error selected target has no implementation for increment
#endif
}
```

Combining `[[atomic]]` and `volatile` retains both contracts. Atomic object
size, required alignment, lock-free widths, cache-coherence scope, and any
address-space restrictions are target properties. Packed members cannot have
atomic type unless their actual offset satisfies the atomic type's required
alignment.

## Vectors and address spaces

Cross defines fixed and scalable vector types through type attributes. For
example:

```x
typedef u32 u32x4 [[vector_size(16)]];
typedef f32 float4 [[ext_vector_type(4)]];
typedef u32 scalable_u32 [[scalable_vector(4)]];
```

`vector_size(B)` creates a fixed vector whose total size is `B` bytes;
`ext_vector_type(N)` creates a fixed vector of `N` elements. The element type
must be a non-atomic integer, floating, or target-supported scalar type, and
the resulting size and lane count must be supported by the target. Fixed
vectors have ordinary object size and alignment. A vector's lane type carries
no `aligned` request; a vector's alignment is its natural one unless the vector
type itself is declared with `aligned`. Arithmetic, bitwise, shift,
and comparison operators apply lane-wise when meaningful for the element
type. A comparison lane contains all zero bits for false and all one bits for
true. Scalar-to-vector conversion splats the scalar only when the conversion
is otherwise permitted by the scalar rules.

`scalable_vector(N)` creates a target-scalable vector with at least `N` lanes.
It has no compile-time `sizeof`, cannot be a structure or union member, cannot
be an array element, and cannot be passed through an unprototyped or variadic
interface. It may cross a call only through a stable ABI that explicitly
supports that scalable type or through compatible manual locations.

Lane indexing is zero-based. An out-of-range fixed-vector constant index is
ill-formed; an out-of-range runtime index has undefined behavior.
Target-specific vector element types are advertised
through feature queries rather than assumed by portable Cross code.

An address-space-qualified pointee type uses
`[[address_space(N)]]`, where `N` is an address-space number from the target
registry:

```text
volatile u32 [[address_space(1)]] *device_status;
```

Address space is part of the pointer type and function type. Implicit
conversion between distinct non-generic address spaces is not permitted.
Conversions involving a target-declared generic address space follow the
target registry; all other conversions require an explicit cast and remain
valid only if the target declares the address representable in the destination
space. Each address-space entry specifies pointer width, null representation,
permitted operations, volatility defaults if any, and ABI lowering. Dynamic
ABI allocation may use such a type only when caller and callee use the same
address-space entry.

## Volatile access and data layout

### Volatile

A read or write through a volatile-qualified lvalue is a **volatile access**;
taking its address, `sizeof`, and `$::alignof` are not. Each scalar
non-bit-field access emits one target sequence covering exactly that object's
bytes. It is never deleted, duplicated, combined, or widened. A target lacking
the required width may split the access only with documented widths and order.

Aggregate and bit-field accesses may use multiple operations; a bit-field write
may touch its allocation unit. Volatile accesses preserve Cross sequencing,
including left-to-right argument order and ordering against instructions or
calls with a `"memory"` effect. `volatile` alone supplies no atomicity,
inter-thread synchronization, fence, cache maintenance, or device barrier.

Parameter copy-in/out performs the corresponding volatile access. A top-level
volatile parameter qualifies only its callee cell and changes no ABI rule. Each
source access to a hard-bound `volatile register` reads or writes that register;
other volatile objects may use any storage preserving these rules.

### Alignment and packing

| Attribute | Valid on | Effect |
| --- | --- | --- |
| `aligned(N)` | Objects, record definitions/members, typedefs, function definitions | Minimum alignment; on a function, entry alignment only. |
| `packed` | Complete record definitions and record members | Reduces each affected non-bit-field member's alignment to one byte unless raised by `aligned`. |

`N` is a positive power-of-two integer constant expression. Effective
alignment is the maximum of natural and requested alignments; repeated requests
take their maximum. A type whose effective alignment exceeds its size has its
size rounded up to that alignment, so arrays and record members of the type
keep every element aligned; `sizeof` reports the rounded size. A typedef
declared with `aligned` denotes a type that is distinct for layout only: its
size and alignment follow the request, and arrays and members of it use that
layout; its values convert, compare, and are passed and returned like the base
type, and the request qualifies storage only. `aligned` is invalid on
parameters, does not lower natural alignment, and does not assert a pointer's
pointee alignment. The frame, object writer, and linker must preserve every
accepted request or diagnose it.

A packed record treats each non-zero-width member as packed, but keeps the
alignment that a member's type requests, including a typedef request and the
request of an array member's element type. Members appear in declaration order
at the first offset satisfying effective alignment. Record alignment is the
maximum of one, member alignments, and explicit record alignment; size
includes required tail padding. Union members remain at offset zero and union
size is its largest member rounded to union alignment.

Packing is not recursive: a nested record keeps its internal layout even if its
containing member is under-aligned. Such rooted member access is valid and uses
an unaligned-safe sequence. Packed non-zero-width bit-fields have one-bit
minimum allocation alignment; zero-width fields and all allocation-unit details
follow the target bit-field ABI.

`packed` and `aligned(N)` may be combined: packing controls members while
alignment controls the completed record. Repeated `packed` is idempotent;
it is invalid on standalone objects, functions, parameters, pointers, typedefs,
or incomplete records.

Direct packed-member access is valid. Forming a pointer to a potentially
under-aligned member requires a diagnostic; dereferencing it is undefined
unless the actual address satisfies the pointed-to type. A volatile packed
access obeys both contracts and may split, so it does not promise one bus
transaction.

Type-level layout attributes participate in compatibility and external ABI
classification. Interoperating declarations must agree exactly on size,
alignment, offsets, bit-fields, and packing.

## Namespaces and lookup

### Qualified names

Namespace components use portable ASCII identifier spelling:

```text
identifier       = letter { letter | decimal_digit } .
letter           = "A" ... "Z" | "a" ... "z" | "_" .
decimal_digit    = "0" ... "9" .
namespace_name   = identifier { "::" identifier } .
qualified_name   = identifier "::" identifier { "::" identifier } .
```

A qualified name is accepted wherever an unqualified identifier of the same
grammar role is accepted, including declarations, declarators, type names,
expressions, tags, and supported macro directives. There are no absolute names
beginning with `::`.

For a qualified reference, the first component is resolved from the current
namespace context. Lookup tries the current namespace from innermost to
outermost, then active imports, then the written global spelling. Once a
complete qualified name matches, its final component is not subjected to a
second unqualified lookup. If preprocessing knows no matching declaration, it
preserves the written components as a global qualified spelling; semantic
analysis may subsequently diagnose the reference as undeclared.

### Namespace declarations and membership

```text
NamespaceDeclaration
    = "namespace" namespace_name "{" { ExternalDeclaration } "}" .
```

A namespace declaration is permitted only at file scope. A qualified header is
shorthand for nested namespaces, so `namespace device::uart { ... }` is
equivalent to nested `device` and `uart` blocks. Inside another namespace, all
header components append to the current namespace. Reopening a namespace adds
declarations to the same qualified namespace, but every reopened block is a
new lexical scope.

The current namespace qualifies these file- or namespace-scope names:

- functions and objects;
- typedef names;
- `struct`, `union`, and `enum` tags;
- enumeration constants;
- user-defined macros.

It does not implicitly qualify parameters, block-local declarations, labels,
record members, or field designators. Grammar role takes precedence over a
matching namespace name; a parameter or member remains local even when an
imported entity uses the same spelling.

There are no anonymous or inline namespaces and no namespace aliases, and a
namespace block cannot appear inside a function.

### Scopes and `using`

Every namespace component creates a lexical scope. Its scope begins after the
opening brace and ends at the matching closing brace. Namespace braces and
`using` declarations remain in preprocessed Cross so later procedural
expansion and semantic lookup retain the exact call-site scope. An included
file is textual input, creates no scope, and inherits the namespace, block, and
import context at its include site.

```text
UsingDeclaration = "using" namespace_name ";" .
```

`using` imports one namespace, not one entity. Its operand is interpreted as a
complete global namespace spelling; a nested namespace must be written in full.
The import is visible from the end of the declaration through the end of its
file, namespace, or compound-block scope. Closing the scope discards it.
Reopening the same namespace does not restore imports from an earlier block.

### Unqualified lookup

For an unqualified identifier at a use site, lookup proceeds in this order:

1. parameters and declarations visible in the current lexical scope;
2. the current namespace, from innermost component to outermost;
3. active imports, from innermost lexical scope to outermost and in source
   declaration order within each scope;
4. the global namespace;
5. no rewrite, leaving the spelling unresolved for semantic analysis.

The first match wins. A later import does not override an earlier import.
Labels, members, field designators, macro parameters, and other grammar-local
names suppress namespace rewriting in their respective roles. Declarations
later in the same fully expanded primary input may participate in namespace
lookup, but this does not waive Cross declaration-before-use requirements.

Cross has no implicit function declarations. Every ordinary call must resolve
to a compatible declaration or visible definition.

## Preprocessing and preserved scopes

`cpp` emits preprocessed Cross accepted by `cc`; integrated preprocessing is
identical. It performs lexical inclusion/conditional/macro work and the limited
scope tracking required for namespace-qualified user macros, but it does not
perform semantic name resolution. Neither tool runs another program or
targets another compiler.

### Processing order

Each primary input has an independent macro environment and is processed in
this fixed order:

1. `-D` and `-U` options and the predefined `$::` macros are applied, `-imacros`
   files are processed, and `-include` files are placed before the input;
2. textual includes are expanded;
3. namespace/`using` syntax is tracked for user-macro lookup while its tokens
   and lexical scopes are preserved;
4. conditional directives are evaluated and macros are expanded;
5. active `$::embed` forms are recognized and their file dependencies recorded;
6. line markers and preprocessed Cross tokens are emitted.

This order is normative. Macro output and token pasting become ordinary
preprocessed Cross tokens and are parsed by `cc` together with written source.
Only explicit procedural and activated-syntax boundaries execute
translation-time Cross code. Preprocessing does not execute asset transforms.

### Includes

Quoted includes search the including directory, then explicit `-I`/`-isystem`;
angle includes search only explicit directories. Inclusion is textual and
tracked as a dependency. `#pragma once` is recognized; ordinary guards are
evaluated later. An include cycle not cut by `#pragma once` is diagnosed.
There are no implicit include directories, macro-expanded include operands,
or deferred unresolved includes.

### User macros

Cross preprocessing supports object-like, function-like, and variadic macros;
argument prescan and rescanning; recursive-expansion prevention;
stringification with `#`; token pasting with `##`; `#define`, `#undef`,
`#if`, `#elif`, `#else`, `#endif`, `#ifdef`, `#ifndef`, `defined`, `#error`,
`#warning`, `#require`, and active pragmas. The special replacement token
`$::macro::args` denotes the variadic arguments of a variadic macro.

Compiler-provided file and line macros are `$::source::file` and
`$::source::line`. No date, time, hosted-environment, or vendor-identity macro
is generated. An unknown active directive is an error unless the target
documents it as a Cross pragma. Redefinition of a user macro replaces its prior
definition; redefining or undefining a compiler-owned `$::` macro is an error.

An unqualified macro defined in a namespace is its member and uses the
preprocessor's namespace/import scope tracking. This affects macro lookup only;
it does not qualify ordinary Cross identifiers in replacement text. Macro
parameters are replacement-list locals.

Preprocessing conditions implement unary `+ - ! ~`, multiplicative `* / %`,
additive `+ -`, shifts, relational/equality, bitwise, and logical operators
with the precedence listed for ordinary expressions, over `i64` and `u64`
values. Undefined identifiers evaluate to zero. Unsigned operations use
modulo 2^64 arithmetic; signed overflow yields the corresponding
two's-complement bit result. Preprocessing expressions have no `?:` operator.

Cross syntax in a branch disabled by a literal `#if 0` or `#if 1` is not
diagnosed. Other conditions are evaluated after namespace tracking, so malformed
Cross syntax in a branch that a macro-dependent condition disables may still be
diagnosed.

`#require condition` asserts one preprocessing condition. Its optional
top-level `, "reason"` follows the condition after macro expansion; the reason
must be one final string literal. Commas nested inside a query's parentheses
are part of that query, not the reason separator. The directive ends at its
logical newline and has no trailing semicolon. It uses exactly the `#if`
expression evaluator and the same resolved target/model queries; it does not
evaluate ordinary Cross functions. A zero result is an error at the directive,
including the reason
when supplied. An inactive branch neither evaluates nor diagnoses its
`#require`. The directive emits no source tokens.

```x
#require $::has_feature($::feature::integer128), "requires 128-bit integers"
#require $::has_abi("sysv_abi")
```

A `$::feature::name` is an argument to `$::has_feature`, not by itself a
Boolean preprocessing macro. Unknown or unavailable queried features return
zero according to the compile-time query contract.

### Preprocessed scope output

Preprocessed output preserves namespace braces, `using` declarations, and
ordinary qualified/unqualified Cross spellings. Consequently an explicit
procedural invocation retains the namespace and active imports of its call
site, and copied call-site identifiers can undergo the same lookup as written
tokens. `cc`, not `cpp`, resolves ordinary declarations/references and assigns
object-file symbols. `link_name` is preserved unchanged.

Macro state and semantic source-unit identity do not leak between primary
inputs. In a multi-input stream, `cpp` emits a compiler-generated
`#$::source::unit "path"` boundary before each primary input, where `path`
is the input's source-unit identity, followed by its normal line marker. `cc`
accepts this boundary in preprocessed Cross verbatim and uses it for `static`
visibility. It is not a source directive and is
rejected when written in a `.x` file. Duplicate global definitions and
unnamespaced type or tag conflicts remain semantic errors; preprocessing does
not rename them.

### Source positions and dependency output

`cpp` emits `#line` around inputs/includes. Diagnostics and dependencies use
original paths and positions, and `$::source::file` uses the original path
after `-ffile-prefix-map`; diagnostics include file, line, column when known,
and reason.

Dependencies come from the textual include graph and active, declared
`$::embed` operations and never contain temporary paths. `-M` and `-MM`
emit dependencies instead of source; `-MD` and
`-MMD` emit source and a dependency file; `-MF`, `-MT`, and `-MQ` select the
file and target spelling. Spaces and `#` are escaped and `$` is doubled for
Make. `cpp` emits one rule per input; `cc` emits one rule per output whose
prerequisites are every input, included file, and embedded file of the group.
`-MP` adds a phony target for each prerequisite other than a primary input.
Because there are no implicit system directories, `-MM` is equivalent to
`-M`, and `-MMD` to `-MD`.

### Restrictions

- identifiers and namespace components use portable ASCII identifier spelling;
- macro-expanded include operands are not resolved;
- macro output is parsed as Cross only after preprocessing and cannot introduce
  new preprocessing directives into the completed directive stream;
- Cross syntax in a branch disabled by a macro-dependent condition may be
  diagnosed;
- there are no trigraphs, and namespace components cannot use universal
  character names.

C++ constructs, C++ namespace variants, single-entity `using`, and absolute
`::` names are rejected rather than reinterpreted.

## Compile-time execution and generation

`cpp` never executes Cross functions. After preprocessing, `cc` performs
generic instantiation, constant evaluation, and explicit procedural expansion
before generating code. Generated or instantiated Cross is subject to the same
lookup, type, ABI, standalone, and diagnostic rules as written source.

### Automatic and explicit evaluation

Ordinary functions are **stage-polymorphic**. For each visible direct call,
`cc` tries to compute the call while translating. It succeeds when the complete
called path has translation-time inputs and permitted effects. When it cannot,
the unchanged call is emitted for runtime:

```x
u64 hash40(in const u8 *text) {
    u64 value = 0;
    for (uptr i = 0; text[i] != 0; ++i)
        value = (value * 40) + text[i];
    return value;
}

global u64 greeting_hash = hash40("Hello"); // required; stored as a constant

u64 lookup(in const u8 *runtime_text) {
    u64 a = hash40("known");                 // normally computed by `cc`
    u64 b = hash40(runtime_text);            // emitted for runtime
    return a ^ b;
}
```

Failure of an opportunistic attempt is not an error and produces no speculative
diagnostic; the runtime expression simply remains. Evaluation is mandatory in
an enumerator, case label, fixed array bound, attribute constant,
static-duration non-relocatable initializer, generic value argument,
`$::static_assert`, and every other translation-time-only context. A
source-constant check such as a fixed-vector index bound or an
integer-zero-to-null conversion also accepts a direct call to a visible
ordinary helper with constant inputs whose sandboxed evaluation proves the
result, independently of `-fno-eval-calls` and without a purity attribute.
Caller runtime values are never inputs; a call that is not proved leaves the
expression non-constant, but exhausting a budget during the proof is an error.

The staging controls are:

| Form | Meaning |
| --- | --- |
| `$::eval(expression)` | Require the complete expression to become one translation-time value; failure is an error. |
| `$::runtime(expression)` | Do not enter or translation-time-fold any call in the enclosed expression; emit the operand as runtime code. |
| `[[eval_only]]` on a function | Every call must evaluate; the function has no runtime symbol or address. |
| `[[runtime_only]]` on a function | The evaluator never enters the function; direct calls remain runtime calls. |

The two intrinsics each take exactly one expression, have its type and value,
and emit no call themselves; their argument-count constraints apply in untaken
branches and unevaluated operands. `$::runtime` is invalid where a
translation-time-only context evaluates it, cannot have an opaque meta result
even when unevaluated, and its operand cannot evaluate `$::eval` or an eval-only
call; a well-typed occurrence in an unselected branch or unevaluated operand is
permitted. `$::eval` may call ordinary or eval-only visible definitions but
fails when it reaches a runtime-only call. Staging is not an optimization
barrier: after `$::runtime` lowers its operand, ordinary runtime constant
folding and inlining may still transform it. The two attributes are mutually
exclusive.

An eval-only function has a visible definition, only `in` parameters, a
non-pointer, non-atomic, non-volatile result, and no `global` linkage. Its name
is valid only as the direct callee of a translation-time call; taking its
address is invalid. A runtime-only function is otherwise ordinary and may have
any supported linkage.

An evaluated ordinary call uses the abstract parameter-cell semantics of
“Functions and result-bearing parameters”: cells are distinct, `out` starts
uninitialized, and normal return saves the ordinary result and then performs
assignment conversion and left-to-right copy-out. A copy-out destination must
be translation-time automatic or allocated storage, including its subobjects;
a non-output-capable argument receives a discarded temporary. Evaluation
failure or a non-normal exit performs no copy-out.

#### The translation-time sandbox

Translation-time execution uses target widths, layout, integer behavior, and
floating formats, never host-language values. It may use scalar and aggregate
values, local objects, pointers to translation-time objects, structured control
flow, recursion, other evaluable calls, and deterministic runtime-free
intrinsics documented as evaluator-capable, and it may modify its own automatic
objects and allocated translation-time buffers. It cannot read or modify
runtime or static state; escape a translation-time pointer; access volatile or
atomic storage; use a machine instruction, patch site, runtime-only,
unresolved, or indirect call, raw label, or explicitly located local; depend on
emitted object addresses; or use any host service other than the declared
`$::embed` file read.

Source legality is separate from execution effects. Every concrete body is
source-checked: uncalled definitions, untaken branches, and unevaluated
operands keep every name, type, and constraint rule, including meta-intrinsic
argument types and counts and unquote operand types. A well-typed
sandbox-forbidden operation is an error only when execution reaches it;
inherently invalid types or storage, such as static storage for an opaque meta
value, are source errors. Source checking does not execute meta operations to
validate their values, such as a field name, child index, token spelling, or
parsed text.

A source constraint in a translation-only function whose constant depends on
the invocation, such as an integer-zero proof over quoted tokens or an inferred
array extent, is checked by each invocation in its real expansion context
before the body executes, including in untaken branches; a constraint
independent of the invocation is checked at definition, and an unavailable
context never yields an invented constant. Deferral does not widen the
source-constant expression categories. Block assertions in such a function
instead execute when reached (see “Size, alignment, and translation checks”).

Undefined behavior reached by mandatory evaluation is diagnosed. Diagnostics
show the failing expression followed by the translation-time call chain.
Recursion and loops are permitted within documented, adjustable budgets:
`-feval-byte-limit=N` (bytes in one sequence or buffer),
`-feval-memory-limit=N` (total memory of one evaluation),
`-feval-step-limit=N` (evaluated instructions), and `-feval-depth-limit=N`
(call depth), with defaults 16777216, 67108864, 1000000, and 256. Token
provenance, original macro input, embedded file backing, buffers, and
constructed output count toward the memory bound without depending on host
object sizes. Exceeding a budget in a mandatory evaluation or procedural
expansion is an error; an opportunistic attempt may defer to runtime.
`-fno-eval-calls` disables only opportunistic ordinary-call evaluation.

### Contexts, hygiene, and identities

Every token and syntax node carries an immutable lexical context: the name
bindings, namespace and `using` imports, and syntax activations visible where
it was written or constructed. Except as the binder rule below and the reparse
of deferred nodes (see “Patterns and captures”) provide, lookup of a name uses
the context its token carries, wherever the token is placed, and later
declarations, imports, or activations never change an existing context.

- **Definition context.** Identifiers written literally in a `$::quote` and
  identifiers constructed by `$::meta::token` or `$::meta::parse` use the
  definition context of the executing macro, expander, or helper at the
  construction site, including a helper's block-scope typedefs and tags: a
  quoted name bound to one denotes that type wherever the output is placed,
  and in a generic helper these are the typedefs and tags of the instance that
  runs.
- **Call-site context.** Tokens copied or unquoted from an invocation's input
  keep the context they were captured with. `$::meta::call_site(identifier)`
  takes a one-identifier token value and returns a token with the same
  spelling, span, and identity whose lookup uses the current invocation's
  call-site context.
- **Fresh identity.** `$::meta::gensym(prefix)` takes a translation-time string
  and returns a one-identifier token with a fresh private identity shared by its
  copies: declarations and references spelled with that token bind to each
  other and collide with nothing else, even when prefixes or visible spellings
  match. The visible spelling is `prefix` with each character that cannot
  appear in an identifier replaced by `_`; an empty result becomes `_`, and `_`
  is prepended to a result that starts with a digit or is a keyword.
- **Construction is not copying.** A constructed token receives a new identity
  and the definition context; text cannot recover a private identity or an
  earlier binding. A copied token keeps its identity, context, and span.
  Constructed tokens default to the enclosing invocation's span; an explicit
  span is kept whole and changes diagnostics only.
- **Binders.** When generated or spliced source is first parsed, its
  declarations bind in the destination scope and bind that fragment's own uses
  of them; free names keep the lookup of their carried context, and unrelated
  destination
  declarations never capture a generated use. Once parsed, bindings are fixed
  through copying, token projection, and relocation.
- **Expansion identity.** Every expansion receives a fresh identity. A
  `$::patch` expression copied from input keeps its identity; one constructed
  by an expansion receives an identity derived from that expansion and its
  output position. Textually including a file in two primary source units
  creates distinct source-unit identities.

### Generic functions and exact inference

A generic function places type and value parameters after its declared name.
A bare identifier declares a type parameter; a typed identifier declares a
value parameter. The parameter list is nonempty and has no trailing comma:

```x
static T choose<T>(in bool first, in T left, in T right) {
    return first ? left : right;
}

static T add_count<T, uptr N>(in T value) {
    $::static_assert(N != 0, "N must be nonzero");
    return value + N;
}

u32 a = choose<u32>(1 == 1, 10u32, 20u32);
u64 b = add_count<u64, 4>(7u64);
```

The generic parameters are in scope throughout the complete declared function
type and definition, including the result type before the name; this does not
make undeclared ordinary type names valid. In a comma-separated declaration,
each declarator has its own generic scope: shared declaration specifiers are
interpreted independently for each declarator, and one declarator's generic
parameters are not visible in its siblings. A macro or syntax invocation in
the shared specifiers still expands once, in source order.

A generic redeclaration matches type parameters by position, not by name, and
has the same surrounding types and complete callable contract. An array or
vector extent that depends on generic parameters is compared for each used
instance after substitution, so `N + N` and `2uptr * N` agree when their
evaluated extents agree; an omitted extent is incomplete, not a dependent
wildcard.

Record, union, and enumeration definitions in a generic function's header
belong to that function: their tags and enumerators are visible after their
declaration in the header and in the body, do not enter the enclosing
namespace, and each instance owns distinct nominal types even when their
layouts do not depend on generic arguments. A reference without a new
definition selects an already visible outer tag. A redeclaration repeats each
header definition with the same kind, tag, members or enumerators, types, and
attributes, in the same order, and then denotes the same per-instance types.

A value parameter has integer, enumeration, `bool`, `label`, or pointer type,
and its argument is a representable translation-time constant; its type may
be a type parameter of the same list, which each instance checks after
substitution. Value
parameters are never deduced. Type parameters may be deduced at a direct call;
an explicit application supplies all arguments, or an initial sequence of type
arguments with the remaining type arguments deduced. A missing value
argument, an undeduced type
argument, or a conflicting deduction is an error.

Deduction first resolves the named generic entity, applies the ordinary
array/function parameter adjustments, and obtains each fixed actual's type: for
an `out` or `inout` argument the designator's type, or the expression's type
when a discarded-output temporary is used, without reading an `out` value. The
adjusted formal and actual types are then matched structurally: every
occurrence of an unbound type parameter must yield the same canonical type,
retaining nominal enumeration and label identity, address spaces, pointer
qualifications, and complete stable callable interfaces; top-level `const` on
an `in` cell is ignored. No conversion chooses a deduction. After all arguments
are fixed, ordinary call conversion and mode checks apply, and an explicitly
bound parameter may accept such a conversion. Deduction uses neither an
expected result type nor overload ranking, specialization ranking,
argument-dependent lookup, default generic arguments, best-common-type search,
or implicit selection of a default-profile function-pointer ABI. A callback or
function pointer deduced with a manual or registered stable interface keeps
that interface; an unavailable adapter is diagnosed after deduction. An
actual whose type is an instance of a generic record binds the type parameters
in the formal's argument list structurally, `T` to `u32` for a formal
`struct list<T>` and an actual `struct list<u32>`.

```text
u64 left, right;
u64 selected = choose(1 == 1, left, right); // T = u64
u32 small;
choose(1 == 1, small, right);               // conflicting T deductions
```

After lookup selects a generic entity, `name<...>` is a generic application
wherever the core grammar permits a function designator; otherwise `<` keeps
its comparison meaning. A selected generic commits to parsing its argument
list. `name::<...>` is always a generic application. A top-level `>` ends an
argument list and `>>` closes two; comparisons or shifts inside a value
argument must be parenthesized. This rule is syntactic and independent of
whitespace.

Each used argument list creates one monomorphized instance after substitution
and semantic checking; two identical normalized argument lists denote the same
instance. Declaration-time `$::static_assert` checks and the ordinary
constraints of the substituted
body diagnose invalid instances. Recursive instantiation of the same argument
list denotes the in-progress instance; creating more instances than the
documented, adjustable budgets allow (`-fgeneric-instance-limit=N` and
`-fgeneric-depth-limit=N`) is diagnosed.

A generic declaration has no runtime address, link symbol, or ABI, and its
address cannot be taken without an argument list. An instance is an ordinary
function with the linkage of its declaration, and its address may be taken.
The generic's definition must be visible in every compilation group that uses
it. An instance of a `global` generic is emitted by each such group as a
mergeable definition (the object format's COMDAT group or weak definition), so
separately compiled groups may each instantiate it.

An instance without `link_name` is named by the `generic` rule of the selected
mangling model (see [models.md](models.md#mangling-entries)), which receives
the result of the model's `entity` rule and the ordered, typed generic-argument
descriptors after explicit arguments and deduction have been normalized;
surface spelling does not affect instance identity or its link name. The
shipped `cross` model produces the qualified source name followed by the
readable argument list, for example `mod::choose::<u32>`; assembly syntax
quotes that name when required, and the quotes are not part of the symbol. The
shipped `simple` model uses the `_XN` path encoding followed by `G`, the
decimal argument count, and `_tLspelling` for each type or `_vLspelling` for
each value argument, where `L` is the decimal UTF-8 byte length of the
canonical spelling: `mod::choose::<u32>` begins `_XN3mod6chooseG1_t3u32`.
Mangling model names and rules are unversioned data with no compiler-internal
meaning. A model shared as an external ABI must pin its complete rules and the
descriptor grammar it consumes; an externally consumed instance may use an
explicit `link_name` instead.

### Generic records and unions

A record or union definition may place a generic parameter list after its
tag. The parameter forms and constraints are those of generic functions, and
the parameters are in scope in the member list. A forward declaration of a
generic tag repeats the same parameter list. An anonymous definition and an
enumeration cannot be generic.

```text
struct list<T> {
    T *items;
    uptr count;
};

static T first<T>(in struct list<T> values) {
    return values.items[0];
}

global u32 use(in u32 *words, in uptr n) {
    struct list<u32> list = { words, n };
    return first(list);
}
```

Every use of a generic tag names all of its arguments, as in `struct list<u32>`
or `struct list<struct list<u8>>`; a type use has no deduction, default
argument, or partial application. After a tag keyword, `name<...>` is always a
generic type application. Each normalized argument list denotes one distinct
nominal type: two spellings of the same canonical arguments denote the same
type, and different argument lists denote unrelated types even when their
layouts agree. An instance is laid out and classified like a written record
with the substituted member types, is complete when its substituted member
list is complete, and may refer to itself through a pointer as an ordinary
record may. Attributes on the definition apply to every instance.

A generic function may use a generic tag with its own parameters, as in
`struct list<T>` above; substitution instantiates the record, and deduction
binds the arguments structurally from an actual's type. The descriptor of an
instance is the record's descriptor with its argument list spelled as in
[models.md](models.md#type-spellings).

### Procedural token macros

A procedural macro generates source at one visible splice point. It has one
token-tree input and returns tokens; callers write `name!`:

```x
[[macro]]
static $::meta::tokens twice(in $::meta::tokens statement) {
    return $::quote {
        $::unquote(statement)
        $::unquote(statement)
    };
}

[[macro]]
static $::meta::tokens checked_item(in $::meta::tokens item) {
    return item;    // a real macro would inspect or rewrite the item
}

global i32 add_two(i32 value) {
    twice! {
        value = value + 1;
    }
    return value;
}

checked_item! {
    static void operation() { /* a complete item can be transformed too */ }
}
```

`[[macro]]` implies `eval_only`. The function is `static`, nongeneric, has
exactly one `in $::meta::tokens` parameter, returns `$::meta::tokens`, and has
no runtime symbol or address. The meta types below `$::meta` (`tokens`, `span`,
`context`, `syntax_match`, `syntax`, `bytes`, and `buffer`) exist only during
translation and have no runtime size, layout, address, or ABI. Any function
with one of them in its signature is a translation-only helper: it must be
declared `static` and is implicitly `eval_only`, with only `in` parameters and
no runtime symbol or
address. Helpers are called directly, may recurse within the evaluation
budgets, and may hold meta values in automatic cells and pass or copy them by
value; pointers, arrays, records, and runtime callable types containing meta
values have no representation, and no runtime or static storage may hold one.

Expansion functions (macros and syntax expanders) and translation-only
helpers accept the hints
`always_inline`, `noinline`, `hot`, `cold`, `no_stack_protector`, and
`no_sanitize`, and redundant `eval_only`, under their ordinary argument and
conflict rules; they create no runtime code.
`noreturn` remains binding: reaching a normal return or fallthrough during
evaluation is an error. Attributes that require an emitted symbol, physical
ABI transport, or machine entry/exit machinery (`abi`, `alias`, `aligned`,
`clobber`, `interrupt`, `link_name`, `naked`, `raw_inline`, `retain`,
`returns_twice`, `section`, `stack_cleanup`, `used`, `variadic`, `visibility`,
`weak`, and `weakref`) are invalid on them.

An invocation is a qualified macro name, `!`, and one balanced `(...)`, `[...]`,
or `{...}` token tree; the delimiters group the input and are not part of it.
The result replaces the complete invocation as a token sequence in the
surrounding source, without an implicit grammar boundary, scope, or
parentheses: it may be empty, contain several items, or supply operators and
other fragments that compose with adjacent tokens, so output `1 + 2` followed
by caller tokens `* 3` means `1 + 2 * 3`. An invocation may also supply a
declarator fragment such as pointer parts, the declared name, an array or
function suffix, or result-location tokens. Token identities, spans, and
contexts are retained; splicing never pastes spellings. The result may contain
further invocations. Expansion order is outermost invocation first, then source
order; cyclic or nonterminating expansion is diagnosed. Macros cannot intercept
an ordinary operator, call, or type outside an explicit `name!` boundary; a
framework defines a private sublanguage inside an explicit token tree or
declares a syntax prefix under the rules below.

`$::quote { ... }` constructs tokens written as Cross source, and
`$::unquote(value)`, valid only inside a quote, inserts a token value or a
syntax node.
Quotation does not splice by itself: there is no `$::emit`, `$::expand`, or
attribute-macro facility. To transform a declaration or statement, put that
complete source inside `name! { ... }`.

The `$::meta` token operations are translation-only. `one` is a
`$::meta::tokens` value with exactly one top-level element; a balanced group or
a structured syntax-node splice counts as one, and empty or multi-element
values are errors where `one` is required. Inspection never executes an
expansion or reparses a structured splice. Every operation in the following
table is required; `concat` and `parse` are the minimum constructors, and
`$::has_intrinsic` reports every additional operation separately.

| Operation | Result and contract |
| --- | --- |
| `$::meta::len(tokens)`, `at(tokens, index)`, `slice(tokens, offset, length)`, `concat(tokens, tokens)` | Count, select, slice, and join top-level elements; `at` returns a one-element `tokens` value. |
| `$::meta::is_kind(one, kind)` | `bool` testing the element's lexical category. This overload does not change syntax-node `is_kind`. |
| `$::meta::spelling(one)` | Immutable `$::meta::bytes` containing a lexical leaf's exact UTF-8 spelling, without a terminator or literal decoding. Groups and structured splices are errors. |
| `$::meta::children(one)` | `$::meta::tokens` containing a balanced group's contents without its delimiters. Nongroups are errors; children retain identities, spans, contexts, and structured splices. |
| `$::meta::delimiter(one)` | A translation-time string: `"()"`, `"[]"`, `"{}"`, or `"[[]]"`. Nongroups are errors. |
| `$::meta::span(one)` | An opaque `$::meta::span`, including a group's delimiters or a structured splice's retained node span. |
| `$::meta::token(kind, text[, span])` | One lexical leaf, validated against `kind`. `text` is a translation-time string or immutable bytes value. Group delimiters and structured splices cannot be forged. |
| `$::meta::group(delimiters, contents[, span])` | One balanced group around `$::meta::tokens` contents, using one of the four delimiter strings above. Contents retain their provenance and grouping. |
| `$::meta::parse(text)` | Tokens lexed from a translation-time string or a complete immutable bytes value read as UTF-8 source. |
| `$::meta::error(span, message)`, `warning(span, message)`, `note(span, message)` | Translation diagnostics returning `void`; `span` is a `$::meta::span` and `message` a translation-time string. An error stops the expansion, while warnings and notes do not. |

`kind` is one of `"identifier"`, `"builtin"`, `"integer"`, `"floating"`,
`"character"`, `"string"`, `"punctuation"`, `"group"`, and `"splice"`; an
unknown category is an error, and a recognized category different from the
element's yields false. Keywords are lexical identifiers, which does not make
them legal declaration names; `"builtin"` denotes a `$::`-qualified name; grammar
classification is not a token property. `token` accepts only the seven leaf
categories and text that spells exactly one leaf of that kind without
surrounding whitespace or comments; delimiters are constructed only through
`group`, whose contents must already be a balanced sequence. `parse` rejects
zero bytes, invalid UTF-8, invalid lexical input, and unbalanced groups; empty
input yields an empty sequence, and no implicit bytes/string conversion exists.
Spelling is public lexical text and never carries private identity; formatting
a literal value is ordinary helper code or quotation, not a conversion by
`token`. To inspect a splice's leaves, project its node with
`$::meta::tokens(node)` first; group extraction does not flatten it. Counts and
indices use target `uptr` under the index rules of “Embedded assets and
translation-time bytes”, and these operations count against the evaluation
budgets.

`$::meta::tokens` and `$::meta::span` name both a type and an operation. Where
the grammar permits either a type or an expression, either name immediately
followed by `(` is the operation call, including in expression statements,
parenthesized and cast probes, layout-query operands, and generic arguments; in
a position that requires a type, and in a named declaration, the name denotes
the type, so `$::meta::span saved;` declares a span and `($::meta::span)value`
is a cast. Parenthesizing the whole call does not make it a type name.

A parsed outer capture does not execute a nested invocation merely to classify
a declarator: when the declarator's structure depends on the fragment, the
bounded capture is deferred until the owner has expanded, as described under
“Patterns and captures”.

A diagnostic in generated source first points to the most specific generated
token, then prints the expansion chain of invocations and definitions; the
chain is part of the diagnostic contract.

### Embedded assets and translation-time bytes

`$::embed("path")` names one external input file and yields `$::meta::bytes`
during translation. Ordinary Cross functions may inspect or transform that
value before it initializes an eligible static byte array:

```x
static $::meta::bytes prepare_asset(in $::meta::bytes source) {
    return source; // a real transform would compress or validate the bytes
}

static const u8 logo[] =
    prepare_asset($::embed("assets/logo.png"));
```

This operation is the only core exception to translation-time code's lack of
host-file access. The file is read byte-for-byte; it is not Cross source, is
never preprocessed or decoded, and receives no implicit terminator.
Translation-time code receives bytes, not a resolved host path or a file
handle. Directory enumeration, globs, recursive trees, URLs, and implicit
decompression have no `$::embed` form.

After source-macro expansion, an embed expression is exactly
`$::embed(string_literal)`: one final string-literal token, which a source
macro may supply, not an expression or adjacent literals. The operation has
type `$::meta::bytes` and is mandatory evaluation: it cannot be enclosed by
`$::runtime`, lowered to a call, or used through a runtime function pointer.
An inactive preprocessing branch creates no operation and no dependency.

A relative path uses the quoted-include search order: the directory of the
expression's logical source location, then `-I` directories, then `-isystem`
directories, each in order; an absolute path names one candidate. There are no
implicit asset directories, and these options configure lookup, not evaluator
I/O or model search paths. The directories are lookup bases, not a confinement
boundary: `$::embed` grants authority only to the file its written path
selects, and a build that needs a narrower sandbox provides one externally.
The selected path is normalized with symbolic links resolved for dependency
identity; diagnostics show the written path and logical location.
`$::has_include("path")` tests availability under the same environment. Only a
readable regular file is accepted; a missing or nonregular file is a
preprocessing error when dependency output is requested and a translation
error otherwise.

`cpp` recognizes completed embed forms after source-macro expansion and before
procedural or syntax expansion, so its dependency set, emitted with include
dependencies in `-M`, `-MM`, `-MD`, and `-MMD` output using the same target
spelling and escaping, can include a file that a later expansion discards.
`-E` preserves the expression; `cpp` never reads or substitutes asset bytes.
Integrated `cc` keeps the preprocessor's selected path and reads the bytes only
for mandatory evaluation. Each occurrence has an immutable dependency identity
tied to its expression and logical origin: an expander may copy the expression,
and every copy keeps the identity and the same file snapshot, but constructing
an embed expression anew, changing its path, or emitting one without that
identity is forbidden generated output. Tokens inside a quote template or a
syntax pattern are not operations merely because they spell `$::embed`. A
separately supplied `.i` file is a new primary source for discovery.

`$::meta::bytes` is an immutable owned byte sequence; `$::meta::buffer` is
mutable fixed-capacity translation-time storage. Neither may enter runtime
storage, runtime calls, relocations, patch values, or integer representations.
Ordinary runtime-capable functions may process translation-time byte pointers
when their complete evaluated call path has permitted effects.

The `$::meta` sequence operations are:

| Operation | Translation-time result and constraint |
| --- | --- |
| `len(bytes)` | Length as target `uptr`. |
| `at(bytes, index)` | One `u8`; invalid index is an error. |
| `slice(bytes, offset, length)` | Immutable bounded view; overflow or out-of-range is an error. |
| `concat(bytes, bytes)` | New immutable concatenated bytes. |
| `data(bytes)` | Read-only translation-time `const u8 *` view. |
| `alloc(capacity)` | New unassigned `buffer` of target-`uptr` capacity, including zero. |
| `data(buffer)` | Mutable translation-time `u8 *` view of its capacity. |
| `cap(buffer)` | Capacity as target `uptr`. |
| `freeze(buffer, length)` | Consume its initialized prefix as `bytes`. |

`len`, `at`, `slice`, and `concat` are exact-type overloads shared with
`$::meta::tokens`; no mixed tokens/bytes operation is implicit. File length,
capacity, and materialized size must fit target `uptr`; length and capacity
queries return target `uptr`, and a count that does not fit is an
error, not a wrapped value. An index is a nonnegative integer within the
sequence's bounds; a negative signed value is not reinterpreted as unsigned.

A `buffer` value is a handle to one allocation: copying or passing it does not
duplicate the allocation. `freeze` consumes the allocation exactly once,
requires `length <= cap` with every byte of the prefix assigned, and
invalidates all aliased handles and derived pointers; reads of unassigned
bytes, use after freeze, and a second freeze are errors. The returned bytes own
their prefix independently. `alloc(0)` is valid, but its data pointer cannot be
dereferenced.

Translation-time storage is modeled as target objects. Immutable byte backing,
including temporaries and slices, stays live for the complete containing
mandatory evaluation; buffer storage is live until frozen and bounded by its
capacity. One bounded view is the array object for pointer arithmetic,
ordering, and subtraction: a pointer may travel through evaluated calls, form a
one-past pointer, and compare equal to another pointer with the same backing
and absolute offset, but cannot be dereferenced outside its view or used after
the evaluation ends. Accesses through other object-pointer types obey target
alignment at the absolute backing offset (every fresh backing is aligned for
every scalar type the target supports), effective-type, width, and byte-order
rules; non-byte writes acquire effective type as in ordinary raw storage, and
byte writes do not erase it. Conversion through `void *` preserves backing and
view. Pointers into translation-time storage are opaque capabilities: they
cannot be cast to integers, inspected through byte or
non-pointer lvalues, frozen as bytes, or placed in runtime storage, including
inside an aggregate result. Typed pointer loads, stores, and containing-object
copies preserve them; a byte overwrite invalidates the stored value until a
complete replacement is stored. Symbolic addresses of runtime entities are
transported as relocations instead, without inspecting emitted addresses.

Processing is ordinary Cross evaluation, not a callback registry:

```x
namespace codec {
    uptr max_size(in uptr source_size) { return source_size; }

    uptr encode(in const u8 *source, in uptr source_size,
                in u8 *destination, in uptr capacity) {
        uptr count = source_size < capacity ? source_size : capacity;
        for (uptr i = 0; i < count; ++i) {
            destination[i] = source[i];
        }
        return count;
    }
}

static $::meta::bytes compress_asset(in $::meta::bytes source) {
    uptr size = $::meta::len(source);
    $::meta::buffer output = $::meta::alloc(codec::max_size(size));
    uptr written = codec::encode(
        $::meta::data(source), size,
        $::meta::data(output), $::meta::cap(output));
    return $::meta::freeze(output, written);
}

static const u8 packed_logo[] = compress_asset($::embed("assets/logo.bin"));
```

The codec may also be called at runtime with ordinary runtime pointers. It
cannot acquire ambient host I/O during translation. A transform may return its
input unchanged, an empty value, or a differently sized result.

One `$::meta::bytes` expression may initialize a static-duration array whose
element type is `u8` or `const u8`, including a block-scope `static` array. An
omitted first bound is inferred from the final byte count; an explicit bound
must equal that count. Empty bytes cannot initialize an array, because array
bounds must be positive. The object is writable only when declared `u8[]`.
Ordinary linkage, alignment, section, visibility, retention, and address-space
rules apply; `noinit` and another initializer are incompatible. `sizeof` the
object is its final byte count. Materialization emits no runtime code or
helper; a block static is initialized during translation, not on first
runtime entry, so its initializer cannot depend on runtime parameters or
automatic local values, although unevaluated queries of their fixed types are
permitted. No implicit conversion permits a meta value to initialize a pointer,
scalar, record, runtime slice, or other object.

An implementation may cache evaluated transforms only when it can validate the
ordered path search, selected normalized path and file contents, the complete
evaluated call graph and source constants, explicit arguments, queried target
facts, language, model, and evaluator options, and resource limits. A newly
appearing file earlier in search order invalidates a previous selection; one
translation uses a consistent snapshot of each selected file. A cache hit
reproduces output, diagnostics, and resource accounting as if evaluation ran.
Input size, meta memory, evaluator work and depth, and output are bounded;
exhaustion is a diagnostic, never runtime fallback. A reported failure
identifies the embed expression and the evaluated call chain.

### User-defined syntax extensions

A library may declare an item, statement, or expression syntax prefix and an
ordinary translation-time Cross function that expands one matched use to
Cross. An extension is inactive until a lexical `syntax` activation selects
it. Recognition occurs in `cc` after preprocessing. Runtime facilities needed
by an extension remain ordinary explicit Cross declarations or calls; names
such as `async`, `go`, `try`, and `catch` have no compiler-owned runtime
meaning.

`syntax` is a reserved keyword. Definitions, activations, and regions are
visible source constructs. Syntax entities inhabit a namespace separate from
ordinary types, values, labels, and procedural macros. A declaration has an
unqualified nonreserved entity name and one of five kinds:

| Kind | Match and replacement |
| --- | --- |
| `item` | Match at file/namespace item position; emit zero or more complete external items. |
| `statement` | Match at statement position; emit exactly one complete statement. |
| `expression` | Match a primary expression; emit one assignment expression without a semicolon. |
| `rule` | Define reusable pattern fields; create no invocation prefix. |
| `bundle` | Name one or more activations; create no invocation prefix. |

The five kind words are contextual only after the declaration colon. A prefix
is one ordinary nonreserved identifier, never punctuation, a qualified path, a
core keyword, or a `$::` built-in.

```x
namespace flow {
    [[syntax_expander]]
    static $::meta::tokens expand_unless(
        in $::meta::syntax_match input
    ) {
        $::meta::syntax condition = $::syntax::node(input, "condition");
        $::meta::syntax body = $::syntax::node(input, "body");
        return $::quote {
            if (!$::unquote(condition))
                $::unquote(body)
        };
    }

    syntax unless : statement {
        prefix "unless";
        match "(" condition:expr ")" body:stmt;
        expand expand_unless;
    }
}

void perform(in bool ready) {
    syntax flow::unless;
    unless (ready) return;
}
```

An expander is `static`, nongeneric, nonvariadic, has exactly one
`in $::meta::syntax_match` parameter, and returns `$::meta::tokens`;
`[[syntax_expander]]` implies `eval_only`, so it has no runtime symbol or
address and cannot fall back to runtime execution. A definition of kind `item`,
`statement`, or `expression` has one `prefix`, one `match`, and one `expand`
clause, in that order; `rule` has one `match` clause; `bundle` has one or more
`use` clauses. Duplicate or missing clauses are errors.

```text
syntax_declaration =
    "syntax" identifier ":" syntax_kind "{" syntax_body "}" ;
syntax_kind =
    "item" | "statement" | "expression" | "rule" | "bundle" ;
syntax_body =
      "prefix" string_literal ";" "match" pattern ";"
          "expand" qualified_name ";"
    | "match" pattern ";"
    | "use" activation_entry ";" { "use" activation_entry ";" } ;
activation_entry = qualified_name [ "as" identifier ] ;
syntax_activation =
    "syntax" activation_entry { "," activation_entry } ";" ;
syntax_region =
    "syntax" "(" activation_entry { "," activation_entry } ")"
    "{" { external_item } "}" ;
```

#### Activation and scope

A declaration is checked locally when parsed: clause order, terminal and
delimiter shape, duplicate fields and choice tags, and progress or fence
conflicts provable without resolving rule names. Rule references stay opaque
until activation, which resolves the selected definitions, bundles, expanders,
and all transitive rules in their definition namespaces and import scopes and
validates the complete graph (left recursion, nullable cycles, rule-dependent
progress, repetition continuations, and expression and type fences) before
committing any binding. Forward references and productive mutual recursion are
permitted; unresolved names and rule-graph errors are diagnosed only when an
activation needs the graph. The first successful activation fixes the rule and
expander identities, which later declarations do not retarget; a failed
activation installs nothing and does not prevent a later valid activation.

`as` replaces one definition's installed prefix; a bundle cannot be renamed. A
bundle may refer to definitions and other bundles, not to rules; cyclic bundle
graphs are errors. One directive resolves, validates, and installs its
flattened entries atomically: any error installs none and leaves earlier
bindings unchanged. Lists are nonempty and have no trailing comma.

An unqualified syntax entity is sought in enclosing non-global namespaces from
nearest outward, then active `using` imports from nearest scope outward, then
the global syntax namespace; multiple candidates at one tier are ambiguous. A
qualified activation names one complete global syntax path without a leading
`::`. Resolution produces a stable entity identity before the binding is
installed.

An activation takes effect after its semicolon until the enclosing file,
namespace body, or compound block ends. Nested scopes inherit it and restore
the outer environment when closed; reopening a namespace does not restore an
earlier body's activations; textual includes inherit the include-site
environment; primary inputs are independent. A `syntax (...) { ... }` region is
allowed only at item position and is a transparent declaration group: its
declarations belong to the enclosing scope, while activations and imports
established inside it end with it. Block activation does not permit nested
function definitions.

At an applicable position, an active prefix commits to its grammar: failure is
a syntax-match error, and ordinary identifier parsing is not retried. An
existing `name!` invocation, `name::member` path, or `name:` label takes
precedence. Item prefixes form one dispatch family; statement and expression
prefixes share another. Two distinct bindings cannot claim one prefix in a
family, including across inherited scopes; an alias resolves the conflict, and
repeating the same binding is harmless. Where the grammar permits either a type
name or an expression (cast and parenthesized probes, `sizeof` and `$::alignof`
operands, generic arguments, and declaration/expression-statement
classification), an active expression prefix takes precedence over type-name
classification, including typedef and generic-parameter names; a qualified
name keeps the qualified-name precedence; a position that requires a type does
not dispatch expression syntax; statement prefixes never override type probes.

Only preprocessed source introduces definitions, rules, bundles, activations,
and regions: source macros finish before recognition and their output counts
as preprocessed source, while expander or macro output cannot register syntax
and `$::meta::parse` cannot change the activation environment.

#### Patterns and captures

A pattern starts immediately after its active prefix. Its grammar is:

```text
pattern = element { element } ;
element = string_literal | capture | rule_reference ;
capture = identifier ":" capture_spec ;
rule_reference = "rule" "(" qualified_name ")" ;
capture_spec =
      "ident" | "name" | "literal"
    | "paren" | "bracket" | "block" | "group"
    | "tokens_until" "(" string_literal ")"
    | "function" | "function_raw"
    | "expr" | "stmt" | "type" | "declaration"
    | "function_header" | "function_decl" | "function_def"
    | rule_reference
    | "optional" "(" pattern ")"
    | "repeat0" "(" pattern ")" | "repeat1" "(" pattern ")"
    | "separated0" "(" pattern "," string_literal ")"
    | "separated1" "(" pattern "," string_literal ")"
    | "choice" "(" alternative { "|" alternative } ")" ;
alternative = identifier ":" "(" pattern ")" ;
```

| Capture | Meaning |
| --- | --- |
| `ident`, `name`, `literal` | One identifier, qualified user name without generic arguments, or literal token; a sign is separate. |
| `paren`, `bracket`, `block`, `group` | One raw balanced group including delimiters. |
| `tokens_until(";")` | Nonempty raw tokens before the next top-level semicolon. |
| `function`, `function_raw` | Direct core function header followed by an opaque brace body. |
| `expr`, `stmt`, `type` | One bounded assignment expression, complete statement, or core type name. |
| `declaration` | One complete core declaration other than a namespace or function body, including ordinary `using`, global-label, and `$::static_assert` declarations where valid. |
| `function_header`, `function_decl`, `function_def` | Direct function header before body/semicolon, whole prototype, or definition with a parsed body. |
| `rule(name)` | One reusable rule's child record. |
| `optional`, `repeat0/1`, `separated0/1`, `choice` | Nested child records, with a choice's selected tag. |

`function` aliases `function_raw`. Raw groups are opaque to the core parser and
may contain a library-owned sublanguage. A raw-group capture accepts written
delimiters or a structured splice of a `group` node with matching delimiters
(`paren` `()`, `bracket` `[]`, `block` `{}`, and `group` any of those or
`[[]]`): the splice is consumed as one element and retained as the original
group node, and the capture exposes its
tokens with nested splice boundaries intact and the original group's span.
Quoted delimiter terminals do not open a structured group; project it
explicitly to match its interior as separate elements. A named rule reference
exposes its fields as one child record; an unnamed reference consumes input
without exposing them. Field names are unique within a record. A separator is
one token, never a delimiter; each repeated body is independently balanced,
and trailing separators are not accepted.

Matching is deterministic and executes no user code:

1. A quoted terminal matches one token by kind and spelling. Quoted delimiters
   may enter a balanced group, but the alternative must balance and exhaust it.
   The whole invocation and its enclosing group must match.
2. There is no longest-match or declaration-order priority: if two complete
   derivations succeed, even of different lengths, the invocation is ambiguous.
3. Optional, repeated, separated, and recursive bodies must consume input when
   selected. Left recursion, nullable cycles, and a repetition whose possible
   starts conflict with its continuation after nullable elements are rejected
   at declaration when locally provable and otherwise at activation.
4. Once a repeated element or separator begins, malformed input is an error for
   that derivation rather than the end of the repetition; independent
   alternatives remain eligible, and several surviving derivations are still
   ambiguous.
5. `expr` and `type` are fenced by `;`, `,`, or their matched closing
   delimiter, whether the fence comes from the enclosing pattern, a rule, or a
   combinator, and every possible continuation must satisfy it; a fragment
   parsed by `$::meta::parse` may instead end at its input boundary. Other
   parsed captures consume one complete core unit.
6. `tokens_until(";")` is followed immediately by the terminal `";"` and stops
   only outside balanced groups. A pattern such as `value:expr "+" other:expr`
   cannot inject infix precedence and is invalid.
7. Rule depth, matching work, and output are bounded. Exhausting a bound is an
   error for the invocation, not a failed alternative, including a bound
   reached inside a nested invocation or a boundary probe; a successful
   independent alternative cannot hide it.

Identical spelling with different carried contexts or private identities does
not by itself make a recursive expansion cycle.

Parsed captures preserve nested procedural and syntax invocations as opaque
nodes without executing them. When recognizing a core capture depends on a
nested invocation, the public tree holds a category-tagged `deferred` node with
the complete bounded tokens and their context; it is opaque to child traversal
and may be copied or replaced as one node. After the owning extension and the
remaining nested invocations have expanded, a surviving deferred node is
reparsed in its carried lexical environment, with the completed generic
bindings of its original header when it belongs to one; a procedural fragment
later in a header may introduce generic parameters whose scope includes earlier
parts of that header, and a capture that depends on them is deferred rather
than executed ahead of its owner. Recognition may leave an independently
bounded parameter, attribute, generic parameter list, dependent result-array
suffix, or
inline tag group opaque while classifying a declarator, but never guesses from
delimiters: parentheses in a macro invocation are not evidence of a function
declarator, a function-pointer object does not match a direct-function
category, and when an opaque fragment leaves the boundary or the
classification undecidable, the match is invalid and the pattern must use a
raw bounded capture instead. Boundary recognition never skips a possible body
to find a semicolon in later source; written inline tag definitions, macro
input groups, quotation, and initializers keep their own boundaries; expression
syntax in an expression or initializer position keeps its own matched input;
an expression prefix that could be a declarator name does not by itself prove
ownership of a following body. The brace body of `function`/`function_raw` is
never parsed to validate the header. A nested invocation inside balanced
attribute arguments does not by itself defer the containing statement or
declaration; name-sensitive attributes that introduce bindings follow the
header rules.

Lookup for a reparsed deferred node follows the carried-context rule.
Declarations introduced by earlier surviving expansions in its original block
are visible to it; copying that block preserves the association, moving the
node into another block does not make that block's declarations visible, and a
declaration placed after the captured position is not made visible by an
expansion that emits it earlier. When several surviving copies of the original
block are equally associated with the node, a name that needs those copies'
declarations is ambiguous and diagnosed rather than resolved to the first or
latest copy; already captured bindings and names that do not depend on the
copies remain usable. A node associated with one copy is never supplied a
declaration omitted from that copy by another copy. Value, alias, and tag
lookup follow the same rule. An
unresolved `struct` or `union` type use introduces its implicit forward tag in
its carried scope, repeated uses in that scope share it, and moving the use
neither merges the tag with a destination tag nor binds it there; explicit tag
declarations and definitions are declarations and bind in the destination
scope.

#### Syntax values, the public tree, and expansion

`$::meta::syntax_match` and `$::meta::syntax` are translation-only values. A
syntax node is an immutable, lossless concrete syntax tree, not the compiler's
internal representation. Its kinds are `token`, `group`, `core`, `extension`,
`macro`, and `deferred`. Each occurrence of a grammar-summary nonterminal is
one `core` node named for that production; its children are the terminals, as
`token` leaves including punctuation and written parentheses, and the
nonterminal occurrences of the selected alternative in source order; absent
optionals contribute no child, and repetitions contribute their occurrences in
order. A raw balanced capture is a `group` node with its delimiter tokens and
ordered contents, inspected without parsing or expanding them; a structured
splice inside it is the original node as one child, keeping its kind, identity,
span, and context, and that boundary survives copying and child replacement
(replacing an interior raw token with a core or opaque node introduces such a
boundary; the delimiter children remain matching lexical tokens). `extension`,
`macro`, and `deferred` nodes are opaque leaves for traversal; an extension node
retains its resolved definition, input, context, span, and match record, and
dedicated accessors expose bounded input without classifying it. Every node
carries its span and lexical context, and a child's kind, production name, and
position are part of the public schema.

The `$::syntax::` operations are:

| Operation | Result |
| --- | --- |
| `input(match)` | Complete matched tokens, including the actual prefix. |
| `capture(match, field)` | Primitive captured tokens, including a raw balanced group. |
| `node(match, field)` | One parsed syntax node, or a `group` tree for a raw balanced-group capture. |
| `count(match, field)`, `at(match, field, index)` | Number of nested child records and one such record. |
| `is_variant(record, label)` | Test the selected choice tag. |
| `span(match)`, `capture_span(match, field)` | Original source spans as `$::meta::span` values; an empty capture's span is anchored at its input boundary. |
| `context(match_or_node)` | Immutable lexical name-binding, namespace/import, and syntax-activation context. |
| `error(span, message)`, `warning(span, message)`, `note(span, message)` | Translation diagnostics with the argument types of `$::meta::error`, returning `void`; an error stops the current expansion, while warnings and notes do not fail translation. |

`context` returns an opaque `$::meta::context` value: it may be copied,
assigned, or selected by a scalar conditional whose alternatives have that same
type, cannot be inspected or converted as a scalar or emitted as tokens, and
shares one immutable environment among its copies. For a complete extension
match it is the context retained by the invocation prefix; captured fields and
nested records retain their own input contexts.

The `$::meta` public-tree operations are `parse(category, tokens, context)`,
`tokens(node)`, `node_span(node)`, `is_kind(node, kind)`,
`is_production(node, name)`, `child_count(node)`, `child(node, index)`,
`replace_child(node, index, replacement)`, `is_extension(node, definition)`,
and `extension_match(node)`. The parse categories are the translation-time
strings `expr`, `stmt`, `type`, `declaration`, `function_header`,
`function_decl`, and `function_def`; the other arguments are
`$::meta::tokens` and `$::meta::context`. Parsing consumes the complete
bounded input in the explicit context and cannot execute nested expansions or
register syntax. Replacement is structurally validated. Invalid fields, tags,
indices, categories, and replacement shapes are errors. Counts return target
`uptr` and reject unrepresentable values; child, replacement, and record
indices are nonnegative integers within bounds, whatever their integer type.
For `is_extension`, `definition` is a translation-time string naming a syntax
entity, resolved in the node's retained syntax lookup context; the operation
compares that entity identity with the node's definition and returns `bool`
(false for a non-extension node), and an invalid, ambiguous, or invisible name
is an error rather than a false comparison.

A `$::quote` value is a structured token sequence. A syntax node spliced with
`$::unquote` stays indivisible and category-aware through quotation, copying,
concatenation, and parsing: an expression node for `amount + 1` spliced before
`* scale` means `(amount + 1) * scale`. `$::meta::tokens(node)` projects it
to plain tokens and gives up that guarantee; an operation that cannot preserve
a splice requires the explicit projection instead of flattening silently.

A spliced node occupies exactly one unit of its category at its destination
and keeps its public-tree root; where the destination grammar needs wrapper
productions (`statement` and `unattributed_statement` around a statement or a
block declaration, `type_specifier` around a type, `primary_expression` around
an expression, `declaration` or `function_definition` around a composed
header), they are added around the retained node. In particular:

- A statement splice occupies one statement of the destination block under
  ordinary lexical scope: a direct declaration statement binds in that block,
  declarations in a nested compound or loop scope do not leak, and the splice
  cannot attach to an outside `else`. A spliced compound statement keeps its
  `compound_statement` root at a function-body position.
- A declaration splice occupies one complete declaration at item scope or one
  declaration statement in a block, in the forms valid for its captured
  context (block-local storage specifiers, `using`, global-label declarations,
  and `$::static_assert`), keeping their `using_declaration`,
  `global_label_declaration`, and `static_assert_declaration` roots. A spliced
  `using` at block scope is a block item, never an unbraced `if` or loop body.
  Inspection neither imports nor asserts; effects and validation apply when the
  declaration survives expansion. A settled declaration still obeys the
  requested category's file or block grammar, including when parsed inside a
  statement.
- A `function_definition` splice occupies one definition at item scope. A
  `function_header` splice followed by `;` or by a compound statement composes a
  `declaration` or `function_definition` root whose children are the header
  node and the semicolon token or body node; attribute specifiers before or
  after the header belong to that function, are validated together with the
  header's own attributes without overriding conflicts, and stay separate
  children in written order. A prototype is a `declaration` node, accepted as
  `function_decl` only with a direct-function declarator. Parsing a decorated
  header splice as `function_header` yields a header root whose children are
  those attributes and the original header node; a bare header splice parses
  to itself. If the header is deferred, the composed unit is deferred without
  executing the header or body.
- A type splice denotes its complete captured type and composes with
  surrounding qualifiers and declarators like a type alias; it does not
  redistribute pointer or array tokens into a neighboring declarator.

In every case the declared names and tags of a spliced node bind in the
destination scope, while identifier uses already inside it keep their carried
lookup (see “Contexts, hygiene, and identities”); a spliced header's
parameters and generic declarations provide the normal body bindings. Nested
deferred nodes keep their own categories and contexts. Splicing does not drop
generic assertions or change the destination source unit's linkage rules.

The logical order is preprocessing, lexical activation and declaration
parsing, recognition of one bounded invocation, execution of its expander,
parsing of its replacement, expansion of the remaining invocations outermost
first and then in source order, and ordinary category and semantic validation.
The owner expands before the nested invocations in its input, and inspecting a
public tree never executes it. Attributes, `case`/`default` labels without an
enclosing switch or with duplicate defaults, and object declarations whose
omitted bound is not yet completed remain inspectable: their placement,
argument, conflict, control-flow, initializer, and completeness constraints
apply to source that survives
expansion, not to discarded or repaired syntax, and an owner may discard a
captured statement or supply its receiving switch without changing captured
lookup. A replacement occupies only its matched subtree: it cannot consume
adjacent source, attach to an outside `else`, or register syntax, and
expression output remains a subtree. These boundaries hold even when the
output contains token macros, and a structured node containing a macro
invocation likewise keeps its boundary and category.

The procedural sandbox and its budgets govern matching, tree construction,
expander execution, and replacement. Diagnostics identify the invocation,
definition, rule, and expansion ancestry for definition, activation, match,
ambiguity, meta-API, output, effect, or limit failures. Cyclic or
nonterminating expansion is diagnosed.

This facility exposes syntax, not resolved types, binding identities,
control-flow graphs, liveness, closure conversion, coroutine lowering, cleanup,
or unwinding. A library that advertises exceptions, suspension, or structured
cleanup must implement and validate its own subgrammar and runtime protocol, or
reject unsupported input. Wrapping a statement can retarget `break`,
`continue`, or `else`; outward `return` or `goto` can bypass generated cleanup;
nonlocal transfer performs no automatic copy-out.

## Declarations, definitions, and linkage

Cross separates declaration, definition, and linker visibility. At file or
namespace scope, an undecorated declaration without an initializer or function
body is a forward declaration, not a C tentative definition.

```x
namespace mod {
    i32 state;             // object forward declaration; allocates no storage
    i32 update(i32 value); // function forward declaration
}
```

Definitions have one of three linkage classes:

| Source form | Linkage | Visible from | Linker-visible |
| --- | --- | --- | --- |
| undecorated definition | compilation-group | every source unit in the group | no |
| `static` definition | source-unit | its source unit only | no |
| `global` definition | external | all compatible compilations | yes |

An undecorated function with a body or object with an initializer is a
compilation-group definition:

```x
i32 mod::helper(in i32 x) { return x + 1; }
i32 mod::seed = 7;
```

Such a definition may satisfy references anywhere in the same compilation
group. If emitted, its symbol has non-external binding and cannot satisfy a
reference from a separately produced object.

`static` gives source-unit linkage. A `static` object declaration
without an initializer allocates zero-initialized storage. A `static` function
declaration without a body declares a function that must be defined in the same
source unit if it is used.

`global` gives external linkage. A `global` object declaration without an
initializer is a zero-initialized definition. A `global`
function with a body is an exported definition. A `global` function declaration
without a body is an external declaration and does not itself define a symbol.

```x
global i32 mod::state;

global
i32 mod::update(in i32 value) {
    return value + mod::state;
}
```

At block scope, ordinary Cross rules decide whether a declaration defines an
automatic or static object. The forward-declaration rule above applies only at
file and namespace scope.

### Resolution at the end of a compilation group

After all source units have been parsed, each referenced forward declaration is
resolved as follows:

1. A compatible `static` definition may satisfy a reference in its own source
   unit.
2. A compatible compilation-group or `global` definition in the group may
   satisfy a reference visible to the caller.
3. Otherwise, the declaration is an external import. Functions use the
   selected registered ABI unless their declaration provides an ABI attribute
   or manual locations.

Conflicting definitions, linkage decorators, types, modes, manual locations,
link names, or ABI clauses that select different registered entries (an
alias names its entry) for the same entity are diagnosed.

### Link names

An external declaration or definition may specify an exact link name:

```x
global i32 app::start() [[link_name("_start")]];
global i32 c::puts(in const u8 *s) [[link_name("puts")]];
```

`link_name` is permitted only on `global` or unresolved external entities.
Its string must be nonempty and exactly representable by the selected object
format. Two different Cross entities in one compilation group shall not select
the same link name.

Without that attribute, `cc` derives a deterministic link name after
preprocessing and semantic analysis using the selected mangling model.
The shipped default `cross` model preserves the qualified source spelling:

```text
app::read    -> app::read
counter      -> counter
run::resume  -> run::resume    /* global label */
```

GNU and LLVM assembly printers quote a spelling containing `::`; object files,
linker diagnostics, and map files retain the unquoted name. Quoting is an
assembly serialization rule, not part of mangling. The object format maps a
link name to its object-file symbol: Mach-O prefixes `_`, as C compilers for
that format do, so `[[link_name("write")]]` names the C function `write`
(symbol `_write`); ELF and COFF on the current targets use the link name
unchanged. Like quoting, this mapping is not part of mangling.

Nothing is implicitly encoded merely because it appears in a declaration.
The model receives entity kind, qualified name, result/object type, ordered
parameter types and modes, variadic state, and explicit generic arguments; its
rules choose which descriptors affect the linker spelling. ABI names, manual
locations, and operator-binding keys are not mangled.

Mangling rules may define typed recursive helpers and may use their one ordered
per-symbol substitution table explicitly (see
[models.md](models.md#mangling-entries)). Substitution indices and
back-reference spellings are rule output, not compiler policy. The shipped
Itanium-style and MSVC-style mangling models use the same evaluator as `cross`,
`simple`, and user models; no mangling model name selects hard-coded behavior.

ABI and mangling are independent selections. The ABI controls value placement
and machine state at a call boundary; the mangling model controls only external
linker spelling. Any ABI model compatible with the target may therefore be
paired with any mangling model. A function-level `abi` attribute does not
implicitly select or restrict a mangling model.

An object and function cannot share one ordinary Cross name. Under a mangling
model that does not distinguish entity kinds, a global label and any other
external entity must also produce distinct link names or the compiler diagnoses
the collision.
Source-unit and compilation-group entities have no externally observable link
name and may use implementation-internal object symbols. `link_name`
replaces the complete encoding rather than being appended to it. Mangling
model syntax, loading, and compatibility are defined in [models.md](models.md).

## Functions and result-bearing parameters

### Parameter modes

Every fixed function parameter has one of three modes:

| Mode | Initial value available | Assignment allowed | Value copied back |
| --- | --- | --- | --- |
| `in` | yes | yes, unless the local cell is `const` | no |
| `out` | no | yes | yes |
| `inout` | yes | yes | yes |

If no mode is written, the mode is `in` regardless of type qualification.
`in` selects copy-in without copy-out; it does not imply immutability.
`in const T` makes a scalar `T` parameter cell immutable within the defining
function. A pointer parameter's top-level `const` likewise makes that pointer
cell immutable; `const` on its pointee instead protects the pointed-to object
through that pointer. Casting away a local cell's top-level `const` and
modifying the cell is undefined. `out` is uninitialized, cannot be read before
assignment, and must be assigned on every normal return. `out const T` and
`inout const T` are invalid when `const` qualifies the parameter cell.
Result-bearing mode is unrelated to `volatile`.

Assignment of an `out` cell is proved on the function's control flow, per
scalar leaf of the cell (a member, or an element selected by a
translation-time index). A leaf is assigned by an assignment to it or to an
enclosing object, by passing it or an enclosing object as an `out` argument,
or by a store through a pointer the function derived from the cell's address
without passing that pointer to a call or storing it in another object; a
union is assigned by any member. Reading an unassigned leaf, including
through such a pointer, and a normal return on a path where a leaf remains
unassigned are errors. An address that escapes to a call or to storage
neither assigns nor reads the cell.

Function declarations and their callers must agree on the normalized
mode and ABI contract across separate compilations.

Variadic arguments and every fixed parameter of a variadic function are `in`.
Such a function requires a stable standard or complete manual ABI.

### Variadic calls and implementation

An ellipsis follows at least one named parameter. Variadic actuals undergo
array/function decay; `bool`, 8/16-bit integers, and eligible enumerations
promote to `i32`; `f32` promotes to `f64`. The selected ABI model defines all
remaining classification, placement, register/overflow areas, hidden state,
supported types, and named incoming-state resources.

Cross supplies no `va_list` or traversal API. A definition may bind ABI
state into local cells:

```x
[[variadic(u32 gp "gp_offset", void *overflow "overflow_arg_area")]]
global i32 log(in const u8 *format, ...) {
    // user traversal reads and advances gp and overflow
    return 0;
}
```

Exactly zero or one `variadic` attribute is allowed, only on a variadic
definition. Bindings are not parameters or outputs; their unique identifiers,
types, mutability, alignment, locations, and meanings must match the selected
ABI model. Reading an incompatible promoted type is undefined. Aggregates the
ABI model forbids, unrepresentable address spaces, and scalable vectors are
invalid. A complete manual variadic ABI must expose iterable state; a definition
that ignores unnamed arguments may omit bindings.

Manual locations on a variadic interface overlay the automatic classification
of the complete call as at any mixed boundary: unnamed arguments, the count
register, and the `variadic` state follow the automatic classification, and a
fixed location that overlaps a position the model can give an unnamed
argument, or the count register, is invalid.

### Abstract parameter cells

Each parameter denotes a distinct automatic **parameter cell** in the callee.
Its source semantics do not depend on whether that cell is later allocated to a
register, a stack slot, or an indirect ABI channel.

For a call, the following abstract steps occur:

1. Argument designators and input values are evaluated from left to right.
2. One distinct parameter cell is created for each fixed parameter.
3. Each `in` or `inout` cell is initialized by assignment conversion from its
   argument. An `out` cell is left uninitialized.
4. The function body executes.
5. The ordinary return expression, if any, is evaluated and saved.
6. On normal return, `out` and `inout` cells are copied to their destinations
   from left to right in parameter declaration order.
7. The saved ordinary result is delivered to the caller.

For an output-capable modifiable lvalue, step 1 captures its object and address
as the destination. Otherwise a temporary receives discarded copy-out, so
constants and expressions are valid:

```x
void normalize(inout i32 status);

global void example(in i32 a, in i32 b) {
    normalize(0);       // hidden cell initialized to 0; final value is discarded
    normalize(a + b);   // hidden cell initialized to a + b
}
```

For `out`, an lvalue's address is evaluated without reading its value. A
non-lvalue is evaluated once for side effects, then ignored.

Copy-out applies the assignment conversion back to the captured destination.
Writing a `volatile` actual is therefore a volatile access. Copy-out does not
occur after a trap, non-local jump, process termination, or raw `$::_ret`.

### Aliasing and order

Distinct parameter cells never alias even when actuals do. Copying two outputs
to the same object is deterministic: the later parameter wins. Coalescing is
valid only when this behavior is unchanged. Pointee writes occur immediately;
only assignment to the pointer cell waits for copy-out.

### Ordinary results

A function may also return an ordinary result with `return expression;`.
Ordinary results are independent of parameter results. The return expression
is saved before parameter copy-out so that aliasing cannot change the returned
value.

Reaching the end of a non-`void` function is undefined. Reaching the end
of a `void` function performs normal copy-out.

### `inline` semantics

`inline` is a code-generation permission and does not alter Cross linkage or
definition rules. A definition's `static`, compilation-group, or `global`
decorator alone determines whether an out-of-line copy is visible and emitted.
The compiler may inline a function without the keyword and may retain an
out-of-line copy of an `inline` function when needed. Cross has one inline
emission model and no `extern inline` form.

## Labels, code addresses, and computed goto

### Label values

`label` is a scalar code-address type; size, alignment, representation, null,
relocation, signing, and tagging are target properties. It supports assignment,
storage, aggregates, parameters/results, and `==`/`!=`, but not dereference,
call, or arithmetic. Explicit `uptr` conversion is target-defined; where the
target defines it, `(uptr)function::label` is a relocatable address
expression in static initialization. Passing a
`label` through a registered ABI requires the ABI model to classify code
addresses; otherwise it needs a complete manual location.

Each function label is an address constant `qualified_function::label`, valid
in static initialization without `&`. Labels remain in their own namespace:

```x
void scan() { digit: ; other: ; }
label first = scan::digit;
global label targets[2] = { scan::digit, scan::other };
```

For a generic function, `name<arguments>::label` (or `name::<arguments>::label`)
denotes the label in that instance.

Taking/exporting an address requires a canonical non-inlined function instance;
`always_inline` then conflicts. Direct `goto name` imposes no such rule.

### Label linkage

An ordinary label address has compilation-group visibility through its visible
implementation. An externally linkable label requires a `global` stable-ABI
function:

```x
global void run() {
    global label resume:
    // ...
}
```

A separate source or header declares that address without allocating an
object:

```x
global void run();
global label run::resume;
```

Declarations/definition must agree. The selected mangling model supplies the
default link name; matching `link_name` attributes override it. A global label
is a raw alternate address, not a generated function entry/prologue.

### Direct and computed goto

`goto name;` is direct; `goto expression;` evaluates one `label` expression:

```x
global i32 dispatch(in bool condition) {
    label destination = condition ? dispatch::left : dispatch::right;
    goto destination;
left:
    return 1;
right:
    return 2;
}
```

Null/invalid/unavailable targets are undefined. A bare identifier matching both
a local label and object is direct; parentheses select the object expression.

Without `naked`, source and target must share the canonical function,
managed-frame state, raw-stack depth, and active VLA scopes. Computed goto does
no copy-out or dynamic-stack cleanup; provable violations are diagnosed and
runtime violations are undefined. Direct goto retains managed VLA cleanup.

Cross-function goto requires `naked` at both ends, a `global label`, and
compatible complete manual ABI/stack/machine-state contracts. It is a raw
transfer. All computed goto lowering is inline; unavailable code-address or
indirect-branch support is diagnosed.

## The ABI model

An ABI maps logical inputs, logical outputs, the ordinary result, machine-state
effects, and stack ownership to physical locations.

| Kind | Use | Stability |
| --- | --- | --- |
| dynamic | Direct call to a visible non-`global` definition unless overridden | Compilation-group only. |
| registered | Loaded ABI model entry | Stable across compatible compilations using the same model contract. |
| manual/mixed | Explicit parameter/result locations over a dynamic or standard base | Stable only when every unresolved part has a stable base. |

A **complete manual ABI** fixes every input/output, ordinary result, stack
layout/ownership, clobber/preservation rule, entry alignment, and return
mechanism without dynamic allocation.

### Dynamic ABI

The compiler may assign registers, stack slots, memory channels, or coalesced
caller locations per call graph, including mutually recursive functions. Call
edges and callee entries in one output must agree; only values live across an
edge are preserved. A dynamic ABI has no linker contract and is never used by a
separately compiled, unresolved, or indirect caller.

`noinline` and `-fno-inline-functions` disable discretionary inlining;
`always_inline` remains mandatory. A clone made by the compiler is private and
has its own dynamic interface; the original function keeps the interface its
other callers use. Disabling an optimization may make a local call use the
selected registered ABI, but never changes a stable external contract.

### ABI models and selection

`cc` constructs the ABI registry from declarative model files
([models.md](models.md)). The file format has no version header. Shipped models
are embedded in the compiler and also available as
readable source; user files pass through the same parser and validation. ABI
models are not Cross includes, are never searched with `-I`, and cannot inject
code or a runtime.

Every ABI model entry defines:

- a nonempty case-sensitive ASCII canonical name and aliases;
- compilation/function selection permission and availability conditions;
- architecture and address width; data-model constraints are explicit model
  properties, never inferred from an ABI name;
- ordered register banks and value-classification rules;
- argument/result exhaustion, stack-region order, clobbers, alignment, and
  return-address facts; and
- compatibility with every other entry.

`-mabi=name` selects a compilation entry before type layout. `[[abi("name")]]`
selects a function entry or asserts the active compilation-only entry.
`default` denotes the selected profile/target default; the empty name is
invalid. A function attribute never changes the compilation's pointer width,
`iptr`/`uptr`, aggregate layout, or object-file class.

A model is declarative and cannot supply executable compiler code. The generic
ABI interpreter applies its ordered first-match rules to scalar, pair,
aggregate, array, vector, pointer, and zero-size values; recursively flattens,
splits, coerces, or indirects them; allocates named bank cursors; handles
whole-value rollback or partial exhaustion; and lays out configured stack
regions. A rule may distinguish fixed arguments, unnamed variadic arguments,
and results, and may require enabled target features or forbid them; caller and
callee classification use the same resolved subtarget. The compiler does not
recognize ABI names or dispatch to an
ABI-specific code path. An unknown register, action, value kind, bank,
direction, stack region, or unsupported field combination is an error.

An implementation exposes every platform ABI and stable calling convention its
backend can produce, under conventional names and aliases; the shipped models
define those listed in [targets.md](targets.md), and `cc --print-abis` lists the
selected target's. User models may add noncolliding project ABIs expressible by
the common rule language and the target's register inventory.

A function-selectable ABI must share the compilation data model. An ABI changing
pointer/layout/object properties is compilation-selectable; its function
attribute is valid only as an assertion of the active `-mabi`, unless the target
defines a bridge. Cross never silently marshals incompatible data models.

Shipped generic profiles select the target's `cross` entry for their data
model. A platform profile may select its platform ABI instead, as the PSP
profile selects `eabi32`. The selected entry applies, unless an `abi` attribute
or manual locations override it, to `global` functions, unresolved declarations,
indirect calls, and adapters for address-taken dynamic functions. `global`
therefore means linker-visible, not C-compatible. The Cross ABI is a stable
registered interface rather than a dynamic ABI, but independently produced
callers and definitions must use the same ABI model, target features that affect
ABI rules, and data model.

The shipped x86-64 Cross entry preserves RBX, R12-R15, and compiler-owned
stack/frame state; its other GPR, SIMD, mask, and x87 resources are
caller-clobbered. This is a model property, not a universal Cross rule. When a
function with a stronger registered contract calls such an entry, the caller
preserves the difference with ordinary target save/unwind machinery.

For a MIPS ELF32/address32 data model, the shipped `cross32` entry (aliased as
`cross`) always uses 32-bit integer carriers, independent of the selected ISA,
and preserves `s0`-`s7`. The separate `cross64` entry requires MIPS III or
later and uses 64-bit GPR carriers without changing pointer width, aggregate
layout, or object class.
Selecting a wider ISA never mutates an already selected registered contract;
the compiler may nevertheless choose 64-bit carriers for a dynamic local
interface. For the MIPS ELF64/address64 data model, the shipped `cross-n64`
entry, also aliased as `cross`, uses 64-bit carriers and pointers and preserves
`s0`-`s7` like `cross64`. A name shared by entries of different data models
denotes the entry of the target triple's data model.

A source primarily implementing a platform interface may select its C ABI for
the whole compilation with `-mabi=name`. Mixed source normally keeps the Cross
default and marks only C-facing declarations with `[[abi("sysv_abi")]]`,
`[[abi("ms_abi")]]`, or a target alias such as `linux` or `windows`.
Explicitly requested missing model files or entries are diagnosed; the
compiler never silently falls back to a similarly named ABI.

### Registered parameter lowering

The ordinary result follows the registered ABI. `in` is passed by adjusted Cross
value. Each `out`/`inout` parameter becomes a pointer to a distinct channel
object of its adjusted type. The caller performs required copy-in, passes the
channel address, then performs ordered copy-out; a Cross callee loads/stores its
logical cell at entry/normal return. Thus C-facing by-value parameters must be
`in`; output-capable parameters correspond to pointer channels.

### Manual locations and clobbers

A parameter location fixes its input/output endpoint; `-> "location"` fixes the
ordinary result. Unfixed parts remain dynamic for visible local definitions and
use the selected or explicit `abi` base at stable boundaries.

At a stable mixed boundary, the selected ABI model first classifies the complete
logical signature, including every fixed parameter, and then overlays fixed
locations. A fixed location therefore does not compact or renumber later
automatic locations. A collision between an overlaid location and a still-live
base-ABI location is diagnosed rather than repaired by inventing a different
stable ABI.

```x
void normalize(i32 status "eax") [[clobber()]];
i32 transform(in u64 source "rdi", out u64 flags "rdx") -> "eax" [[clobber()]];
```

A direct manual endpoint on `out`/`inout` replaces standard channel lowering;
an indirect endpoint such as `"*rdi"` retains it. Input endpoints may not
overlap, nor may simultaneously live outputs overlap each other or the ordinary
result, unless the target defines one indivisible multi-result location.

`[[clobber("flags", "rcx", "memory")]]` adds effects not already named as
inputs/outputs. Standard ABIs contribute their clobbers; dynamic ABIs infer
them. A stable definition must preserve every undeclared resource or be
diagnosed, never silently widen its contract. A declaration is fully custom
when it names no `abi` and every parameter and its non-`void` result have a
manual location; a fully custom declaration of a function without a
definition in the compilation group requires `clobber(...)`, where empty
arguments mean no extra clobber.
`"memory"` covers externally reachable memory and `"flags"` the ordinary
condition-code resource.

### Function pointers

A function pointer always has a stable interface. Taking a dynamic function's
address creates a private registered-ABI adapter unless it already has a
complete stable manual ABI. Converting a named function to a pointer type
whose interface differs from the function's own likewise creates an adapter
when the target can synthesize one; an implicit conversion of one function
pointer to another function-pointer type with a different interface is an
error, and an explicit cast reinterprets the pointer. Indirect calls use the
pointed-to modes/ABI
and never infer a dynamic ABI. Adapters may bridge function-selectable ABIs,
but never incompatible data models without an explicit target marshaling
contract.

## Manual ABI locations

Location annotations are string literals; target register names are not
identifiers. The string is parsed by `cc`, not by the preprocessor.

### Core location forms

Every conforming target accepts the applicable forms below:

| Form | Meaning |
| --- | --- |
| `"auto"` | compiler-selected endpoint |
| `"reg"` | the target register view named `reg`, such as `eax` |
| `"stack"` | next compiler-laid-out manual argument slot |
| `"stack+N"` | byte offset N in the logical manual argument area |
| `"*reg"` | channel memory addressed by register `reg` |
| `"push"` | LIFO input slot with mode-selected output handling |
| `"push=>pop"` | caller pushes input and pops the final value after return |
| `"push=>discard"` | caller pushes input and discards the slot after return |
| `"reserve=>pop"` | caller reserves an uninitialized slot and pops its output |
| `"A=>B"` | input endpoint A and output endpoint B |

`N` is a nonnegative decimal byte offset. `stack+0` begins at the target-defined
logical argument base, excluding return addresses, linkage records, and
mandatory home areas. The target maps that logical base to its physical entry
stack pointer.

A single endpoint on an `inout` parameter is both its input and output endpoint.
On `in` it is input-only; on `out` it is output-only. In a pair, `A` is ignored
for `out` and `B` is ignored for `in`, but writing an irrelevant endpoint is
discouraged and may be diagnosed.

`*reg` passes a channel address in `reg`. The source-level parameter remains a
distinct cell; the indirection describes transfer at the boundary, not an
implicit reference or alias to the caller object.

Targets may add forms for register pairs, vector lanes, banked registers,
addressing modes, special registers, or capability locations. Target-added
forms must have documented input/output, size, alignment, overlap, and clobber
rules.

### Register views

Register names denote target-defined views, not merely textual names. The
compiler knows that, for example, `eax` overlaps `rax` on x86-64 and that a
write may zero or preserve other bits. A type must fit the selected view and
must satisfy its register-class constraints. Conversions not defined by the
location contract occur in compiler-selected temporary storage.

### Value type and register class

A value's numeric type and its physical register class are independent.
`fptr` means pointer-width floating arithmetic; it never means an object
pointer and never promises a floating/SIMD register.

A target register view may instead advertise **bit transport** for scalar
types outside its usual arithmetic class. A direct parameter/result endpoint
or hard-bound `register` object may then place an integer, object pointer, or
`label` in a floating/SIMD register view:

```x
global void receive(in void *context "xmm0") [[clobber()]];

global void forward(in void *context) {
    register uptr opaque_bits "xmm3" = (uptr)context;
    receive((void *)opaque_bits);
}
```

Transfer into or out of such a view preserves the declared value's object bits;
it performs no integer/floating conversion and does not change the source type.
If the register view is wider, the value occupies its target-defined scalar
lane and all other bits are unspecified. Arithmetic still follows the declared
type and the compiler may move the bits to another register class to perform
it.

The target registry lists supported transported types, lane positions, widths,
feature gates, and upper-bit effects. x87 numeric stack locations do not imply
bit transport. On x86-64, an enabled XMM scalar view may transport a 32- or
64-bit scalar in its low lane, including a 64-bit object pointer.

### Ordered register stacks

A target may expose an ordered register-stack class. On x86-64, `st0`
through `st7` denote the nonempty x87 values at `(TOP + N) mod 8` at the
interface boundary; they are relative stack positions, not eight stable local
object locations.

All direct x87 inputs of one interface must be unique and form the dense prefix
`st0` through `st(I-1)`. All direct x87 parameter outputs plus the ordinary
result must independently be unique and form `st0` through `st(O-1)`. Entry
depth is exactly I and normal-return depth is exactly O. Input and output phases
are independent, so a position may be reused across the call.

The caller loads inputs from highest index to zero. A managed definition
captures and pops inputs from zero upward before executing its body, then loads
outputs from highest index to zero immediately before return. The caller
captures and pops returned outputs from zero upward. Compiler-generated managed
code therefore has no undeclared live x87 stack entries across the boundary.
Binding `f32` or `f64` to an x87 endpoint rounds when the value is captured into
its nominal cell; `f80` preserves the extended value. Hard-bound local objects
cannot use an ordered register-stack position.

The x87 register stack is unrelated to RSP-based `stack`, `push`, `pop`, and
manual argument-area forms. Its depth/TOP resource is modeled separately.

### Stack slots

Manual stack slots contain the parameter value, not a pointer, unless the
source type itself is a pointer. The compiler checks size, alignment, and
overlap. `stack` slots are packed in declaration order using target alignment;
fixed `stack+N` slots reserve their exact ranges.

An ordinary result may also use `stack` or `stack+N`. It is written before
cleanup and read by the caller as the call's result. It counts as a stack output
for cleanup-ownership validation.

The caller owns the logical manual argument area unless a
`[[stack_cleanup("callee")]]` attribute says otherwise. Output values are
collected before caller cleanup.

### Push and pop endpoints

LIFO endpoints describe a precise call sequence. Parameters using LIFO input
endpoints are pushed in reverse parameter-declaration order, so their outputs
can be popped in declaration order after the call.

The shorthand `"push"` expands by mode:

| Parameter mode | Expansion |
| --- | --- |
| `in` | `push=>discard` |
| `inout` | `push=>pop` |
| `out` | `reserve=>pop` |

For `push=>pop`, the caller initializes a stack slot, transfers control, reads
the callee's final value from that same slot after return, and then releases the
slot. `reserve=>pop` reserves the slot without reading the actual's old value.
The target may use a native push/pop instruction or an equivalent stack
adjustment and load/store sequence unless the code is in an exact machine
region supplied by a target extension.

```x
void normalize(inout i32 status "push=>pop") [[clobber()]];

global i32 update() {
    i32 status = 0;
    normalize(status); // push status; call; pop final status
    normalize(0);      // push 0; call; pop and discard final value
    return status;
}
```

`[[stack_cleanup("caller")]]` is the default. A declaration may request callee
cleanup:

```text
[[abi("stdcall"), stack_cleanup("callee")]]
global void notify(in u32 code "push");
```

With callee cleanup, stack output endpoints are forbidden because the caller
cannot collect them after the callee releases the area. All declarations of the
function must agree on cleanup ownership.

## Stack and frame management

Cross distinguishes the managed frame, manual ABI argument area, and raw
instruction stack effects.

### Managed frames and `stack` objects

For an ordinary function, `cc` owns the prologue, epilogue, stack alignment,
saved registers, fixed `stack` objects, spills, and outgoing call areas. A
`stack` object's address is stable for its lifetime even if the physical stack
pointer moves.

```x
void encode(in u64 input, out u64 output) {
    stack u8 scratch[32];
    stack u64 word = input;
    // scratch and word have distinct frame slots
    output = word;
}
```

The compiler may use a frame pointer or another stable base. Source programs
shall not assume an offset for a managed object unless a target-specific hard
location explicitly provides one.

### Managed dynamic stack objects

A variable-length array uses managed dynamic frame storage. Its bound is
evaluated once when its declaration is reached, must be greater than zero, and
its storage is released when control leaves the declaring block. `stack` may
be combined with a variable-length array and a constant alignment request:

```x
void work(in uptr bytes) {
    [[aligned(64)]] stack u8 temporary[bytes];
    // temporary exists until this block is left
}
```

This declaration syntax is Cross's managed replacement for an `alloca`
function. Cross defines no `alloca`, stack-mark, stack-restore, or portable
push/pop function. A user header may provide block macros around stack object
declarations, but an ordinary called function cannot allocate storage in its
caller's managed frame.

A direct `goto` shall not enter the scope of a variably modified object. A
direct jump out of such a scope performs compiler-managed release. A computed
goto must remain at an identical active VLA state as specified by the label
rules. Stack probing, realignment, and restoration must be emitted as inline
instruction sequences; the compiler shall diagnose a function whose managed
frame would require an undeclared probing, growth, or cleanup helper call.

### Native raw stack effects

Raw stack manipulation uses target instructions, commonly `$::_push`,
`$::_pop`, or an arithmetic instruction on the target stack-pointer register.
Their stack
effects come from the target registry. `cc` tracks those effects through
the control-flow graph. At each ordinary control-flow join, incoming raw stack
depth must agree. Before an ordinary call, target alignment and reserved areas
must be satisfied. At every normal return, the raw stack must be balanced to
the managed-frame baseline.

A fixed, tracked imbalance may cross a call; the compiler accounts for it when
forming the outgoing area. An unknown or inconsistent imbalance is an error.
Manual ABI `push`, `pop`, and `reserve` endpoints remain declarative call
lowering and do not create portable stack-operation functions.

### User-defined non-local control and frame inspection

Cross defines no `setjmp`, `longjmp`, frame-address, or return-address
function. A user may implement or link such facilities with ordinary
declarations. A set-jump-like entry is marked `[[returns_twice]]`; a
non-returning transfer is marked `[[noreturn]]`. Both require a stable
or complete manual ABI, and a raw implementation normally uses target
control-transfer `$::_mnemonic` forms.

A call to a `returns_twice` declaration tells the compiler that execution may
resume after that call again. A non-volatile automatic object modified after
the first return has an indeterminate value on a later resumption unless the
declared facility specifies a stronger user contract. A non-local transfer is
not a normal Cross return and performs no parameter copy-out or lexical stack
cleanup. Frame and return-address inspection is target-specific and uses
documented register/resource locations or hardware instructions.

### Naked functions

`[[naked]]` suppresses the managed prologue, epilogue, automatic copy-out,
unwind description, and frame allocation:

```x
[[naked, clobber("memory", "flags")]]
static void context_restore(in uptr frame "rsp") {
    // target instruction built-ins only
    $::_ret();
}
```

An attributed naked function must have a complete manual ABI. It may use
constants, hard-bound register objects, labels, structured `if`/loop control
over values that can be legalized without a spill, and machine built-ins.
`break` and `continue` retain their ordinary nearest-loop meaning; the compiler
checks every resulting join and back edge. It may not use ordinary automatic or
`stack` objects, dynamic stack allocation, variable length arrays, an ordinary
`return`, or an ordinary call that survives raw-compatible inlining. Every
reachable exit must be an explicit target control-transfer built-in.

Because `[[naked]]` functions have no Cross epilogue, parameter copy-out occurs
only through their declared physical output locations. The programmer is
responsible for satisfying the declared stack and preservation contract. The
compiler still diagnoses statically contradictory locations and known invalid
instruction operands.

### Raw-compatible managed inlining

`[[raw_inline]]` marks a managed function that may be called from a naked
function only as a mandatory compile-time inline operation:

```x
[[raw_inline]]
static u64 mix(in u64 value) {
    u64 x = value;
    x ^= x >> 17;
    return x * 0x9e3779b97f4a7c15u64;
}

[[naked, clobber("r10", "r11", "flags")]]
static void entry(inout u64 value "rax") {
    value = mix(value);
    $::_ret();
}
```

`cc` inlines the callee into the naked caller and simplifies the combined code;
no call may remain. The compiler then diagnoses the call if any path would
require:

- a runtime call, managed prologue/epilogue, parameter channel, or copy-out;
- an automatic spill, stack object, VLA, address-taken local, or hidden helper;
- unwind, returns-twice, indirect control, TLS resolution, or another
  managed-only operation; or
- a register, flag, memory, or other resource not permitted by the naked
  function's interface and `clobber` contract.

Temporaries, scalar locals whose addresses are not taken, structured
control flow, pointer accesses, and recursively eliminated `raw_inline`
calls are permitted. Compatibility is checked after optimization, so dead
managed-only code does not invalidate a call. Register allocation may use resources explicitly named by `clobber` and,
while the bound object's value is not needed, a hard-bound register that the
function writes; it shall not create a spill. A `raw_inline` function remains
an ordinary callable function in managed contexts unless `always_inline`
also requires inlining. Taking its address is permitted, but such an indirect
call is never raw-compatible.

## Machine instruction built-ins

Every instruction family available under the selected target/`-march` has one
or more typed, non-linker built-in forms named `$::_mnemonic`. Conceptual forms:

```text
void $::_movabs(out u64 destination, in u64 source);
void $::_add(inout u64 destination, in u64 source);
void $::_cmp(in u64 left, in u64 right);      // defines condition flags
void $::_push(in u64 value);                  // changes stack depth
void $::_pop(out u64 value);                  // changes stack depth

register u64 value "rax";
register volatile u64 *device "rdi";
$::_mov(value, *device);                      // target load form
$::_mov(*device, value);                      // target store form
$::_mov(value, device[index]);                // target indexed load form
```

Source never redeclares these forms; modes, constraints, and implicit resources
come from the target registry.

### Selection and effects

A mnemonic may have several forms. Selection uses:

- operand count and fixed-width types;
- parameter modes;
- constant ranges and immediate classes;
- current or manually fixed physical locations;
- selected architecture features.

An instruction form may require an exact physical register rather than only a
register class.

No/ambiguous best form is an error. A form whose only effect is its register
result is pure; a form without a result is preserved. A selected form denotes
the instruction's
typed semantics and operand constraints, not an opaque exact-opcode barrier.
The compiler may fold it, combine it with surrounding operations, choose an
equivalent instruction or sequence, or delete it when its result and effects
are unobservable.

An lvalue accepted by an instruction memory operand keeps its pointee width,
`const`, `volatile`, and `atomic` qualification. A form that writes rejects a
`const` lvalue. An ordinary non-atomic form rejects an atomic lvalue; volatile
access remains observable even if the chosen instruction spelling is otherwise
identical to a non-volatile access. Addressing forms and displacement ranges
are selected-target constraints, not portable properties of the lvalue.
For a typed pointer `base`, `base[index]` is also an instruction memory operand.
The target accepts it directly only when the index has an encodable integer
register location and `sizeof(*base)` is an encodable scale; otherwise the
program must form the address explicitly with ordinary arithmetic or another
machine built-in.

Each instruction form declares all register, flag, memory, stack, control, trap,
privilege, volatile, and atomic effects. Those effects and implicit resources
create optimization dependencies. A pure form such as integer `$::_add` can be
optimized like the corresponding language operation; an observable effect is
preserved and ordered exactly as its declared effects require.
Ordered register-stack effects additionally declare their depth change or a
complete reset. The compiler rejects underflow, overflow, mismatched depths
at control-flow joins, and any normal return whose depth differs from the
function's declared interface.

### Patchable values and operand fields

`$::patch(initial, site)` is a runtime scalar expression whose encoded source
field may be rewritten after image loading. The patch cell is initialized to
the statically relocated value of `initial`. Evaluation before an allowed
update yields that value; evaluation after an update permitted by a
target-defined dynamic protocol yields the scalar value decoded from the
current cell. The expression retains the type of `initial`, but it is not a
compile-time constant and shall not be propagated as one:

```x
struct relocation {
    u64 encoded_tag;
    uptr operand_address;
    u32 checksum;
};

u64 encode_tag(in u64 tag) { return tag ^ 0x5a5a5a5a5a5a5a5au64; }
u32 checksum(in u64 tag) { return (u32)(tag ^ (tag >> 32)); }

[[section(".data.private"), used]]
static struct relocation record = {
    .encoded_tag = encode_tag(0xb002ca11u64),
    .checksum = checksum(0xb002ca11u64)
};

global u64 patched() {
    u64 key = $::patch(0xb002ca11u64, record.operand_address);
    u64 result = key + 123u64;
    return result;
}
```

`initial` is a compile-time or link-time scalar value accepted by a
target-declared patch materializer. `$::eval` may compute it, and a relocatable
address plus constant addend is permitted when the target/object format can
encode it. A runtime value, volatile/atomic access, or another patch value is
not a valid initial value.

In an ordinary runtime expression, `cc` creates one site-unique patch-value
operation. Machine selection shall either:

1. absorb that operation into one registered patchable field of a consuming
   instruction without changing replacement-value semantics; or
2. emit a target-declared materializer containing one contiguous patch cell
   and use its result in the surrounding expression.

For example, x86-64 can materialize a `u64` with a patchable `movabs`
immediate. A target with fragmented immediate instructions may instead use a
nearby literal cell; if it has no conforming contiguous representation for the
type, the expression is diagnosed. Target-specific source can retain exact
control by placing the same expression directly in an instruction operand:

```x
struct relocation { uptr operand_address; };
[[used]] static struct relocation record;

global void load_key() {
    register u64 value "rax";
    $::_movabs(value, $::patch(0xb002ca11u64, record.operand_address));
}
```

When it is the complete direct operand of a registered patchable instruction
form, that selected operand field is the patch cell and no separate
materializer is introduced. The initial's scalar type shall be one of that
field's registered exact types; an ordinary immediate's value-fit conversion
does not implicitly change a patch cell's replacement-value type. Use an
explicit cast inside `$::patch` when a different field type is intended. The
same registered type set governs `$::has_patch_operand`; equal bit width alone
does not make an integer field accept floating-point or pointer values.
An enumeration remains nominally distinct from its underlying integer type;
cast explicitly when the field registers that integer type rather than the
enumeration. Ordinary typedef aliases do not introduce a distinct type.

`site` shall be a static sink designator. Its grammar is a static-duration
object followed by zero or more `. member` selections or
`[integer_constant_expression]` selections. It shall designate one complete,
addressable, non-bit-field subobject whose address is a link-time constant.
Calls, dereferences, runtime subscripts, pointer arithmetic, and conditional
lvalues are not static sink designators.

The default sink type is unqualified, non-atomic `uptr` when the target declares
every supported patch-cell address representable by it. A target with separate
code/data spaces, capabilities, tags, or wider patch addresses shall instead
register a scalar built-in patch-address type below `$::target` and require that
type for the affected materializer. No conversion may discard patch-address
information.

`cc` initializes the designated sink with an object-format relocation to the
first byte of the exact contiguous patch cell. It shall not be `volatile`,
atomic, or explicitly initialized by source. Implicit zero initialization from
omission in an aggregate initializer is replaced by the compiler relocation
and does not count as a competing initializer. The containing record, its other
fields, its section, ordering, tag encoding, checksums, visibility, and
retention are entirely user-defined. `$::patch(initial)` omits the address
sink.

The target entry specifies cell width, byte order, encoding, alignment,
permitted replacement values, instruction-cache requirements, and whether
concurrent rewriting can be atomic. The compiler pins the patch-value
operation and field identity: it shall not treat `initial` as the runtime
value, fold surrounding transformations into the cell, merge, resize,
duplicate, or move the cell to a different encoding. In
`$::patch(initial) + 123`, replacement bits always denote the value before
addition; a patcher never has to pre-apply `+ 123`.

The expression is siteful but otherwise side-effect-free. Distinct patch
identities are not known equal even when their initial expressions are equal,
because a loader may patch them independently. Multiple uses of the result of
one evaluation share that one materialized value. Ordinary consuming
operations remain optimizable, including register allocation, scheduling,
algebraic simplification independent of the unknown value, and dead removal of
an unreachable/unobservable site.

A patch identity consists of source-unit identity, procedural-expansion
identity, lexical patch-expression identity, and the concrete generic instance.
Each emitted concrete function instance contains at most one cell for each
identity. Different generic instances therefore receive distinct identities
and cells. Function/block inlining, cloning, tail duplication, and unrolling
shall preserve one cell for an identity or be suppressed.

A `raw_inline` or `always_inline` function shall not contain a
sink-bearing patch expression. A sinkless patch is permitted only when the
required inlining produces exactly one emitted identity; otherwise compilation
is diagnosed. No facility generates per-expansion sinks. `used` or `retain`
may make a containing definition and record observable.

Each `site` lvalue is written by at most one live patch expression and shall
not have another initializer. A patch site has a stable address only in the
final linked image; ordinary relocation, section movement, or image rebasing
may change it. Patch records, their relocations, and patched instructions remain
visible in the object file.

A conforming materializer shall emit object-format information that prevents
instruction/data relaxation, section merging, identical-code/COMDAT folding,
thunk substitution, or another final-link transform from changing or sharing
the patch cell. When a selected object format or linker cannot express them,
that materializer is unavailable and `cc` diagnoses its use. If an unobservable
site is removed, its unobservable sink relocation is removed with it; an
observable sink makes the associated site observable.

Evaluating `$::patch` is not permitted in `$::eval`, procedural-macro execution,
a static-duration initializer, array bound, attribute, case label,
`$::static_assert`, or another translation-time context.
As with other sandbox-forbidden operations, a well-typed patch expression may
occur in an unselected branch or unevaluated operand. Its source constraints
still apply, and its type is the type of `initial`; querying that type does not
create a patch cell or initialize an address sink.

In the absence of a target-defined dynamic-patching protocol, a patch cell may
be modified only after final image layout and before the first dynamic
execution of its containing site. It is immutable for the remainder of that
program execution. A target may separately provide
`$::feature::repeatable_patch` and `$::feature::concurrent_patch`. Those
features define optimizer visibility, synchronization, atomicity, permitted
execution/update overlap, and required data/instruction-cache maintenance;
neither is implied by ordinary patchable values or operands.

### Output operands

`out`/`inout` operands use parameter-cell semantics:

```x
void advance() {
    register u64 value = 0;
    $::_movabs(value, 0x123456789abcdef0);
    $::_add(value, 4);
}
```

A constant output actual is allowed only when a valid temporary satisfies the
constraints; its result is discarded.

### Memory, flags, and control flow

Memory effects state exact width, alignment, ordering, and atomicity; a pointer
operand grants nothing broader. Branches accept `label` and distinguish direct,
register, and memory classes. Raw branches perform no cleanup, ABI adaptation,
or copy-out. Terminators end the block; raw returns require `naked`.
Privileged/trapping/system instructions remain exposed but unavailable uses are
diagnosed.

### Inline assembly and raw jumps

Cross has no inline-assembly or `goto *expression` syntax. Instructions use
typed built-ins; placement/emission uses attributes; computed goto uses a typed
`label`. Declared instruction effects replace assembly constraint strings.

## Standalone execution and feature detection

Ordinary operations lower to data, relocations, loops, and finite inline target
sequences, never undeclared calls. This includes arithmetic, aggregate copy/
return, atomics, VLAs, TLS addressing, and initialization. An unavailable
inline lowering is an error. Optimizers may transform explicit calls while
preserving behavior, but a link name such as `memcpy` grants no special
semantics. Taking a function address authorizes only the stable adapter defined
by this specification.

### Runtime-free compiler intrinsics

A compiler intrinsic has no linker symbol, hidden state, or library dependency.
Required intrinsics are:

| Intrinsic | Contract |
| --- | --- |
| `$::expect(value, constant)` | Evaluate `value` once; return its unchanged boolean/integer/enumeration type; use the converted integer constant only as a frequency hint. |
| `$::assume(condition)` | Type-check but do not evaluate a side-effect-free, non-volatile scalar condition; reaching it false is undefined. |
| `$::unreachable()` | Emit no required instruction; terminate the block; execution is undefined. |
| `$::trap()` | When available, emit a documented inline abnormal transfer; preserve prior sequenced effects; never return normally. |
| `$::alignof(type-or-expression)` | Unevaluated compile-time `uptr` alignment query. |
| `$::offsetof(type, designator)` | Unevaluated compile-time `uptr` offset of a member designator (a member name followed by `.member` and `[constant]` steps) within a complete record type. |
| `$::sqrt(x)`, `$::fabs(x)`, `$::copysign(x, y)`, `$::fmin(x, y)`, `$::fmax(x, y)` | Floating operations on one floating type: correctly rounded square root, absolute value, sign transfer, and IEEE 754 `minNum`/`maxNum`; each lowers to one instruction or a finite inline sequence, or is diagnosed. |
| `$::static_assert(constant, string)` | Compile-time declaration requiring nonzero `constant`. |
| `$::patch(initial[, site])` | Siteful runtime scalar source with the lifecycle, identity, sink, and lowering contract defined under patchable values. |

Argument counts, operand types and required constant arguments remain source
constraints in untaken branches and unevaluated expressions. In particular,
`expect`'s integer expectation is checked by required translation-time
evaluation, independently of optional ordinary-call folding. Context-dependent
checks in translation-only bodies follow the definition/invocation rule above.

An assumption condition may use non-volatile object projections, dereferences,
address formation and layout queries. Address formation does not read the
designated object; a layout query does not evaluate its expression operand.
Potentially effectful calls or writes, and volatile/atomic value reads, are
invalid in the condition's potentially evaluated parts. Unevaluated operands
still retain their ordinary source constraints. An assumption never executes
its condition merely to validate it.

`$::trap` and `$::unreachable` perform no copy-out or managed-stack cleanup.
An implementation may offer optional instrumentation that inserts inline
checks ending in `$::trap()`, such as array-bound and output-channel checks
under `-fbounds-trap`; `no_sanitize("name")` suppresses the named
instrumentation per function, and no instrumentation calls a helper.
The atomic intrinsics are listed under “Atomic and concurrent access.”
`$::eval`, `$::runtime`, `$::quote`, and `$::unquote` are translation
intrinsics and therefore emit no runtime operation. Additional intrinsics must
document types, evaluation, effects, feature gates, and finite inline lowering.

### User-provided libraries

Libraries are ordinary declarations/definitions explicitly linked by the user;
no link spelling has intrinsic meaning. Optional nonconforming modes may add
runtime calls only when explicitly enabled and when every required symbol is
documented.

### Compile-time queries

The preprocessor recognizes these integer queries, including in `#if`:

```text
$::has_builtin($::qualified_name)
$::has_intrinsic($::qualified_name)
$::has_instruction($::qualified_name)
$::has_patch_value(type_name)
$::has_patch_operand($::_instruction, zero_based_operand_index, type_name)
$::has_attribute("qualified_name")
$::has_feature($::feature::qualified_name)
$::has_extension($::extension::qualified_name)
$::has_abi("abi-name")
$::has_mangling("mangling-name")
$::has_profile("profile-name")
$::has_include(<header>)
$::has_include("header")
```

Each returns `1` iff the named category entry is usable under the current
language, target, features, and ABI; otherwise `0`. A positive name query does
not validate a particular operand form.
`$::has_patch_value(T)` tests a generic contiguous materializer for exactly
`T`. `$::has_patch_operand(I, N, T)` tests whether zero-based operand `N` of a
selectable form of instruction `I` accepts a patch cell with exactly type `T`;
it does not assert that unrelated instruction forms are patchable. ABI and
model aliases resolve before testing; unknown features/extensions produce zero;
`has_include` only searches configured paths. Core feature names include
`runtime_free_intrinsics`,
`control_intrinsics`, `atomics`, `variadics`, `fixed_vectors`,
`scalable_vectors`, `address_spaces`, `thread_local`, `integer128`,
`binary128_storage`, `binary128_arithmetic`, `evaluation`,
`automatic_evaluation`, `generics`, `generic_types`, `procedural_macros`,
`syntax_extensions`,
`embedded_assets`, `patchable_values`, `patchable_operands`, `repeatable_patch`,
`concurrent_patch`, `raw_inline`, `contextual_attributes`,
`external_models`, and `operator_binding`, each below `$::feature::`.
`patchable_values` reports a generic materializer for at least one core scalar
type; `patchable_operands` reports at least one explicit instruction field.
The two dynamic-patching features are absent unless their stronger lifecycle
contract is registered. This specification defines no `$::extension::` names; an
implementation documents any extension it provides.

### Predefined `$::` macros

Target-aware `cpp` and `cc` provide at least:

```text
$::language::version          900i64
$::language::version_major    0
$::language::version_minor    9
$::target::triple             "canonical-target-triple"
$::target::abi                "canonical-abi-name"
$::target::mangling           "canonical-mangling-name"
$::target::profile            "canonical-profile-name"
$::target::byte_bits          8
$::target::pointer_bytes      /* target value */
$::target::order_little       1234
$::target::order_big          4321
$::target::byte_order         /* one of the order macros */
```

These immutable object-like macros may be tested with `defined`; source cannot
define or undefine them. `$::source::file` and `$::source::line` are also
predefined. Targets may add structured `target::arch`, `os`, `feature`, and
`object_format` names; unselected facts are absent, not zero. No hosted or
other-compiler compatibility aliases exist. `cpp` accepts model, profile,
target, ABI, mangling, and feature options so its environment matches `cc`.

## Attributes and code-generation control

Attributes use `[[qualified_name(arguments)]]`; comma-separated entries may
precede a declaration, follow its declarator, or precede a statement when the
target is unambiguous. `[[...]]` is an isolated grammar context, so its names
are compiler attributes without a `$::` root. `[[$::name]]` is invalid and
receives no compatibility interpretation.

A leading attribute applies to the subjects of the declaration that can carry
it: each declared entity, or the type it declares or defines inline. When an
inline record or enumeration definition and a declared
entity could both carry it, for example `aligned` on an inline record
followed by an object, typedef, member, or function definition, or `packed`
on an inline nested record inside a member declaration, the placement is
ambiguous and is an error; it is never applied to both. Put type layout
attributes after the tag name (after `struct`/`union` for an anonymous
definition), before `{`, and entity attributes after the declarator:

```x
struct Record [[aligned(16)]] { u8 byte; } object;
struct Small { u8 byte; } separately_aligned [[aligned(16)]];
struct Outer { struct Inner { u8 byte; u32 word; } member [[packed]]; };
```

An explicit entity-side placement never falls back to the inline type when it
is invalid for the entity. These ownership constraints are checked when the
declaration survives expansion; captured syntax preserves the written
placement.

An attribute region, one or more attribute specifiers followed by a braced
group of external items, applies its attributes to each declaration and
definition in the group for which they are valid, as leading attributes: an
attribute is valid for a declaration when its subject matches, so function
attributes apply to functions (`abi`, `clobber`, and `stack_cleanup` also to
typedefs and objects of function or function-pointer type), symbol attributes
as the declaration's definition status and linkage allow, layout attributes
to typedefs and record definitions, and `underlying` to enumerations. An
attribute written on a declaration inside takes precedence over a region
attribute of the same name, regions nest, and a region attribute valid for
none of the region's declarations is an error. Like a `syntax` region, an
attribute region is a transparent declaration group that creates no namespace
or scope.

```text
[[abi("sysv_abi")]] {
    i32 c_read(in i32 fd, in void *buffer, in uptr count);
    i32 c_close(in i32 fd);
}
```

Core attributes use one identifier, for example `[[interrupt("irq")]]`. A target
may define qualified contextual names. Unknown attributes are errors.
Cross has no user-defined attributes or ignored annotation namespace;
procedural macros and activated syntax declarations provide explicit
user-defined source transformation.
`$::has_attribute("qualified_name")` tests the contextual spelling.

The following object, type, and symbol attributes are normative:

| Attribute | Contract |
| --- | --- |
| `aligned(N)` | Minimum alignment already specified by this document. |
| `packed` | Packed record or member layout already specified by this document. |
| `atomic` | Gives the attributed object or pointed-to type the atomic access contract. |
| `underlying(T)` | Selects the exact integer representation of an enumeration. |
| `may_alias` | Accesses through the attributed type may alias incompatible effective types. |
| `address_space(N)` | Selects a target address-space-qualified type. |
| `vector_size(B)` | Defines a fixed vector of total size B bytes. |
| `ext_vector_type(N)` | Defines a fixed vector with N lanes. |
| `scalable_vector(N)` | Defines a target-scalable vector with at least N lanes. |
| `section("name")` | Places a definition in the named object-file section. |
| `used` | Requires emission when a definition is otherwise emit-capable. |
| `retain` | Requires the object format/linker retention flag as well as emission. |
| `weak` | Gives an external definition weak binding. |
| `weakref("name")` | Emits a weak undefined reference to the given link name. |
| `alias("name")` | Defines the entity as an alias of a compatible definition with that link name. |
| `visibility("kind")` | Selects `default`, `hidden`, `protected`, or `internal` visibility when supported. |
| `noinit` | Places an uninitialized static-duration object in non-zeroed storage. |
| `address(N)` | On a declaration without an initializer or body: the entity is at the fixed address `N`, a nonzero `uptr` value; it has no link symbol, no storage is emitted, and references use that address. A definition of it in the group, or `alias`, `weakref`, or `thread_local` with it, is an error. |
| `exhaustive` | On a structure definition: a brace initializer of the structure must initialize each named member. |
| `thread_local` | Gives a static-duration object thread-local storage. |
| `tls_model("model")` | Selects a target TLS model for a thread-local entity. |
| `link_name("name")` | Replaces the default link name of a global function, object, or label. |

`section`, symbol binding, aliasing, visibility, retention, TLS, and `noinit`
are valid only where the selected object format can represent them. An alias
must have compatible type, size, alignment, address space, and ABI and must
resolve within the same final link. A `noinit` object shall not have an
initializer and its initial value is indeterminate. `used` prevents compiler
elimination but does not by itself stop section garbage collection; `retain`
does both when supported. A TLS model whose access sequence requires a resolver
call is unavailable in standalone translation; user code may instead declare
and call its own resolver explicitly.

### Named sections

`[[section("name")]]` places a `static`, compilation-group, or `global`
object/function definition in the exact object-file section named by the
nonempty string. Declarations and definitions of one entity must agree.

```x
[[section(".boot"), used, aligned(16)]]
global void boot_entry() {
    // No startup or entry semantics are implied by the section name.
}
```

It implies no linkage, retention, entry-point, or section flags. Those come
from other attributes, the target/object format, or a user linker script. A
global label remains in its function's section. Unrepresentable names or
placements are diagnosed. The definitions a source unit places in one named
section are emitted into that section contiguously in definition order, with
only the padding their alignments require; `-ffunction-sections` and
`-fdata-sections` affect only definitions without an explicit section, which
they place in a section named after the definition's link name in the object
format's convention.

The following function and statement attributes are normative:

| Attribute | Contract |
| --- | --- |
| `noreturn` | The function has no normal return. |
| `returns_twice` | One invocation can resume its caller more than once. |
| `abi("name")` | Selects or asserts a registered ABI. |
| `clobber("resource", ...)` | Declares additional machine-state effects. |
| `naked` | Suppresses the managed frame and requires a complete manual ABI. |
| `raw_inline` | Permits mandatory inline-and-verify use from naked code. |
| `eval_only` | Requires every direct call to evaluate and gives the function no runtime symbol/address. |
| `runtime_only` | Prevents translation-time entry into the function. |
| `macro` | Defines a one-token-tree procedural macro and implies `eval_only`. |
| `syntax_expander` | Defines a one-match syntax expander and implies `eval_only`. |
| `operator("token")` | Binds one exact unary or binary operator signature to this ordinary function. |
| `stack_cleanup("caller" or "callee")` | Selects ownership of a manual stack argument area. |
| `variadic(binding, ...)` | Names incoming ABI state used to traverse variadic arguments. |
| `always_inline` | Every call that can legally be inlined must be inlined; otherwise diagnose. |
| `noinline` | Calls to the function are not inlined. |
| `cold` / `hot` | Supplies a layout and optimization-frequency hint. |
| `interrupt("name")` | Selects a registered interrupt ABI of the target. |
| `no_stack_protector` | Suppresses stack-protector instrumentation for this function. |
| `no_sanitize("name")` | Suppresses the named optional instrumentation for this function. |
| `musttail` | On a `return` statement, requires a tail transfer or a diagnostic. |

`[[noreturn]]` is the sole no-return declaration spelling. Its calls perform
no copy-out; output-capable parameters are permitted but warned as unobservable.
Mandatory translation-time evaluation diagnoses a reached normal return or
fallthrough from a `noreturn` function, including an ordinary helper. Failed
evaluation retains its original failure; it is not a normal return.
`[[returns_twice]]` requires a stable ABI and only `in` parameters.
`interrupt` uses the complete entry/exit contract of its registered ABI and
cannot use a dynamic ABI.

Function declarations cannot carry the object-storage attributes `noinit`,
`thread_local`, or `tls_model`, or the return-statement attribute `musttail`.
The pairs `always_inline`/`noinline`, `eval_only`/`runtime_only` (including
implicit eval-only meta signatures), `hot`/`cold`, and `raw_inline`/`naked` are
conflicting contracts. These declaration constraints apply even to unused
functions, uninstantiated generic declarations, and translation-only helpers;
erasure or non-execution does not waive them. Parsed captures may retain such
declarations for inspection and discard; surviving declarations receive the
ordinary checks.

Managed stack allocation, alignment, saves, and restores are compiler-generated
instructions. CFI/SEH describe those instructions; they neither allocate the
frame nor supply an unwind runtime. Unwind tables are emitted only with
`-funwind-tables` or `-fasynchronous-unwind-tables`. Under the default
`-funwind-model=none`, `-felide-noreturn-saves` lets a function from which no
execution returns skip saving incoming callee-saved values; local storage, call
alignment, and values live across calls are always kept.
`-funwind-model=platform` keeps incoming frame state intact for a platform
unwinder; see [targets.md](targets.md#object-formats).

`[[musttail]] return callee(arguments);` is valid only when caller and
callee have compatible physical ABI, stack cleanup, result locations, and
clobber contracts. Any caller parameter outputs must either have been delivered
before the transfer or be forwarded in exactly the locations from which the
tail callee will deliver compatible results: a caller output passed directly
as an `out` or `inout` argument of the same type is forwarded, and the caller
delivers its other outputs, and the current value of an output forwarded to an
`inout` parameter, before the transfer. Destruction of managed dynamic
storage and restoration of the caller frame must also be expressible before
the tail transfer. Failure of any condition is a required diagnostic.
The returned expression, after ignoring redundant parentheses, must be a call.
This source-shape constraint applies even in untaken branches, unused helpers,
and uninstantiated generic definitions. A parsed capture may retain the source
for inspection or discard; surviving source receives the check.
During translation-time execution, a `[[musttail]]` return releases the
caller's frame before the callee runs, so a chain of tail calls does not count
against the evaluation depth limit. The physical requirements above apply only
to emitted functions; a translation-only function needs only the call shape.

Target descriptions may add qualified contextual attributes and must document
their contracts. Cross does not recognize `__attribute__`, `__declspec`, or
vendor-qualified compatibility aliases.

## Compilation and separate compilation

`cc` preprocesses each primary input independently, preserves its namespace and
import structure, collects visible procedural macros, expands explicit `name!`
regions, parses the resulting Cross, collects declarations, instantiates
explicit generics, resolves the target data model and applicable names/types,
and validates source constraints before replacing evaluated expressions.
Required and opportunistic evaluation use that target's conversion and
arithmetic rules; staging cannot suppress constraint diagnostics. The compiler
then completes group-wide checking, call-graph construction, ABI
allocation, optimization, and code generation. Macro state never crosses a
primary-input boundary; compatible declarations and undecorated definitions do
cross source units in the same group. File order does not affect semantic
resolution. A procedural invocation requires its macro definition to be
visible through its primary input, normally by textual inclusion.

The output of a compilation is a function of its inputs, options, loaded
models, and compiler build: the same command on the same files produces
byte-identical preprocessed, assembly, object, and dependency output
regardless of the working directory, environment, host, thread count, or
allocator behavior. A source unit is identified by its primary input's path as
written on the command line, after `-ffile-prefix-map=OLD=NEW` replaces a
leading `OLD` with `NEW` (when several mappings match, the last one applies);
`$::source::file`, source-unit identities,
implementation-internal symbol names, and debugging information use that
spelling, never a path the compiler resolved. Within a source unit and
section, definitions are emitted in definition order, and source units in
command-line order, so adding a definition to one source unit changes no other
unit's symbols or layout.

All inputs of one `cc` command form one compilation group with one output;
objects produced by separate commands belong to separate groups.
At the end of a group:

1. a visible compatible definition resolves the reference and may use a
   dynamic ABI;
2. otherwise a declared function becomes an external reference using its
   complete stable manual ABI or the selected registered ABI;
3. an undeclared call is an error.

Consequently, an undecorated `mod::foo2` definition can satisfy calls when
`mod.x` and `app.x` are compiled together. When compiled separately, the caller
emits an external reference but the defining object exposes no external symbol,
so linking fails. Marking the definition `global` makes separate linking valid.

## Interoperation

### C interfaces

A C-facing function shall be `global`, explicitly select the platform ABI,
have a C link name when required, and use `in` for every parameter passed by C
value. The default Cross ABI is not a C ABI:

```x
[[abi("sysv_abi"), link_name("write")]]
global i32 c::write(in i32 fd, in const void *buffer, in uptr count);
```

An `out T` or `inout T` platform-ABI parameter is compatible with a C `T *`
channel, not a C `T` parameter. The C implementation receives a pointer to a
temporary channel, so it may store the result through that pointer.

Structures, unions, enumerations, bit-fields, variadic arguments, and ordinary
results are interoperable only when their Cross declarations match the selected
C ABI's layout and promotion rules.

Cross type spellings do not depend on a C implementation's source keywords.
At an interface, `iN` and `uN` match a C integer type only when that type has
the same N-bit representation and the ABI gives it the same classification;
`i128/u128` require a C extended integer with the same representation and ABI
classification. `f32` and `f64` analogously require binary32 and binary64
representation and classification. `f80` or `f128` matches a C extended
floating type only when its value format, object layout, and ABI classification
all agree; the spelling `long double` alone establishes none of these facts.
`iptr` and `uptr` match suitable pointer-width C integer types,
not object pointers themselves. `fptr` matches a C floating type only when its
registered format and ABI class agree. `bool` is an eight-bit Cross integer and
is not assumed to match any particular C boolean type. Headers shared across
languages should define their C-facing spellings explicitly and verify layout.

The `abi` name should match the producing toolchain's conventional name.
For example, an x86-64 interface may use `abi("sysv_abi")` or
`abi("ms_abi")`, while a MIPS interface may use `abi("o32")` or `abi("n64")`.
Aliases resolve to the same canonical function type.

### Assembly interfaces

Assembly-facing functions should declare every fixed operand location, ordinary
result location, clobber, stack-cleanup rule, and exact link name. Omitted
information is filled from the named registered ABI; use a target-defined
`abi("none")` or equivalent only when that entry exists and the interface is
fully specified.

An assembly implementation of a result-bearing parameter must place the final
value at its output endpoint. For `*reg` or standard pointer-channel lowering,
it stores the final value through the supplied address before normal return.

### Cross version compatibility

Pre-1.0 drafts do not promise source or binary compatibility with one another.
A compiler need not accept source or objects of another draft. Every source unit
in one compilation group uses the compiler's advertised draft, and separately
compiled objects must agree on every interface they share.

A global interface's parameter modes and ABI contract are part of its binary
interface even though the default Cross link-name encoding does not encode
them. Changing a mode, location, cleanup rule, target data model, target
features used by ABI classification, or selected registered ABI requires
recompiling callers or changing the link name.

`$::language::version` lets source test the draft it is compiled under; it does
not adapt an existing binary interface.

## Toolchain interface

### `cpp`: Cross preprocessor

```text
cpp [options] input.x ...
```

`cpp` implements this document's preprocessing pipeline and emits Cross
accepted directly by `cc`.

The core options are:

```text
-Dname[=value]  define a macro
-Uname          undefine a macro
-Ipath          add an include/embedded-asset search directory
-isystem path   add an include/embedded-asset search directory
-include file   force an include
-imacros file   process a file for macro definitions only
-o file         select output
-M/-MM          emit dependencies
-MD/-MMD        emit source and dependencies
-MF/-MT/-MQ     control dependency output
-MP             add phony targets for prerequisites
-ffile-prefix-map=OLD=NEW
                spell source paths starting with OLD as NEW
-target triple  select the target macro and feature environment
-march=name     select target instruction compatibility
-mtune=name     select target scheduling and cost preferences
-moption/-mno-option/-moption=value
                set a registered target option
-mabi=name      select the target ABI macro and registry environment
-mmangling=name select the name-mangling model
-mprofile=name  select a compiler-model profile
--model=file    load one explicit compiler-model file
--model-path=dir
                add a compiler-definition search directory
--help          show command help
--version       show tool version
```

`.i` denotes saved canonical Cross. `cc` accepts it without preprocessing and
recovers source units/positions from compiler-generated markers.

Joined and separated option arguments are accepted where shown; unknown options
are rejected. Options and macro state apply independently to each primary
input. `-include` precedes that input; `-imacros` retains only definitions.

Inputs are emitted in command-line order with unit boundaries and line markers,
to standard output or the single `-o` file. `-E` is the explicit/default
standalone `cpp` mode.

### `cc`: Cross compiler driver

```text
cc [options] input.x ...
```

The required core options are:

```text
-E              preprocess only
-S              emit assembly
-c              emit object code
-o file         select the output
-I/-D/-U        pass preprocessing controls
-target triple  select the target
-march=name     select target instruction compatibility
-mtune=name     select target scheduling and cost preferences
-mabi=name      select the default compilation ABI from loaded models
-mmangling=name select a loaded name-mangling model
-mprofile=name  select a loaded compiler-model profile
--model=file    load one explicit compiler-model file
--model-path=dir
                add a compiler-definition search directory
-O0/-Og/-O1/-O2/-O3/-Os/-Oz
                select a loaded standard optimization preset
-O=name         select any loaded optimization preset
-g[=name]       emit debugging information per a loaded debug entry
-g0             emit no debugging information
-foption/-fno-option/-foption=value
                set a registered target-independent compiler option
-moption/-mno-option/-moption=value
                set a registered target option
--print-abis    list the selected target's ABI registry
--print-models  list loaded model sources
--print-profiles list loaded profiles
--print-manglings list loaded name-mangling models
--print-optimizations list loaded optimization presets
--print-options[=category]
                list registered options, values, availability, and origins
--print-keywords list every reserved source keyword
--print-builtins list all $:: built-ins and their category
--print-attributes list contextual [[attribute]] names
--print-instructions list hardware instructions and their feature gates
--print-features list language and target feature-query names
```

Every `-O` spelling selects a preset, defined by an optimization model (see
[models.md](models.md#optimization-entries)), that sets `-f` options and, in a
target-specific preset, the `-m` options the target allows presets to set. A
preset only sets options: every effect of a preset can be reproduced or
overridden with explicit `-f` and `-m` options, and `cc --print-options` shows
each resolved value and its origin. Work required for correctness, such as ABI
conformance and `always_inline`, does not depend on options.

The standard presets are target-independent and never imply `fast-math`,
function/data sections, a required ISA, a different selected registered ABI or
external calling convention, a code model, or a runtime contract.
`cc --print-options` lists every option, and [invoke.md](invoke.md) describes
the commonly used ones.

Resolution uses registered defaults, then the selected optimization preset,
then the profile's option settings, then explicit command-line `-f`/`-m`
settings. A direct option therefore overrides a preset independently of its
position relative to `-O`; repeated direct settings use the last occurrence.
An impossible explicit value and every unknown `-f`/`-m` name are errors rather
than ignored requests.

Model loading and profile resolution precede target queries and preprocessing.
Source `-I` paths are never model paths. A command-line `-O` replaces the
profile's default optimization entry without erasing the profile's deliberate
option overrides. Explicit command-line target, ABI, mangling, and `-f`/`-m`
settings override their corresponding profile defaults independently of
argument order. Duplicate model names/aliases (for ABIs, within one
architecture and address width) and explicitly requested missing entries are
errors.

`-mabi=help` is accepted as an alias for `--print-abis`. ABI discovery occurs
after model, profile, `-target`, `-march`, and `-mtune` selection. For every
entry, the listing shows its canonical name, aliases, bank/rule counts, whether
it is compilation-selectable and/or function-selectable, and relevant target
constraints. The output identifies the target/profile default and currently
selected entry. The model syntax and lookup order are defined in
[models.md](models.md).

`cc` does not link. It writes preprocessed Cross, assembly, or object files
that the target's assemblers and linkers accept; the user links them with the
startup objects, libraries, linker scripts, and entry symbol they choose.
Linker options such as `-e`, `-L`, and `-l`, and options conventionally named
`-nostdlib`, `-nodefaultlibs`, `-nostartfiles`, `-ffreestanding`, and
`-fno-builtin`, have no Cross contract and are rejected with an explanatory
diagnostic rather than silently accepted as no-ops.

The names `cpp` and `cc` also commonly name host C tools. Builds should use an
explicit Cross toolchain path when both are installed.

### Program entry

`main` has no special semantics. The user gives the linker a `global`
function's exact link name as the entry symbol or links a startup object; Cross
supplies no adapter. The entry may be a `global label`, but the user must
establish its required stack and machine state.

## Diagnostics and conformance

A compiler shall diagnose every violated *shall*, *must*, unavailable target
facility, and constraint stated by this specification. In particular, it shall
reject:

- malformed or unresolved names, declarations, attributes, locations, queries,
  labels, calls, or definitions;
- invalid scalar, enumeration, bit-field, vector, address-space, alignment,
  packing, atomic, or volatile use;
- invalid generic declarations/arguments/instances, failed constant
  evaluation, unhygienic or cyclic procedural expansion, and forbidden
  compile-time access to host or runtime state;
- reads of unassigned `out` cells, writes to `const` parameter cells, missing
  `out` assignment on normal return, and incompatible copy-out destinations;
- unknown, unavailable, mismatched, or data-model-incompatible ABIs; incomplete
  custom contracts; invalid variadic state; overlapping locations; and illegal
  stack-cleanup/output combinations;
- register-class violations, inconsistent raw-stack depth, unbalanced returns,
  illegal VLA entry, invalid computed goto, forbidden `naked` behavior, and
  a `raw_inline` call that cannot be completely legalized without a spill;
- unknown, unavailable, ambiguous, or ill-typed instructions/intrinsics and any
  failed mandatory optimization such as `always_inline` or `musttail`;
- an unavailable, duplicated, resized, constant-propagated, or otherwise
  unstable `$::patch` value/cell; invalid/static-inexpressible address sink;
  sink reuse across concrete generic instances; forbidden patch-bearing
  `raw_inline`/`always_inline`; or unavailable final-link preservation;
- incompatible linkage, section, layout, or object-format requirements; and
- any lowering that would create an undeclared call or helper symbol.

Forming a potentially misaligned pointer to a packed member requires at least a
warning. Unsupported foreign syntax is rejected. Recovery is permitted after a
diagnostic, but the compiler shall not silently change a declared interface.
For generated source, the primary diagnostic identifies the generated token
and is followed by the complete procedural-expansion chain, including each
invocation and definition. For translation-time execution, notes identify each
active evaluated call. These origin notes use original source paths.

### Implementation-defined target contract

Each target description shall document:

- scalar, 128-bit integer/binary128, pointer, label, and floating
  representations; alignments;
  endianness; nulls; address spaces; and mapped-address assumptions;
- record/bit-field packing, unaligned access, volatile access granularity,
  atomic widths/orderings, and vector types/operations;
- registers, views, overlaps, classes, stack layout/alignment, red zones, home
  areas, and all manual location strings;
- every ABI name/alias, selection scope, data model, availability predicate,
  compatibility relation, variadic state, and complete lowering contract;
- link-name/object-format capabilities, relocations, sections, visibility,
  binding, retention, TLS models, and linker/assembler limits;
- every instruction and intrinsic form, operands, effects, feature gates,
  patch-value materializers, patchable operand fields, and runtime-free
  lowering; and
- predefined macros/queries plus trap, privilege, unwind, exception, and exact
  machine-region behavior; and
- the debugging-information formats its `debug` entries may name.

Delegated behavior is implementation-defined by that target description, not
undefined merely because this common document delegates it.

## Grammar summary

This grammar is a compact, self-contained description of Cross source after
lexical preprocessing. Semantic restrictions in the preceding sections still
apply; for example, accepting an attribute's balanced arguments does not make
an unknown attribute valid.

```text
translation_unit
    = { external_item } ;

external_item
    = namespace_declaration
    | using_declaration
    | declaration
    | function_definition
    | global_label_declaration
    | static_assert_declaration
    | syntax_declaration
    | syntax_activation
    | syntax_region
    | attribute_region
    | syntax_invocation
    | macro_invocation ;

attribute_region
    = attribute_specifier { attribute_specifier }
      "{" { external_item } "}" ;

namespace_declaration
    = "namespace" namespace_name "{" { external_item } "}" ;
using_declaration
    = "using" namespace_name ";" ;
namespace_name
    = identifier { "::" identifier } ;

declaration
    = { attribute_specifier }
      declaration_specifiers [ init_declarator_list ]
      { attribute_specifier } ";"
    | { attribute_specifier } structured_function_header_splice
      { attribute_specifier } ";" ;

function_definition
    = { attribute_specifier }
      declaration_specifiers declarator
      { attribute_specifier }
      compound_statement
    | { attribute_specifier } structured_function_header_splice
      { attribute_specifier } compound_statement ;

function_header
    = { attribute_specifier }
      declaration_specifiers declarator
      { attribute_specifier }
    | { attribute_specifier } structured_function_header_splice
      { attribute_specifier } ;

declaration_specifiers
    = declaration_specifier { declaration_specifier } ;
declaration_specifier
    = "typedef" | "static" | "global" | "register" | "stack" | "inline"
    | type_qualifier | type_specifier | attribute_specifier ;
type_qualifier
    = "const" | "volatile" | "restrict" ;
type_specifier
    = scalar_type | struct_or_union_specifier | enum_specifier
    | typedef_name | target_scalar_builtin_name | structured_type_splice ;
typedef_name
    = qualified_name ;
scalar_type
    = "void" | "bool"
    | "i8" | "i16" | "i32" | "i64" | "i128" | "iptr"
    | "u8" | "u16" | "u32" | "u64" | "u128" | "uptr"
    | "f32" | "f64" | "f80" | "f128" | "fptr" | "label" ;

struct_or_union_specifier
    = ("struct" | "union")
      [ qualified_name [ generic_parameter_list | generic_arguments ] ]
      { attribute_specifier } [ "{" { member_declaration } "}" ] ;
member_declaration
    = { attribute_specifier } declaration_specifiers
      [ member_declarator { "," member_declarator } ] ";" ;
member_declarator
    = [ declarator ] [ ":" constant_expression ]
      { attribute_specifier } ;

enum_specifier
    = "enum" [ qualified_name ] { attribute_specifier }
      [ "{" enumerator { "," enumerator } [ "," ] "}" ] ;
enumerator
    = identifier [ "=" constant_expression ] ;

init_declarator_list
    = init_declarator { "," init_declarator } ;
init_declarator
    = declarator [ object_location ] { attribute_specifier }
      [ "=" initializer ] ;

declarator
    = { pointer_part } direct_declarator ;
pointer_part
    = "*" { type_qualifier | attribute_specifier } ;
direct_declarator
    = (qualified_name [ generic_parameter_list ] | "(" declarator ")")
      { array_suffix | function_suffix } ;
array_suffix
    = "[" [ assignment_expression ] "]" ;
function_suffix
    = "(" parameter_list ")" [ result_location ]
      { attribute_specifier } ;
generic_parameter_list
    = "<" generic_parameter { "," generic_parameter } ">" ;
generic_parameter
    = identifier | type_name identifier ;

parameter_list
    = /* empty */ | "void"
    | parameter_declaration { "," parameter_declaration } [ "," "..." ] ;
parameter_declaration
    = { attribute_specifier } [ parameter_mode ]
      declaration_specifiers [ declarator | abstract_declarator ]
      [ location ] { attribute_specifier } ;
parameter_mode
    = "in" | "out" | "inout" ;
location
    = string_literal ;
object_location
    = string_literal ;
result_location
    = "->" string_literal ;

abstract_declarator
    = { pointer_part }
      [ "(" abstract_declarator ")" { array_suffix | function_suffix }
      | array_suffix { array_suffix | function_suffix }
      | function_suffix { array_suffix | function_suffix } ] ;
type_name
    = declaration_specifiers [ abstract_declarator ] ;

initializer
    = assignment_expression
    | "{" [ initializer_entry { "," initializer_entry } [ "," ] ] "}" ;
initializer_entry
    = { designator } [ "=" ] initializer ;
designator
    = "." identifier | "[" constant_expression "]" ;

attribute_specifier
    = "[[" attribute { "," attribute } "]]" ;
attribute
    = attribute_name [ "(" [ balanced_token_sequence ] ")" ] ;
attribute_name
    = identifier { "::" identifier } ;

global_label_declaration
    = { attribute_specifier } "global" "label"
      qualified_label_name { attribute_specifier } ";" ;
qualified_label_name
    = qualified_function_name "::" identifier ;
static_assert_declaration
    = "$::static_assert" "(" constant_expression ","
      string_literal ")" ";" ;

statement
    = { attribute_specifier } unattributed_statement ;
unattributed_statement
    = labeled_statement | compound_statement | expression_statement
    | selection_statement | iteration_statement | jump_statement
    | declaration | static_assert_declaration | syntax_activation
    | syntax_invocation | macro_invocation ;
compound_statement
    = "{" { statement | using_declaration } "}" ;
expression_statement
    = [ expression ] ";" ;
labeled_statement
    = identifier ":" statement
    | "label" identifier ":" statement
    | "global" "label" identifier ":" statement
    | "case" constant_expression ":" statement
    | "default" ":" statement ;
selection_statement
    = "if" "(" expression ")" statement [ "else" statement ]
    | "switch" "(" expression ")" statement ;
iteration_statement
    = "while" "(" expression ")" statement
    | "do" statement "while" "(" expression ")" ";"
    | "for" "(" for_initializer ";" [ expression ] ";"
      [ expression_list ] ")" statement ;
for_initializer
    = /* empty */ | expression_list | declaration_without_final_semicolon ;
expression_list
    = expression { "," expression } ;
jump_statement
    = "break" ";" | "continue" ";"
    | "return" [ expression ] ";"
    | "goto" assignment_expression ";" ;

expression
    = assignment_expression ;
assignment_expression
    = conditional_expression
      [ assignment_operator assignment_expression ] ;
assignment_operator
    = "=" | "*=" | "/=" | "%=" | "+=" | "-="
    | "<<=" | ">>=" | "&=" | "^=" | "|=" ;
conditional_expression
    = logical_or_expression
      [ "?" expression ":" conditional_expression ] ;
logical_or_expression
    = logical_and_expression { "||" logical_and_expression } ;
logical_and_expression
    = inclusive_or_expression { "&&" inclusive_or_expression } ;
inclusive_or_expression
    = exclusive_or_expression { "|" exclusive_or_expression } ;
exclusive_or_expression
    = and_expression { "^" and_expression } ;
and_expression
    = equality_expression { "&" equality_expression } ;
equality_expression
    = relational_expression { ("==" | "!=") relational_expression } ;
relational_expression
    = shift_expression { ("<" | "<=" | ">" | ">=") shift_expression } ;
shift_expression
    = additive_expression { ("<<" | ">>") additive_expression } ;
additive_expression
    = multiplicative_expression { ("+" | "-") multiplicative_expression } ;
multiplicative_expression
    = cast_expression { ("*" | "/" | "%") cast_expression } ;
cast_expression
    = unary_expression | "(" type_name ")" cast_expression ;
unary_expression
    = postfix_expression
    | ("++" | "--") unary_expression
    | ("&" | "*" | "+" | "-" | "~" | "!") cast_expression
    | "sizeof" unary_expression
    | "sizeof" "(" type_name ")" ;
postfix_expression
    = primary_expression
      { "[" expression "]"
      | "(" argument_list ")"
      | "." identifier | "->" identifier
      | "++" | "--"
      | generic_arguments } ;
argument_list
    = /* empty */ | assignment_expression
      { "," assignment_expression } ;
primary_expression
    = qualified_name | builtin_name | literal
    | "$::alignof" "(" (type_name | expression) ")"
    | "$::offsetof" "(" type_name "," member_designator ")"
    | "$::atomic_is_lock_free" "(" (type_name | argument_list) ")"
    | "(" expression ")" | embed_expression
    | quote_expression | syntax_invocation | macro_invocation
    | structured_expression_splice ;

member_designator
    = identifier { "." identifier | "[" constant_expression "]" } ;
generic_arguments
    = [ "::" ] "<" generic_argument { "," generic_argument } ">" ;
generic_argument
    = type_name | constant_expression ;
constant_expression
    = conditional_expression /* required to evaluate during translation */ ;
embed_expression
    = "$::embed" "(" string_literal ")" ;
quote_expression
    = "$::quote" "{" balanced_tokens "}" ;

macro_invocation
    = qualified_name "!" balanced_token_tree ;
balanced_token_tree
    = "(" balanced_tokens ")" | "[" balanced_tokens "]"
    | "{" balanced_tokens "}" | "[[" balanced_tokens "]]" ;
balanced_tokens
    = { preprocessing_token | balanced_token_tree } ;

syntax_declaration
    = "syntax" identifier ":" syntax_kind "{" syntax_body "}" ;
syntax_kind
    = "item" | "statement" | "expression" | "rule" | "bundle" ;
syntax_body
    = "prefix" string_literal ";" "match" pattern ";"
      "expand" qualified_name ";"
    | "match" pattern ";"
    | "use" activation_entry ";" { "use" activation_entry ";" } ;
activation_entry
    = qualified_name [ "as" identifier ] ;
syntax_activation
    = "syntax" activation_entry { "," activation_entry } ";" ;
syntax_region
    = "syntax" "(" activation_entry { "," activation_entry } ")"
      "{" { external_item } "}" ;
syntax_invocation
    = /* active prefix and its definition's uniquely matched tokens */ ;
pattern
    = pattern_element { pattern_element } ;
pattern_element
    = string_literal | identifier ":" capture_spec
    | "rule" "(" qualified_name ")" ;
capture_spec
    = "ident" | "name" | "literal" | "paren" | "bracket" | "block"
    | "group" | "function" | "function_raw"
    | "expr" | "stmt" | "type" | "declaration"
    | "function_header" | "function_decl" | "function_def"
    | "tokens_until" "(" string_literal ")"
    | "rule" "(" qualified_name ")"
    | ("optional" | "repeat0" | "repeat1") "(" pattern ")"
    | ("separated0" | "separated1") "(" pattern "," string_literal ")"
    | "choice" "(" identifier ":" "(" pattern ")"
      { "|" identifier ":" "(" pattern ")" } ")" ;

qualified_name
    = identifier { "::" identifier } ;
qualified_function_name
    = qualified_name ;
builtin_name
    = "$::" identifier { "::" identifier } ;
target_scalar_builtin_name
    = builtin_name ;
literal
    = integer_literal | floating_literal | character_literal | string_literal ;
```

`declaration_without_final_semicolon` is `declaration` with its final `;`
supplied by the `for` syntax. `balanced_token_sequence` follows the same
balancing rule as `balanced_tokens` but excludes its outer delimiter.
Nested attribute delimiters `[[` and `]]` are one balanced-token-tree
occurrence, not two `[` or `]` tokens. A procedural invocation's outer token
tree is still restricted to `(...)`, `[...]`, or `{...}`; the attribute
alternative describes groups inside balanced sequences such as quote contents
and attribute arguments. `structured_expression_splice` is a translation-only
public-tree alternative, not a source token spelling. Its `primary_expression`
node has exactly one child: the original category-compatible expression node.
The splice keeps that child's root and lexical identity; it does not project or
re-associate its contents with surrounding operators.
`structured_function_header_splice` likewise contributes the original
category-compatible `function_header` node, and `structured_type_splice`
contributes the original type node under a `type_specifier`. Neither denotes
a source token spelling. These alternatives preserve node identity during
composition rather than flattening the retained subtree into terminals.
The `function_header` parse category has a `function_header` public-tree root
and excludes the following body or semicolon. The `function_decl` category
uses a `declaration` root restricted to a direct function prototype, and
`function_def` uses `function_definition`. These categories require a direct
function declarator, not an object containing a function-pointer type.
At file or namespace scope, a prototype accepted by `function_decl` is also
accepted by `declaration`, including when its header must remain deferred for
pending generic bindings. Choosing the broader category does not bind earlier
type names before those bindings are known.
`preprocessing_token`, literal spelling, `identifier`, and `builtin_name` are
defined by the lexical section. A typedef name or target scalar built-in is
accepted as a type specifier only after semantic lookup confirms that category.

Declaration specifiers may be separated by whitespace/newlines and appear in
any unambiguous order. `global` is permitted only at file/namespace scope.
`static` at block scope selects static storage; at file/namespace scope it
selects source-unit linkage. `object_location` is valid only on a hard-bound
`register` object.

`generic_parameter_list` is valid only on a direct function declaration or
definition and on the tag of a record or union definition or forward
declaration. Its parameters are in scope for the preceding result type as
specified under Generic functions. A `generic_arguments` form without `::`
is selected only when lookup finds a generic entity, and always after a tag
keyword. `syntax_invocation`
is determined by the active lexical syntax bindings and their bounded
patterns; its output must satisfy the declared item, statement, or
expression category.

For `goto`, a bare identifier found in the current function's label namespace
is a direct target; every other assignment expression must have type `label`.
Macro invocations are expanded before their surrounding grammar production is
finally parsed, so their output—not the placeholder nonterminal—must be valid
at that position.

At file or namespace scope, `global label q::name;` is a global-label
declaration only when `q` resolves to a previously declared function;
otherwise the tokens follow ordinary qualified object-declaration rules. A
header should therefore declare the enclosing function before its global
labels.

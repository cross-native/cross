# The Cross language

Cross keeps C99's statements, expressions, declarators, and preprocessor, and
changes the parts that C leaves implicit: type widths, linkage, parameter
dataflow, evaluation order, ABIs, and every compiler-provided facility. This
guide assumes knowledge of C and describes the differences; the language
specification, [spec.md](spec.md), is the complete reference.

A Cross program gets no library, runtime, startup code, or entry point. The
compiler never inserts a call that the source did not write; an operation the
target cannot perform inline, such as `f64` arithmetic on a single-precision
FPU, is an error rather than a hidden helper call.

## Lexical structure

Source files are UTF-8 and use the `.x` extension. Comments, identifiers
(ASCII only), and punctuation follow C. Compiler-provided names live under
`$::`, such as `$::alignof` or `$::_add`; no other identifier contains `$`,
and no identifier is reserved for its leading underscores.

The keywords are:

```text
bool break case const continue default do else enum f32 f64 f80 f128 for fptr
global goto i8 i16 i32 i64 i128 if in inline inout iptr label namespace out
register restrict return sizeof stack static struct switch syntax typedef u8
u16 u32 u64 union u128 uptr using void volatile while
```

`int`, `char`, `long`, `float`, `double`, `extern`, `auto`, `true`, and
`false` are ordinary identifiers.

| Literal | Type |
| --- | --- |
| `42` | First of `i32`, `i64`, `i128` that fits. |
| `0xff`, `0b1010`, `017` | First of `i32`, `u32`, `i64`, `u64`, `i128`, `u128` that fits. |
| `4u32`, `0iptr`, `255u8` | The suffixed type; the value must fit. |
| `1.5`, `1.5f32` | `f64`, or the suffixed `f32`, `f64`, `f80`, `f128`, or `fptr`. |
| `'a'` | `u32`. |
| `"text"` | `const u8[N]`, UTF-8 with a terminating zero. |

Wide or prefixed strings and C's `u`, `l`, `ul` suffixes do not exist.

## Types

| Type | Meaning |
| --- | --- |
| `i8` ... `i128`, `u8` ... `u128` | Two's-complement integers of exactly that width. |
| `iptr`, `uptr` | Pointer-sized integers. `sizeof` yields `uptr`. |
| `f32`, `f64` | IEEE binary32 and binary64. |
| `f80`, `f128` | x87 extended and IEEE binary128, where the target supports them. |
| `fptr` | Pointer-sized floating type (`f32` or `f64`); not a pointer. |
| `bool` | Eight bits holding 0 or 1. Comparisons and `!` produce `bool`. |
| `label` | A code address; see [Labels](#labels-and-computed-goto). |

Pointers, arrays, `struct`, `union`, `enum`, bit-fields, `typedef`, and
`const`/`volatile`/`restrict` work as in C. Integer promotion and the usual
arithmetic conversions follow C's rules applied to exact widths: `bool`, 8-bit,
and 16-bit values promote to `i32`. Signed overflow, division by zero, and
out-of-range shifts are undefined. Floating expressions round to their own
type; there is no excess precision.

```x
enum color [[underlying(u8)]] {
    red,
    green = 5,
    blue,
};

struct pixel {
    u16 x;
    u16 y;
    enum color shade;
};

typedef u32 u32x4 [[vector_size(16)]];   // four u32 lanes
```

An enumeration uses `i32` unless `[[underlying(T)]]` selects another integer.
Atomic objects are written `T [[atomic]]`, and an address space
`T [[address_space(N)]] *`. Records support `[[packed]]`. `[[aligned(N)]]`
raises the alignment of an object, record, member, or typedef. A typedef's
request applies to storage of its type: every object, member, and array
element of the type is aligned, and the type's size rounds up to the
alignment. Values of the type convert, compare, and are passed and returned
like the base type; function types, pointer conversions, and dereferences use
the base type. Packing does not lower a member below the alignment its type
requests. A bit-field cannot have such a type, and a vector's lanes carry no
request, although a vector typedef can request alignment for the whole vector.

```x
typedef u32 line_u32 [[aligned(64)]];   // sizeof and $::alignof are both 64

struct counters {
    line_u32 hits;                      // offset 0
    line_u32 misses;                    // offset 64
};

$::static_assert(sizeof(struct counters) == 128uptr, "one line per counter");
```

`[[may_alias]]` exempts a type from the effective-type aliasing rules, which
otherwise follow C (any object may be accessed through `u8` or `i8`). Write it
after a typedef's name or as a qualifier, as in `u32 [[may_alias]] *`.
Converting between unrelated pointer types still goes through `void *`:

```x
typedef u32 any_u32 [[may_alias]];

u32 float_bits(in f32 value) {
    f32 copy = value;
    return *(any_u32 *)(void *)&copy;   // the representation of value
}
```

## Declarations and linkage

At file and namespace scope, a declaration without an initializer or body
declares only; there are no tentative definitions and no `extern`. A
definition has one of three linkages:

| Form | Visible to | Exported to the linker |
| --- | --- | --- |
| `static` | Its input file and that file's includes | No |
| no keyword | Every input of the same `cc` command | No |
| `global` | Everything it is linked with | Yes |

```x
u32 counter;                    // declaration only
static u32 hidden;              // zero-initialized, private to this file
global u32 exported = 7;        // exported definition

u32 helper(u32 x) {             // shared with the other inputs of this command
    return x + hidden;
}
```

A `static` or `global` object without an initializer is zero-initialized.
Exported names are the qualified source names (`net::send`) unless
`-mmangling` selects another encoding or `[[link_name("send")]]` gives an exact
spelling. Initializers support C99 designators; missing members are zero, and
static initializers must be constants or relocatable addresses. No
initialization code runs before your entry point.

Local storage can be constrained: `register T x;` has no address and never
spills, `stack T x;` always gets an addressable frame slot, and
`register T x "rax";` binds a register by name; the function still restores a
bound register that its ABI preserves. Arrays with runtime bounds are
allocated in the frame and released when their block exits; there is no
`alloca`.

## Functions and parameters

Each parameter has a mode that states its dataflow:

| Mode | Meaning |
| --- | --- |
| `in` (default) | Copied in. The callee may modify its own copy; add `const` to forbid that. |
| `out` | Uninitialized on entry, must be assigned, copied back on normal return. |
| `inout` | Copied in and copied back on normal return. |

```x
global void divide(in u32 a, in u32 b, out u32 quotient, out u32 remainder) {
    quotient = a / b;
    remainder = a % b;
}

global u32 digits(u32 value) {
    u32 count = 1;
    u32 rest;
    divide(value, 10, value, rest);   // copy-out writes `value` after the call
    while (value != 0) {
        divide(value, 10, value, rest);
        count += 1;
    }
    return count;
}
```

Every parameter is a distinct copy, so modes never create aliases. Copy-out
happens left to right after the return value is computed. An `out` argument
that is not an lvalue, such as a constant, receives a discarded temporary.

Other rules:

- `f()` and `f(void)` both declare a function with no parameters. There are no
  unprototyped functions, no implicit declarations, and no overloading.
- `inline` permits inlining; it never changes linkage. `[[always_inline]]`,
  `[[noinline]]`, `[[hot]]`, `[[cold]]`, and `[[noreturn]]` work as in GCC.
- `[[musttail]] return f(x);` requires a tail call or fails to compile. During
  compile-time evaluation it also replaces the caller's frame, so tail-call
  chains do not count toward the recursion limit.
- Variadic functions take `...` after a named parameter, but there is no
  `va_list`; a definition reads its arguments through
  `[[variadic(...)]]` bindings of the target ABI's state.
- A function may define an operator for a nominal record, union, or enum type
  with `[[operator("+")]]`. There are no other overloads and no user-defined
  conversions.

## Expressions and statements

Statements are C99's. The differences in expressions:

- Operands, arguments, and both sides of an assignment are evaluated left to
  right. Nothing is unsequenced.
- There is no comma operator. Commas only separate declarators, arguments, and
  initializers, so `for (u32 i = 0, n = 10; i < n; ++i)` is valid but
  `i = 0, j = 1` is not.
- Pointer comparisons with integer constant zero, conversions through
  `void *`, and casts follow C. Function pointers and `void *` do not convert.
- `$::assume(condition)` and `$::unreachable()` state facts the optimizer may
  use; neither adds a check.

## Namespaces

```x
namespace geometry {
    struct point {
        i32 x;
        i32 y;
    };

    i32 dot(struct point a, struct point b) {
        return a.x * b.x + a.y * b.y;
    }
}

using geometry;   // import every name of the namespace into this scope

global i32 length_squared(struct point p) {
    return dot(p, p);
}
```

Functions, objects, types, tags, enumerators, and macros declared inside
`namespace a::b { ... }` are named `a::b::name`. A qualified name may be used
or defined anywhere an identifier can appear, for example
`u32 mod::seed = 7;`. Unqualified lookup searches the local scope, the
enclosing namespaces from the innermost, `using` imports, and then the global
namespace. `using` imports a whole namespace for the rest of the enclosing
block, namespace body, or file. There are no anonymous namespaces, aliases,
absolute `::name` spellings, or single-name imports.

## Preprocessor

The preprocessor is C99's with these changes:

- `$::macro::args` replaces `__VA_ARGS__`, and `$::source::file` and
  `$::source::line` replace `__FILE__` and `__LINE__`. There are no date or
  time macros.
- Includes search only the including file's directory and explicit `-I` and
  `-isystem` directories. `#pragma once` is supported.
- `#require condition, "reason"` fails translation when the condition is
  false. `#if` expressions do not support `?:`.
- Predefined macros describe the target, for example `$::target::triple`,
  `$::target::pointer_bytes`, `$::target::byte_order`, and
  `$::language::version`.
- Queries test what the compiler and target provide: `$::has_feature`,
  `$::has_builtin`, `$::has_intrinsic`, `$::has_instruction`,
  `$::has_attribute`, `$::has_abi`, `$::has_mangling`, `$::has_profile`, and
  `$::has_include`.

```x
#require $::has_abi("sysv_abi"), "needs the System V ABI"

#if $::has_feature($::feature::integer128)
global u128 wide_product(u64 a, u64 b) {
    return (u128)a * (u128)b;
}
#endif
```

## Compile-time evaluation

Any ordinary function can run during compilation. `cc` tries each call whose
arguments are known and keeps the call for runtime when it cannot finish it.
Initializers of static objects, array bounds, case labels, enumerator values,
and generic arguments require evaluation.

```x
static u64 hash(const u8 *text) {
    u64 value = 0;
    for (uptr i = 0; text[i] != 0; ++i) {
        value = value * 31 + text[i];
    }
    return value;
}

global u64 greeting_hash = hash("hello");   // computed by cc

global u64 lookup(const u8 *name) {
    return hash(name) ^ $::eval(hash("salt"));
}
```

| Form | Effect |
| --- | --- |
| `$::eval(expr)` | Compute during compilation or fail. |
| `$::runtime(expr)` | Never compute during compilation; an error if a constant expression evaluates it. |
| `[[eval_only]]` | The function only runs during compilation and has no symbol. |
| `[[runtime_only]]` | The function never runs during compilation. |
| `$::static_assert(condition, "message");` | Compile-time assertion. |

Compile-time code uses the target's type sizes and arithmetic. It cannot
access static or volatile storage, call through pointers, execute machine
instructions, or read host files other than through `$::embed`. The
`-feval-*-limit` options bound its work, and `-fno-eval-calls` disables the
optional evaluation of ordinary calls.

## Generics

Type and value parameters follow the function name. Type arguments are
deduced from the call arguments; value arguments are always explicit.

```x
static T larger<T>(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

static T scaled<T, uptr N>(T value) {
    $::static_assert(N != 0, "N must be nonzero");
    return value * (T)N;
}

global u64 pick(u64 x) {
    return larger(x, 10u64) + scaled<u64, 4>(x);
}
```

Deduction requires every use of a type parameter to agree exactly: there is no
conversion, ranking, or default argument. `name::<u64>` is an unambiguous
alternative spelling of `name<u64>`. Each argument list produces one instance,
whose default link name is `name::<u64>`.

A record, union, or enumeration defined in a generic function's header belongs
to that function: each instance has its own type, and the tag and enumerators
are visible only in the header and the body. A redeclaration repeats each such
definition with the same tag, members or enumerators, types, and attributes,
in the same order, and then denotes the same types. Type parameters correspond
by position, so a redeclaration may rename them.

```x
struct Pair { T first; T second; } make_pair<T>(T a, T b);

global u64 pair_sum(u64 a, u64 b) {
    return make_pair(a, b).first + make_pair(a, b).second;
}

struct Pair { U first; U second; } make_pair<U>(U a, U b) {
    struct Pair result;
    result.first = a;
    result.second = b;
    return result;
}
```

## Metaprogramming

A procedural macro is a Cross function that runs during compilation and
returns tokens. It is invoked explicitly with `name!(...)`, `name![...]`, or
`name! { ... }`, and its result replaces the invocation:

```x
[[macro]]
static $::meta::tokens twice($::meta::tokens input) {
    return $::quote { (($::unquote(input)) * 2) };
}

global u32 doubled(u32 x) {
    return twice!(x + 1);
}
```

`syntax` declarations add new statement, expression, and item forms that are
active only where a `syntax` directive enables them. `$::embed("file")` reads a
file during compilation as bytes, which ordinary code can transform before
they initialize a static `u8` array. See
[metaprogramming.md](metaprogramming.md) for these facilities.

## Labels and computed goto

`label` values hold code addresses. `function::name` denotes a label in a
function (`function<u32>::name` in a generic instance), and
`goto expression;` jumps to a label value:

```x
global u32 parity(u32 n) {
    label targets[2] = { parity::even, parity::odd };
    goto targets[n & 1];
even:
    return 0;
odd:
    return 1;
}
```

Computed jumps must stay within one function. `global label name:` exports a
label for use as a symbol.

## ABIs and interoperability

Calls to functions defined without `global` in the same compilation may use a
convention the compiler chooses. Global functions, calls to functions defined
elsewhere, and calls through pointers use the compilation's ABI, which is the
target's Cross ABI unless a profile or `-mabi=` selects another. The Cross ABI
is not a C ABI. To call C or be called from C, select a platform ABI per
declaration or for the whole compilation:

```x
[[abi("sysv_abi"), link_name("write")]]
global i32 c::write(in i32 fd, in const void *buffer, in uptr count);

[[abi("sysv_abi")]]
global i32 add(i32 a, i32 b) {   // int add(int, int) in C
    return a + b;
}
```

Under a platform ABI, `in` parameters are passed by value and `out` and
`inout` parameters as pointers. `cc --print-abis` lists the ABIs of a target.

Parameters and results can also be pinned to registers, which is useful for
assembly interfaces (x86-64):

```x
global i32 transform(in u64 source "rdi", out u64 flags "rdx") -> "eax" {
    flags = source >> 1;
    return (i32)source;
}
```

`[[clobber("rcx", "flags", "memory")]]` declares additional effects, and
`[[stack_cleanup("callee")]]` makes the callee pop its stack arguments.

### Function pointers

A function pointer type includes the parameter modes and the ABI. A pointer
uses the compilation's ABI unless its type names another one, such as
`[[abi("ms_abi")]] typedef u32 (*operation)(u32 x);`. Like a function
declaration, the type can also pin parameters and the result to registers or
stack slots, declare clobbers, and select callee stack cleanup; calls through
the pointer follow that complete interface.

```x
typedef u32 (*operation)(u32 x);

static u32 increment(u32 x) {
    return x + 1;
}

global u32 apply(operation op, u32 x) {
    return op(x);   // (*op)(x) is equivalent
}

global u32 example() {
    return apply(increment, 41);   // &increment is equivalent
}
```

When a named function is converted to a pointer whose ABI, register or stack
locations, clobbers, or stack cleanup differ from the function's, `cc` emits a
small wrapper with the pointer's interface:

```x
typedef u64 (*hash_fn)(in u64 value);

static u64 mix(in u64 value "rdi") -> "rax" {
    return value * 31;
}

global hash_fn hasher() {
    return mix;   // a wrapper with the plain hash_fn interface
}
```

Parameter types, modes, and result must match exactly, and an existing
function-pointer value is never converted to another interface. A conditional
never selects a wrapper either: both of its function-pointer operands must
already have the same interface.

### Machine code

There is no inline assembly. Each target instruction is a typed built-in named
`$::_mnemonic`, and the compiler allocates, schedules, and optimizes it like
any other operation (x86-64):

```x
global u64 load_constant() {
    register u64 value "rax";
    $::_movabs(value, 0x123456789abcdef0u64);
    $::_add(value, 4);
    return value;
}
```

`cc --print-instructions` lists a target's instructions. `[[naked]]`
functions have no prologue or epilogue and must use instructions to return;
`[[raw_inline]]` functions can be inlined into them. `$::patch(value, site)`
creates a value that a loader can rewrite after linking.

### Other attributes

| Attribute | Effect |
| --- | --- |
| `section("name")` | Place a definition in a named section. |
| `used`, `retain` | Always emit; `retain` also keeps it through linker garbage collection. |
| `weak`, `alias("name")`, `weakref("name")` | Weak definitions, aliases, and weak references. |
| `visibility("hidden")` | Symbol visibility, where the object format has it. |
| `thread_local`, `tls_model("model")` | Thread-local storage. |
| `noinit` | Leave a static object uninitialized. |
| `aligned(N)` | On a function definition, its entry alignment. |

`cc --print-attributes` lists every attribute, and [targets.md](targets.md)
describes which object formats support each one.

## Program entry

`main` is an ordinary function. A program defines its own entry symbol, such
as a `global` function with `[[link_name("_start")]]`, and sets up whatever
stack and machine state it needs. It is linked with an ordinary linker
together with any libraries the user chooses.

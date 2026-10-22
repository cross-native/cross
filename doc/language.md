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
(ASCII only), and punctuation follow C. As in C23, two consecutive `[` tokens
open an attribute specifier where one may appear and are an error elsewhere,
so `a[b[i]]` closes two subscripts. Compiler-provided names live under
`$::`, such as `$::alignof` or `$::_add`; no other identifier contains `$`,
and no identifier is reserved for its leading underscores.

The keywords are:

```text
bool break case const continue default do else enum f32 f64 f80 f128 for fptr
global goto i8 i16 i32 i64 i128 if in inline inout iptr label namespace out
register restrict return sizeof stack static struct switch syntax typedef u8
u16 u32 u64 union u128 uptr using void volatile while
```

A keyword cannot name anything a program declares, and neither can `_`,
which discards an `out` argument. `int`, `char`, `long`, `float`, `double`,
`extern`, `auto`, `true`, and `false` are ordinary identifiers.

| Literal | Type |
| --- | --- |
| `42` | First of `i32`, `i64`, `i128` that fits. |
| `0xff`, `0b1010`, `017` | First of `i32`, `u32`, `i64`, `u64`, `i128`, `u128` that fits. |
| `4u32`, `0iptr`, `255u8` | The suffixed type; the value must fit. |
| `1.5`, `.5`, `1.5f32` | `f64`, or the suffixed `f32`, `f64`, `f80`, `f128`, or `fptr`. |
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
and 16-bit values promote to `i32`. A value read from a `const` or
`volatile` object has the unqualified type, so it is promoted and passed to a
variadic function like any other value. Signed overflow, division by zero, and
out-of-range shifts are undefined. Floating expressions round to their own
type; there is no excess precision. The selected profile declares which
floating exceptions trap and whether denormal results flush to zero
([models.md](models.md#profiles)); the compiler runs an operation that may
trap exactly where the source runs it, even when its result is unused, and
compile-time evaluation flushes as the profile says and does not evaluate an
operation that would trap.

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
raises the alignment of an object, record, or member, and sets the alignment
of a typedef, which may also lower it, as GCC's typedef `aligned` does. A
typedef's request applies to storage of its type: every object, member, array
element, and pointer target of the type has that alignment, and the type's
size rounds up to it. Values of the type convert, compare, and are passed and
returned like the base type, and function types and pointer conversions use
the base type. A dereference through a type whose request lowers its
alignment is unaligned-safe, like a packed member access; a raised request
does not make a dereference assume more alignment. Packing does not lower a
member below the alignment its type requests. A bit-field cannot have such a
type, and a vector's lanes carry no request, although a vector typedef can
request alignment for the whole vector.

```x
typedef u32 line_u32 [[aligned(64)]];   // sizeof and $::alignof are both 64

struct counters {
    line_u32 hits;                      // offset 0
    line_u32 misses;                    // offset 64
};

$::static_assert(sizeof(struct counters) == 128uptr, "one line per counter");
$::static_assert($::offsetof(struct counters, misses) == 64uptr, "second line");

typedef u32 u32_any [[aligned(1)]];     // a u32 at any address

global void put_word(in u8 *bytes, in u32 value) {
    *(u32_any *)(bytes + 1) = value;   // an unaligned store where required
}
```

`$::offsetof(type, member)` is a member's byte offset as a compile-time
`uptr`; the member may be followed by `.member` and `[index]` steps, as in
`$::offsetof(struct frame, lanes[2].low)`. It cannot name a bit-field.

`[[exhaustive]]` on a structure definition requires every brace initializer of
the structure to initialize each member, by position or designator, so adding
a member finds every initializer to update:

```x
struct version [[exhaustive]] {
    u16 major;
    u16 minor;
};

global struct version current = { .major = 1, .minor = 4 };
// { .major = 2 } is an error: member 'minor' is not named
```

A member with a record or union type and no name embeds that type: an
anonymous definition, as in C11, or a named type, designated by its tag or
typedef name. The members of an embedded record can be used as if they were
declared in the enclosing record, in accesses, initializer designators, and
`$::offsetof`, unless a member at a shallower depth has the same name; two
candidates at the same depth make that access an error. A pointer to the
record converts implicitly to a pointer to an embedded type at any depth, and
an explicit cast converts it back; a null pointer stays null. Embedding adds
no methods or dispatch, and the layout and ABI are those of ordinary members.
An embedded member counts as one member of an `[[exhaustive]]` structure.

```x
struct entity { f32 x; f32 y; };

struct enemy {
    u32 flags;
    struct entity;                // designated `entity`
    struct { u16 hp; u16 mp; };   // anonymous
};

static f32 sum(in const struct entity *e) { return e->x + e->y; }

global f32 update(struct enemy *foe) {
    foe->hp = 3;                                 // promoted from the anonymous member
    struct entity *self = foe;                   // points to foe->entity
    struct enemy *back = (struct enemy *)self;   // back to foe
    return sum(foe) + back->y + foe->entity.x;
}

global struct enemy boss = { .x = 1.0f32, .hp = 50, .flags = 1 };
$::static_assert($::offsetof(struct enemy, y) == 8, "promoted member offset");
```

`[[may_alias]]` exempts a type from the effective-type aliasing rules, which
otherwise follow C (any object may be accessed through `u8` or `i8`). Write it
after a typedef's name, after a record's tag in its definition
(`struct bits [[may_alias]] { ... }`), or as a qualifier, as in
`u32 [[may_alias]] *`. An explicit cast converts between unrelated pointer
types:

```x
typedef u32 any_u32 [[may_alias]];

u32 float_bits(in f32 value) {
    f32 copy = value;
    return *(any_u32 *)&copy;   // the representation of value
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

A declaration without `static`, `global`, or an initializer may omit an
array's first bound, as a header declares a table defined elsewhere. Such an
array can be indexed and decays to a pointer, but `sizeof` needs a definition
of the object in the same compilation to complete it; without one, the
declaration refers to an external symbol:

```x
u32 primes[];                          // usually from a header

global u32 primes[] = {2, 3, 5, 7};    // completes the type

global uptr prime_count() {
    return sizeof(primes) / sizeof(primes[0]);
}
```

Types have no linkage. Each input sees the types and enumerators it declares
or includes, and a record, union, or enumeration defined with the same tokens
in several inputs of one command, as by a shared header, is one type; defining
a tag differently in two inputs is an error.

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
    divide(value, 10, value, _);   // copy-out writes `value`; `_` discards
    while (value != 0) {
        divide(value, 10, value, _);
        count += 1;
    }
    return count;
}
```

Every parameter is a distinct copy, so modes never create aliases. Copy-out
happens left to right after the return value is computed. An `out` or `inout`
argument must be a modifiable lvalue; `_` discards an `out` result. The
compiler rejects a read of an `out` parameter before it is assigned and a
return that leaves it unassigned, and it follows `&&`, `||`, `!`, and `?:` as
C# does: after `if (!p || !parse(p, v)) return -1;`, `v` is assigned.

Other rules:

- `f()` and `f(void)` both declare a function with no parameters. There are no
  unprototyped functions, no implicit declarations, and no overloading.
- `inline` permits inlining; it never changes linkage. `[[always_inline]]`,
  `[[noinline]]`, `[[hot]]`, `[[cold]]`, and `[[noreturn]]` work as in GCC.
  A call to an `[[always_inline]]` function that cannot be inlined is an error
  that names the reason: a recursive or `[[musttail]]` call, or a callee that
  is variadic or `[[naked]]`, takes a label's address, contains `$::patch` or a
  `[[musttail]]` return, places a parameter or local at an explicit location,
  or has `out` or `inout` parameters and an explicit result location.
- `[[musttail]] return f(x);` requires a tail call or fails to compile. During
  compile-time evaluation it also replaces the caller's frame, so tail-call
  chains do not count toward the recursion limit.
- Variadic functions take `...` after a named parameter, but there is no
  `va_list`; a definition reads its arguments through
  `[[variadic(...)]]` bindings of the target ABI's state.
- A function may define an operator for a nominal record, union, or enum type
  with `[[operator("+")]]`. There are no other overloads and no user-defined
  conversions. An enumeration with an underlying type is a distinct integer
  type, so bindings give it its own arithmetic, such as saturation:

```x
enum sample [[underlying(i16)]] { sample_zero };

[[operator("+")]]
global enum sample add_samples(in enum sample a, in enum sample b) {
    i32 sum = (i32)a + (i32)b;
    if (sum > 32767) sum = 32767;
    if (sum < -32768) sum = -32768;
    return (enum sample)sum;
}

global enum sample mix(in enum sample a, in enum sample b) {
    return a + b;   // add_samples(a, b)
}

$::static_assert((i32)((enum sample)30000 + (enum sample)10000) == 32767,
                 "bindings evaluate at translation time too");
```

## Expressions and statements

Statements are C99's. The differences in expressions:

- Operands, arguments, and both sides of an assignment are evaluated left to
  right. Nothing is unsequenced.
- There is no comma operator. Commas separate declarators, arguments,
  initializers, and the expressions of a `for` loop's first and last clauses,
  which run left to right: `for (i = 0, j = n; i < j; ++i, --j)` is valid, but
  `i = 0, j = 1;` as a statement is not.
- Pointer comparisons with integer constant zero, differences of pointers
  that differ only in qualification, such as `const u8 *` minus `u8 *`,
  conversions through `void *`, casts, and `(void)expression` follow C: an
  explicit cast converts between any two object pointer types and may drop
  `const` or `volatile`, while an implicit conversion only adds qualifiers.
  Function pointers and object pointers, including `void *`, do not convert;
  see [Function pointers](#function-pointers) for their casts.
- A compound literal `(T){ ... }` is an unnamed object initialized like a
  declaration of `T`, as in C99; an omitted array bound comes from the
  initializer. In a function it lives until its enclosing block exits and is
  initialized anew each time it is evaluated; outside a function it is a static
  object. As with other brace initializers, `T` is an array, record, or union
  type.

```x
struct range { u32 low; u32 high; };

static u32 sum(in const u32 *values, in uptr count) {
    u32 total = 0;
    for (uptr i = 0; i < count; ++i) total += values[i];
    return total;
}

static u32 width(in const struct range *r) { return r->high - r->low; }

global const u32 *primes = (const u32[]){ 2, 3, 5, 7 };   // a static object

global u32 weigh(in u32 scale) {
    return sum((u32[]){ scale, scale * 2 }, 2) + width(&(struct range){ 1, 9 });
}
```

- `$::assume(condition)` and `$::unreachable()` state facts the optimizer may
  use; neither adds a check.
- `$::sqrt`, `$::fabs`, `$::copysign`, `$::fmin`, and `$::fmax` take operands
  of one floating type and return that type: the correctly rounded square
  root, the absolute value and sign transfer, which change only the sign bit,
  and IEEE minNum and maxNum, where a NaN operand yields the other operand and
  equal operands, such as `0.0` and `-0.0`, yield the first. They evaluate at
  translation time like operators; a target without an inline form reports
  an error, and `$::has_intrinsic` tells whether it has one.

```x
f64 distance(in f64 x, in f64 y) {
    return $::sqrt(x * x + y * y);
}

f32 clamp(in f32 value, in f32 low, in f32 high) {
    return $::fmin($::fmax(value, low), high);   // a NaN value yields low
}

$::static_assert($::sqrt(2.25) == 1.5, "evaluated at translation time");
$::static_assert($::copysign(3.0, -0.0) == -3.0, "the sign of -0.0");
```

- `$::clz`, `$::ctz`, and `$::popcount` count the leading zero bits, the
  trailing zero bits, and the one bits of an integer at the width of its type
  and return a `u32`; zero gives the width. `$::rotl(x, n)` and
  `$::rotr(x, n)` rotate `x` by `n` modulo its width, so a negative `n`
  rotates the other way, and `$::bswap(x)` reverses the bytes of `x`; these
  three return the type of `x`. They evaluate at translation time like
  operators and compile to an instruction where the CPU has one, otherwise to
  a short inline sequence, never to a call
  ([targets.md](targets.md#bit-intrinsics)).

```x
// The lowest free slot of a 64-slot pool, or 64 when the pool is full.
u32 first_free(in u64 used) {
    return $::ctz(~used);
}

u32 hash_step(in u32 hash, in u32 word) {
    return $::rotl(hash ^ word, 5) * 0x9e3779b9u32;
}

$::static_assert($::clz(1u16) == 15u32 && $::popcount(0xf0u8) == 4u32, "counts");
$::static_assert($::rotl(0x81u8, -1) == 0xc0u8, "a negative count rotates right");
$::static_assert($::bswap(0x1234u16) == 0x3412u16, "bytes in reverse order");
```

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

A record or union definition may also take a parameter list after its tag,
and every use names all of its arguments. Each argument list is one distinct
type, laid out and passed like a record written with those member types;
`struct buf<u8, 2 + 2>` and `struct buf<u8, 4>` are the same type, and
`struct list<u32>` and `struct list<i32>` are unrelated. A forward
declaration repeats the parameter list. A generic function can take an
instance, and deduction reads the arguments from it:

```x
struct list<T>;

struct list<T> {
    T *items;
    uptr count;
    struct list<T> *next;
};

struct buf<T, uptr N> { T data[N]; uptr used; };

static T first<T>(in struct list<T> *values) {
    return values->items[0];
}

global u32 head(in struct list<u32> *words, in struct buf<u8, 4> *bytes) {
    return first(words) + bytes->data[bytes->used - 1];
}
```

An instance is designated by its tag when embedded, and its descriptor in
mangling input appends the argument spellings, as in `struct list<u32>`.
Anonymous records and unions in a generic record's member list are copied for
each instance; named records, unions, and enumerations cannot be defined
there. Enumerations and anonymous definitions cannot be generic, and a generic
tag is declared before its first use.

## Metaprogramming

A procedural macro is a Cross function that runs during compilation and
returns tokens. It is invoked explicitly with `name!(...)`, `name![...]`, or
`name! { ... }`, and its result replaces the invocation; a keyword never names
a macro, so `return !(x)` is an ordinary negation:

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
label for use as a symbol. `(uptr)function::name` is the label's address, also
in a static initializer.

## ABIs and interoperability

Calls to functions defined without `global` in the same compilation may use a
convention the compiler chooses. Global functions, calls to functions defined
elsewhere, and calls through pointers use the compilation's ABI, which is the
target's Cross ABI unless a profile or `-mabi=` selects another; `x` names it
on every target. The Cross ABI is not a C ABI. To call C or be called from C,
select a platform ABI per declaration or for the whole compilation:

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
All declarations of a function select the same ABI; an alias such as `linux`
for `sysv_abi` names the same one.

Attributes followed by braces form an attribute region: they apply to each
declaration and definition inside for which they are valid, and an attribute
written on a declaration takes precedence. Regions nest and create no scope:

```x
[[abi("sysv_abi")]] {
    i32 c_close(in i32 fd);
    typedef i32 (*c_compare)(in const void *left, in const void *right);
    global i32 c_twice(in i32 value) { return value * 2; }
    global i32 c_win(in i32 value) [[abi("ms_abi")]] { return value; }
}
```

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
function-pointer value is never implicitly converted to another interface. A
conditional never selects a wrapper either: both of its function-pointer
operands must already have the same interface.

An explicit cast reinterprets a function pointer as any other function-pointer
type, with no wrapper; calling through it with an interface that differs from
the function's own is undefined. A cast to `uptr` gives the code address, also
in a static initializer, and a cast back from `uptr` restores the pointer:

```x
typedef void (*handler)();
typedef u32 (*counter)(in u32 x);

static u32 next(in u32 x) {
    return x + 1;
}

global handler table[1] = { (handler)next };   // a type-erased entry
global uptr entry_point = (uptr)next;         // the code address

global u32 call_entry(in u32 x) {
    counter typed = (counter)table[0];        // recover the original type
    return typed(x) + ((counter)entry_point)(x);
}
```

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
| `address(N)` | Place a declared object or function at the fixed address `N`; no storage or symbol is emitted. |

A memory-mapped device is an object at a fixed address (the register layout
here is illustrative):

```x
struct uart {
    volatile u32 data;
    volatile u32 status;
};

struct uart console [[address(0x10000000)]];

global void put(in u8 byte) {
    while ((console.status & 1) == 0) {}
    console.data = byte;
}
```

`cc --print-attributes` lists every attribute, and [targets.md](targets.md)
describes which object formats support each one.

## Program entry

`main` is an ordinary function. A program defines its own entry symbol, such
as a `global` function with `[[link_name("_start")]]`, and sets up whatever
stack and machine state it needs. It is linked with an ordinary linker
together with any libraries the user chooses.

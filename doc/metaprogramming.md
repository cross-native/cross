# Metaprogramming

Cross code can run during compilation to generate code: procedural macros
rewrite an explicit invocation, syntax extensions add new statement,
expression, and item forms, and `$::embed` brings a file's bytes into the
compilation. Call checkers inspect the calls of a function instead of
generating code. All of them run ordinary Cross functions in a sandbox: they
see only their inputs, use the target's type sizes, and cannot touch the host
except through `$::embed`. Generated output is ordinary Cross, checked like
written code.

The `-feval-step-limit`, `-feval-depth-limit`, `-feval-memory-limit`, and
`-feval-byte-limit` options bound this work. Errors in generated code report
the expansion chain back to the invocation.

## Procedural macros

A macro is a `static` function marked `[[macro]]` that takes and returns
`$::meta::tokens`. It is invoked as `name!(...)`, `name![...]`, or
`name! { ... }`; the input excludes the delimiters, and the returned tokens
replace the invocation.

```x
[[macro]]
static $::meta::tokens swap($::meta::tokens input) {
    $::meta::tokens left = $::meta::at(input, 0);    // input is `a , b`
    $::meta::tokens right = $::meta::at(input, 2);
    $::meta::tokens temp = $::meta::gensym("temp");
    return $::quote {
        u32 $::unquote(temp) = $::unquote(left);
        $::unquote(left) = $::unquote(right);
        $::unquote(right) = $::unquote(temp);
    };
}

global u32 ordered(u32 a, u32 b) {
    if (a > b) {
        swap!(a, b);
    }
    return a * 1000 + b;
}
```

`$::quote { ... }` builds tokens from written source, and `$::unquote(x)`
inserts a token or syntax value inside a quote. The result is spliced as
tokens, without implicit parentheses or scope: if `identity!` returns its
input, `identity!(1 + 2) * 3` means `1 + 2 * 3`. Output may be empty, contain
several statements or declarations, or contain further macro invocations.

Names are hygienic:

- Identifiers written in a quote resolve where the macro is defined. That
  includes typedefs and `struct`, `union`, and `enum` tags declared in the
  macro's own blocks: the output denotes the macro's type wherever it is
  placed, and a type with the same name at the invocation does not capture
  it. Enumerators declared in those blocks are not visible to the output.
- Tokens copied from the input keep the caller's meaning.
- `$::meta::gensym("prefix")` makes an identifier that cannot collide with any
  other name. Any string works as the prefix; characters that cannot appear in
  an identifier become `_`.
- `$::meta::call_site(identifier)` makes an identifier resolve at the
  invocation instead.

```x
[[macro]]
static $::meta::tokens declare_point($::meta::tokens input) {
    struct Point { i32 x; i32 y; };
    return $::quote { struct Point $::unquote(input); };
}

struct Point { u8 unrelated; };

global i32 point_sum(i32 x, i32 y) {
    declare_point!(p);    // the macro's struct Point, not the one above
    p.x = x;
    p.y = y;
    return p.x + p.y;
}
```

Macros can call other `static` functions; a quote inside such a helper
resolves where the helper is defined, by the same rules. In a generic helper,
the helper's typedefs and tags denote those of the instance that runs.
Identifiers made by `$::meta::parse` or `$::meta::token` resolve like
identifiers written in a quote at that call. A function whose signature uses a
`$::meta` type runs only during compilation and has no symbol, so attributes
that need one, such as `naked`, `abi`, or `section`, are errors on it.

### Token operations

All of these live under `$::meta::`. `one` is a token sequence holding one
token or one balanced group.

| Operation | Result |
| --- | --- |
| `len(tokens)`, `at(tokens, i)`, `slice(tokens, i, n)`, `concat(a, b)` | Count, select, and join top-level tokens (a group counts as one). |
| `is_kind(one, kind)` | Test the kind: `"identifier"`, `"builtin"`, `"integer"`, `"floating"`, `"character"`, `"string"`, `"punctuation"`, `"group"`, or `"splice"`. |
| `spelling(one)` | The token's exact source text, as bytes. |
| `children(one)`, `delimiter(one)` | A group's contents, and its delimiters: `"()"`, `"[]"`, `"{}"`, or `"[[]]"`. |
| `token(kind, text)`, `group(delimiters, tokens)` | Construct a token or a group. |
| `parse(string)` | Tokens from source text. |
| `span(one)` | The token's source position. |
| `error(span, message)`, `warning(...)`, `note(...)` | Report a diagnostic at a position; an error stops the expansion. The message is a string or bytes. |

### Type reflection

A `$::meta::type` value stands for a type during compilation. Like
`$::meta::tokens`, the name is a type in a declaration and an operation
before `(`. `$::meta::type(tokens)` gives the type that tokens name: tokens
taken from the macro's input are resolved at the invocation, and tokens
written in a quote where the quote is. A record is complete for a query if it
is complete where the expansion runs, so a macro invoked after a definition,
including a block-scope one, can inspect it.

| Operation | Result |
| --- | --- |
| `type(tokens)` | The type the tokens name. |
| `same_type(a, b)` | Whether two types are the same; a typedef denotes the type it aliases. |
| `is_type_kind(t, kind)` | Test the kind: `"void"`, `"bool"`, `"integer"`, `"floating"`, `"pointer"`, `"array"`, `"struct"`, `"union"`, `"enum"`, `"vector"`, `"function"`, or `"label"`. |
| `size(t)`, `align(t)` | `sizeof` and `$::alignof` of a complete type. |
| `bits(t)`, `is_signed(t)` | The width and signedness of a scalar or enumeration type. |
| `member_count(t)`, `member_name(t, i)`, `member_type(t, i)`, `member_offset(t, i)` | The members of a complete record or union in declaration order, including embedded members and unnamed bit-fields; an unnamed member's name is empty. |
| `member_bit_offset(t, i)`, `member_bit_width(t, i)` | A bit-field's lowest bit in the unit at its offset, read as its declared type in the target's byte order, and its width; zero width for other members. |
| `element(t)`, `count(t)` | Array element and length, vector lane and lane count, pointer pointee and 1, or enumeration underlying type and 0. |
| `enumerator_count(t)`, `enumerator_name(t, i)` | The enumerators of an enumeration; a name is resolved at the invocation. |
| `type_tokens(t)` | Tokens that denote the type wherever they are spliced, even where its name is hidden. |
| `type_spelling(t)` | The type's spelling for messages, such as `const u8 *`, as bytes. |

Counts and offsets are `uptr` and widths `u32`. A query on an incomplete type,
an index out of range, or a type of the wrong kind is an error.

```x
struct header { u8 version; u16 flags; u32 length; };

[[macro]]
static $::meta::tokens member_sum($::meta::tokens input) {
    $::meta::type t = $::meta::type(input);
    $::meta::tokens sum = $::meta::parse("0u64");
    for (uptr i = 0; i < $::meta::member_count(t); ++i) {
        if (!$::meta::is_type_kind($::meta::member_type(t, i), "integer")) continue;
        $::meta::tokens name = $::meta::member_name(t, i);
        sum = $::quote { $::unquote(sum) + (u64)p->$::unquote(name) };
    }
    return $::quote {
        static u64 checksum(in const $::unquote($::meta::type_tokens(t)) *p) {
            return $::unquote(sum);
        }
    };
}

member_sum! { struct header }

global u64 header_sum(in const struct header *h) { return checksum(h); }
```

## Syntax extensions

A syntax extension declares a prefix keyword, a pattern, and an expander
function. It is inactive until a `syntax` directive activates it, and then it
applies until the end of the enclosing block, namespace body, or file.

```x
namespace flow {
    [[syntax_expander]]
    static $::meta::tokens expand_unless($::meta::syntax_match input) {
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

global u32 clamp(u32 value) {
    syntax flow::unless;
    unless (value < 100) {
        value = 100;
    }
    return value;
}
```

A parsed expression spliced with `$::unquote` stays grouped, so
`!$::unquote(condition)` negates the whole condition.

| Kind | Matches at | Expands to |
| --- | --- | --- |
| `statement` | Statement position | Exactly one statement. |
| `expression` | A primary expression | One expression. |
| `item` | File or namespace scope | Zero or more declarations. |
| `rule` | Not invoked directly | A reusable pattern, used as `rule(Name)`. |
| `bundle` | Not invoked directly | A list of `use Name;` activations. |

Activation forms:

```text
syntax flow::unless;                    // until the end of this scope
syntax flow::unless as unless_not;      // under another prefix
syntax (flow::unless) { ... }           // for the declarations in the braces
```

Two active extensions cannot claim the same prefix. Recognition never runs
user code. An invocation that matches its pattern in two different ways is an
error, and an expansion replaces only the matched source.

### Patterns

A pattern is a sequence of quoted tokens, which must appear literally, and
captures named `name:kind`:

| Capture kind | Matches |
| --- | --- |
| `ident`, `name`, `literal` | An identifier, a qualified name, or a literal. |
| `expr`, `type`, `stmt`, `declaration` | One parsed expression, type, statement, or declaration. |
| `function_header`, `function_decl`, `function_def` | A function header, prototype, or definition. |
| `paren`, `bracket`, `block`, `group` | One balanced `( )`, `[ ]`, `{ }`, or any group, unparsed. |
| `tokens_until(";")` | Unparsed tokens up to a `;`, which must follow in the pattern. |
| `function`, `function_raw` | A function header followed by an unparsed body. |
| `optional(p)`, `repeat0(p)`, `repeat1(p)` | Zero or one, zero or more, or one or more of `p`. |
| `separated0(p, ",")`, `separated1(p, ",")` | `p` repeated with a separator token. |
| `choice(a:(p) \| b:(q))` | One of several tagged alternatives. |
| `rule(Name)` | A `rule` declared elsewhere. |

Repeated and alternative captures produce nested records:

```x
[[syntax_expander]]
static $::meta::tokens expand_all($::meta::syntax_match input) {
    $::meta::tokens test = $::quote { 1 == 1 };
    uptr count = $::syntax::count(input, "conditions");
    for (uptr i = 0; i < count; ++i) {
        $::meta::syntax_match entry = $::syntax::at(input, "conditions", i);
        $::meta::syntax condition = $::syntax::node(entry, "condition");
        test = $::quote { $::unquote(test) && $::unquote(condition) };
    }
    $::meta::syntax body = $::syntax::node(input, "body");
    return $::quote {
        if ($::unquote(test))
            $::unquote(body)
    };
}

syntax all : statement {
    prefix "all";
    match "(" conditions:separated1(condition:expr, ",") ")" body:stmt;
    expand expand_all;
}

global u32 in_box(i32 x, i32 y) {
    syntax all;
    all (x >= 0, x < 640, y >= 0, y < 480) {
        return 1;
    }
    return 0;
}
```

### Match and syntax operations

| Operation | Result |
| --- | --- |
| `$::syntax::input(match)` | All matched tokens, including the prefix. |
| `$::syntax::capture(match, "field")` | A capture's tokens. |
| `$::syntax::node(match, "field")` | A capture as a syntax tree (`$::meta::syntax`). |
| `$::syntax::count(match, "field")`, `$::syntax::at(match, "field", i)` | Nested records of a repeated capture. |
| `$::syntax::is_variant(record, "tag")` | Which `choice` alternative matched. |
| `$::syntax::span(match)`, `$::syntax::capture_span(match, "field")` | Source positions. |
| `$::syntax::context(match)` | The invocation's name-lookup context. |
| `$::syntax::error(span, message)`, `warning(...)`, `note(...)` | Diagnostics at a position. |

Syntax trees follow the published grammar. `$::meta::child_count(node)`,
`child(node, i)`, `is_kind(node, kind)`, and `is_production(node, name)`
inspect a tree, `replace_child(node, i, replacement)` returns an edited copy,
`tokens(node)` flattens it to tokens, and `parse(category, tokens, context)`
parses tokens as `"expr"`, `"stmt"`, `"type"`, `"declaration"`,
`"function_header"`, `"function_decl"`, or `"function_def"`. Nested
invocations inside a capture stay unexpanded until the enclosing expansion
finishes.

`$::has_feature($::feature::syntax_extensions)` reports the facility;
`$::has_intrinsic` tests individual operations.

### Example: resumable functions

A syntax extension can rewrite a whole function. Here `resumable(state)`
before a definition makes the function a script that runs one step per call,
as a protothread does: `state` designates a `label` object, `yield;` stores in
it the position after the `yield` and returns, and the next call resumes
there. The entry dispatches on `state` with a computed `goto` and clears it,
so a call that returns normally leaves it null and the next call starts over.

```x
namespace script {
    // Whether `one` is the token spelled `text`.
    static bool spelled($::meta::tokens one, const u8 *text) {
        $::meta::bytes spelling = $::meta::spelling(one);
        uptr i = 0;
        for (; i < $::meta::len(spelling); ++i)
            if (text[i] != $::meta::at(spelling, i)) return 0;
        return text[i] == 0;
    }

    // The name that a function header declares.
    static $::meta::syntax declared_name($::meta::syntax node) {
        for (uptr i = 0; i < $::meta::child_count(node); ++i) {
            $::meta::syntax child = $::meta::child(node, i);
            if ($::meta::is_production(child, "qualified_name")) return child;
            if ($::meta::is_production(child, "declarator") ||
                $::meta::is_production(child, "direct_declarator"))
                return declared_name(child);
        }
        return node;
    }

    // `body` with each `yield;` replaced by: store the label after it in
    // `state`, return, and resume there. The label marks the `;` of the
    // `yield`, so diagnostics about the resume point show the `yield`.
    static $::meta::tokens rewrite($::meta::tokens body, $::meta::tokens state,
                                   $::meta::tokens name) {
        $::meta::tokens text = $::quote {};
        uptr count = $::meta::len(body);
        for (uptr i = 0; i < count; ++i) {
            $::meta::tokens one = $::meta::at(body, i);
            if ($::meta::is_kind(one, "group")) {
                one = $::meta::group($::meta::delimiter(one),
                                     rewrite($::meta::children(one), state, name));
            } else if (spelled(one, "yield") && i + 1 < count &&
                       spelled($::meta::at(body, i + 1), ";")) {
                $::meta::tokens resume = $::meta::gensym("resume");
                $::meta::tokens end = $::meta::at(body, ++i);
                one = $::quote {
                    {
                        $::unquote(state) = $::unquote(name)::$::unquote(resume);
                        return;
                        $::unquote(resume): $::unquote(end)
                    }
                };
            }
            text = $::meta::concat(text, one);
        }
        return text;
    }

    [[syntax_expander]]
    static $::meta::tokens expand_resumable($::meta::syntax_match input) {
        $::meta::syntax header = $::syntax::node(input, "header");
        $::meta::tokens state = $::meta::children($::syntax::capture(input, "state"));
        $::meta::tokens body = rewrite($::meta::children($::syntax::capture(input, "body")),
                                       state, $::meta::tokens(declared_name(header)));
        $::meta::tokens resume = $::meta::gensym("resume");
        // Resume where the last yield left off; start over on the next call
        // unless another yield stores a label.
        return $::quote {
            $::unquote(header) {
                label $::unquote(resume) = $::unquote(state);
                $::unquote(state) = (label)0;
                if ($::unquote(resume)) goto $::unquote(resume);
                $::unquote(body)
            }
        };
    }

    syntax resumable : item {
        prefix "resumable";
        match state:paren header:function_header body:block;
        expand expand_resumable;
    }
}

struct walker {
    i32 x;
    label resume;   // null: start from the beginning
    u32 steps;      // kept across each yield
};

syntax script::resumable;

// One step per call: right three times, then back to the start.
resumable(self->resume) void walk(struct walker *self) {
    for (self->steps = 0; self->steps < 3; ++self->steps) {
        self->x += 1;
        yield;
    }
    self->x = 0;
}
```

The pattern captures `state` raw, as a `paren` group: like the body, it is
parsed where the output places it, so `self` binds to the parameter of the
spliced header. The expander builds its output as one token sequence; a part
parsed separately with `$::meta::parse` would be a fragment of its own.

Locals do not survive a `yield`, since each call starts with fresh ones, so a
script keeps what it needs across one in its state record, as `walk` keeps
`steps` in `walker`. A local that is read after a resume before it is
assigned again is reported by `-Wuninitialized` or `-Wmaybe-uninitialized`,
which `-Wall` enables ([invoke.md](invoke.md#warnings)), with notes at the
dispatch and at the `yield` that the read follows. Declaring `u32 steps` in
the `for` statement instead reports `'steps' is used uninitialized` at
`++steps`.

## Call checkers

`[[check(name)]]` on a function declaration runs the call checker `name` at
every direct call of the function, including calls in file-scope initializers
and assertions, before translation-time evaluation folds it. A checker is a
`static` function marked `[[checker]]` that takes one `$::meta::call`; it
reads the call's arguments and reports problems with `$::meta::warning`,
`$::meta::note`, and `$::meta::error`. `[[checker("format")]]` ties the
checker to a warning ([invoke.md](invoke.md#warnings)): it runs only while
`-Wformat` is on, and its warnings are `-Wformat` warnings, which `-Werror`
and `-Werror=format` turn into errors. A checker without a warning always
runs.

```x
// The key of a lookup must be a literal without spaces.
[[checker("format")]]
static void key_checker(in $::meta::call call) {
    $::meta::bytes key = $::meta::argument_string(call, 0);
    if ($::meta::len(key) == 0) {
        $::meta::warning($::meta::argument_span(call, 0), "the key is not a string literal");
        return;
    }
    for (uptr i = 0; i < $::meta::len(key); ++i)
        if ($::meta::at(key, i) == ' ')
            $::meta::error($::meta::argument_span(call, 0), "a key cannot contain a space");
}

[[check(key_checker)]] u32 lookup(in const u8 *key);

global u32 sword() { return lookup("sword"); }
```

| Operation | Result |
| --- | --- |
| `argument_count(call)`, `argument_type(call, i)`, `argument_span(call, i)` | The arguments, the type of each value (before parameter conversion or variadic promotion), and their positions. |
| `argument_string(call, i)` | A string-literal argument's bytes with its terminating zero; empty for any other argument. |
| `parameter_count(call)`, `parameter_type(call, i)`, `is_variadic(call)` | The called function's named parameters, and whether it takes more. |
| `call_span(call)` | The call's position. |
| `abi_type(call, name)` | The type that the called function's ABI model names `name`, such as `long` in a C ABI, or `void`. |

The checkers `$::format::printf` and `$::format::scanf` are built into the
compiler and written in Cross the same way; they check a call's format
string against its arguments, as GCC's `-Wformat` does, using the C types of
the called function's ABI ([language.md](language.md#abis-and-interoperability)).

## Embedded files

`$::embed("path")` reads a file during compilation as `$::meta::bytes`. The
path is searched like a quoted include, and the file is a dependency in `-M`
output. Ordinary functions can transform the bytes before they initialize a
static `u8` array:

```x
static $::meta::bytes upper($::meta::bytes text) {
    uptr size = $::meta::len(text);
    $::meta::buffer result = $::meta::alloc(size);
    u8 *bytes = $::meta::data(result);
    for (uptr i = 0; i < size; ++i) {
        u8 c = $::meta::at(text, i);
        if (c >= 'a' && c <= 'z') {
            c = c - 32;
        }
        bytes[i] = c;
    }
    return $::meta::freeze(result, size);
}

global const u8 banner[] = upper($::embed("banner.txt"));
```

| Operation | Result |
| --- | --- |
| `len(bytes)`, `at(bytes, i)`, `slice(bytes, i, n)`, `concat(a, b)` | Inspect and combine byte sequences. |
| `data(bytes)` | A read-only `const u8 *` to the bytes. |
| `alloc(n)`, `data(buffer)`, `cap(buffer)` | A writable buffer of `n` bytes. |
| `freeze(buffer, n)` | The first `n` bytes of a buffer as `$::meta::bytes`; the buffer is consumed. |

The array's size is the final byte count; no terminator is added. Bytes and
buffers exist only during compilation and cannot be stored at runtime.

A macro can turn an embedded table into declarations. Here `items.tsv` holds
one `NAME<TAB>VALUE` row per line, and a row without a value continues the
numbering; the file is a dependency of the unit although the embed is written
in the macro's body:

```x
static $::meta::bytes quoted($::meta::bytes name) {
    uptr size = $::meta::len(name);
    $::meta::buffer result = $::meta::alloc(size + 2);
    u8 *bytes = $::meta::data(result);
    bytes[0] = '"';
    for (uptr i = 0; i < size; ++i) {
        bytes[i + 1] = $::meta::at(name, i);
    }
    bytes[size + 1] = '"';
    return $::meta::freeze(result, size + 2);
}

// Each row becomes `NAME = VALUE,` or `NAME,`; with `names` set, `"NAME",`.
static $::meta::tokens tsv_rows($::meta::bytes text, u32 names) {
    $::meta::tokens rows = $::meta::parse("");
    uptr size = $::meta::len(text);
    for (uptr start = 0; start < size; ) {
        uptr end = start;
        while (end < size && $::meta::at(text, end) != '\n') ++end;
        uptr stop = end;
        if (stop > start && $::meta::at(text, stop - 1) == '\r') --stop;
        uptr tab = start;
        while (tab < stop && $::meta::at(text, tab) != '\t') ++tab;
        if (tab > start) {
            $::meta::bytes name = $::meta::slice(text, start, tab - start);
            if (names) {
                $::meta::tokens literal = $::meta::token("string", quoted(name));
                rows = $::meta::concat(rows, $::quote { $::unquote(literal), });
            } else if (tab < stop) {
                $::meta::tokens enumerator = $::meta::token("identifier", name);
                $::meta::tokens value =
                    $::meta::token("integer", $::meta::slice(text, tab + 1, stop - tab - 1));
                rows = $::meta::concat(rows, $::quote { $::unquote(enumerator) = $::unquote(value), });
            } else {
                $::meta::tokens enumerator = $::meta::token("identifier", name);
                rows = $::meta::concat(rows, $::quote { $::unquote(enumerator), });
            }
        }
        start = end + 1;
    }
    return rows;
}

[[macro]]
static $::meta::tokens item_table($::meta::tokens tag) {
    $::meta::bytes text = $::embed("items.tsv");
    $::meta::tokens enumerators = tsv_rows(text, 0);
    $::meta::tokens names = tsv_rows(text, 1);
    return $::quote {
        enum $::unquote(tag) [[underlying(u16)]] { $::unquote(enumerators) };
        global const u8 *const item_names[] = { $::unquote(names) };
    };
}

item_table!(item)
```

With the rows `ITEM_SWORD 1`, `ITEM_POTION 17`, and `ITEM_SHIELD`, the
invocation expands to `enum item [[underlying(u16)]] { ITEM_SWORD = 1,
ITEM_POTION = 17, ITEM_SHIELD, };` and a three-entry `item_names`, so
`$::static_assert(ITEM_SHIELD == 18, "")` holds. The macro runs in every unit
that invokes it.

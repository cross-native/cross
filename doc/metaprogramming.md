# Metaprogramming

Cross code can run during compilation to generate code: procedural macros
rewrite an explicit invocation, syntax extensions add new statement,
expression, and item forms, and `$::embed` brings a file's bytes into the
compilation. All three run ordinary Cross functions in a sandbox: they see
only their inputs, use the target's type sizes, and cannot touch the host
except through `$::embed`. Their output is ordinary Cross, checked like
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
resolves where the helper is defined, by the same rules. A function whose
signature uses a `$::meta` type runs only during compilation and has no symbol.

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
| `error(span, message)`, `warning(...)`, `note(...)` | Report a diagnostic at a position; an error stops the expansion. |

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

`$::has_feature($::feature::syntax_extensions)` is still false because the
facility is incomplete. Test individual operations with `$::has_intrinsic`.

## Embedded files

`$::embed("path")` reads a file during compilation as `$::meta::bytes`. The
path is searched like a quoted include, and the file is a dependency in `-M`
output. Ordinary functions can transform the bytes before they initialize a
static `u8` array:

```x
static $::meta::bytes upper($::meta::bytes text) {
    uptr size = $::meta::len(text);
    $::meta::buffer result = $::meta::alloc(size);
    u8 *out = $::meta::data(result);
    for (uptr i = 0; i < size; ++i) {
        u8 c = $::meta::at(text, i);
        if (c >= 'a' && c <= 'z') {
            c = c - 32;
        }
        out[i] = c;
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

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODE MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
elseif(MODE MATCHES "^mips")
    list(APPEND flags -target "${MODE}-unknown-linux-gnu")
endif()
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags} ${ARGN}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: expected success\n${out}\n${err}")
            endif()
            if(name STREQUAL diagnostics AND
               (NOT err MATCHES "warning: token warning" OR NOT err MATCHES "note: token note"))
                message(FATAL_ERROR "${name}/${level}: missing diagnostics\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

# Narrow negative integers must not become their unsigned bit-pattern indices.
# The large captures make the bits of (i8)-1i32 (255) and (i8)-128i32 (128)
# valid positive indices, so a host-sized bounds comparison cannot pass by luck.
string(REPEAT "leaf " 300 index_words)
string(REPEAT "leaf " 129 record_words)
foreach(index "(i8)-1i32" "(i8)-128i32" "(i16)-1i32" "-1i32" "-1i64" "-1i128")
    string(MAKE_C_IDENTIFIER "${index}" index_case)
    foreach(operation child replace_child)
        if(operation STREQUAL child)
            set(call "$::meta::child(node, ${index})")
            set(expected "child index is out of range")
        else()
            set(call "$::meta::replace_child(node, ${index}, $::meta::child(node, 1uptr))")
            set(expected "child index is out of range")
        endif()
        check(negative_${operation}_${index_case} "${expected}" "
            [[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
                $::meta::syntax node = $::syntax::node(input, \"body\");
                $::meta::syntax invalid = ${call};
                return $::quote { 1u32 };
            }
            syntax Inspect : expression { prefix \"inspect\"; match body:block; expand inspect; }
            syntax Inspect;
            global u32 entry() { return inspect { ${index_words} }; }")
    endforeach()
    check(negative_record_${index_case} "record index is out of range" "
        [[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
            $::meta::syntax_match invalid = $::syntax::at(input, \"items\", ${index});
            return $::quote { 1u32 };
        }
        syntax Inspect : expression { prefix \"inspect\"; match \"(\" items:repeat1(\"leaf\") \")\"; expand inspect; }
        syntax Inspect;
        global u32 entry() { return inspect (${record_words}); }")
endforeach()
check(large_index_controls pass "
    [[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
        $::meta::syntax node = $::syntax::node(input, \"body\");
        $::static_assert($::meta::child_count(node) == 302uptr, \"all raw children\");
        $::meta::syntax selected = $::meta::child(node, 255u8);
        $::static_assert($::meta::is_kind(selected, \"token\"), \"positive narrow index\");
        $::static_assert($::meta::is_kind($::meta::child(node, 127i8), \"token\"), \"positive signed index\");
        $::meta::syntax changed = $::meta::replace_child(node, 255u8, selected);
        $::static_assert($::meta::child_count(changed) == 302uptr, \"replacement preserves count\");
        $::static_assert($::syntax::count(input, \"items\") == 129uptr, \"all child records\");
        $::meta::syntax_match items = $::syntax::at(input, \"items\", 128u8);
        $::static_assert(sizeof($::meta::child_count($::meta::child(node, (i8)-1i32))) == sizeof(uptr),
            \"negative bounds are checked only on execution\");
        $::static_assert(sizeof($::meta::child_count($::meta::replace_child(node, (i8)-1i32, selected))) == sizeof(uptr),
            \"unevaluated replacement\");
        $::static_assert(sizeof($::syntax::count($::syntax::at(input, \"items\", (i8)-1i32), \"absent\")) == sizeof(uptr),
            \"unevaluated record lookup\");
        return $::quote { 1u32 };
    }
    syntax Inspect : expression { prefix \"inspect\"; match body:block \"(\" items:repeat1(\"leaf\") \")\"; expand inspect; }
    syntax Inspect;
    global u32 entry() { return inspect { ${index_words} } (${record_words}); }")

foreach(rebuild fresh parse construct)
    set(reference "$::meta::gensym(\"part\")")
    if(rebuild STREQUAL parse)
        set(reference "$::meta::parse($::meta::spelling(member))")
    elseif(rebuild STREQUAL construct)
        set(reference "$::meta::token(\"identifier\", $::meta::spelling(member))")
    endif()
    check(private_member_${rebuild} "no member" "
        [[macro]] static $::meta::tokens define(in $::meta::tokens input) {
            $::meta::tokens member = $::meta::gensym(\"part\");
            $::meta::tokens reference = ${reference};
            return $::quote {
                struct Record { u32 $::unquote(member); };
                global u32 entry() {
                    struct Record item = { 7u32 };
                    return item.$::unquote(reference);
                }
            };
        }
        define!()")
endforeach()

check(inspection pass [=[
#if !$::has_intrinsic($::meta::spelling) || !$::has_intrinsic($::meta::children) || !$::has_intrinsic($::meta::delimiter) || !$::has_intrinsic($::meta::span) || !$::has_intrinsic($::meta::error) || !$::has_intrinsic($::meta::warning) || !$::has_intrinsic($::meta::note) || !$::has_intrinsic($::meta::token) || !$::has_intrinsic($::meta::group)
#error missing implemented token API
#endif
#if $::has_feature($::feature::syntax_extensions)
#error incomplete syntax feature was advertised
#endif
[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) {
    $::static_assert($::meta::is_kind($::quote { namespace }, "identifier"), "lexical keyword");
    $::static_assert($::meta::is_kind($::quote { $::alignof }, "builtin"), "builtin");
    $::static_assert($::meta::is_kind($::quote { 17u32 }, "integer"), "integer");
    $::static_assert($::meta::is_kind($::quote { 1.5f64 }, "floating"), "floating");
    $::static_assert($::meta::is_kind($::quote { 'a' }, "character"), "character");
    $::static_assert($::meta::is_kind($::quote { "\x41" }, "string"), "string");
    $::static_assert($::meta::is_kind($::quote { + }, "punctuation"), "punctuation");
    $::static_assert($::meta::is_kind($::quote { () }, "group"), "group");
    $::static_assert(!$::meta::is_kind($::quote { () }, "punctuation"), "group is not a leaf");
    $::meta::bytes literal = $::meta::spelling($::quote { "\x41" });
    $::static_assert($::meta::len(literal) == 6uptr && $::meta::at(literal, 1uptr) == 92u8,
                    "spelling is not decoded or terminated");
    $::static_assert($::meta::len($::meta::children($::quote { () })) == 0uptr, "empty group");
    $::meta::tokens nested = $::meta::children($::quote { (first [second] [[third]] {fourth}) });
    $::static_assert($::meta::len(nested) == 4uptr, "direct children");
    $::static_assert($::meta::delimiter($::meta::at(nested, 1uptr))[0uptr] == '[' &&
                    $::meta::delimiter($::meta::at(nested, 2uptr))[2uptr] == ']' &&
                    $::meta::delimiter($::meta::at(nested, 3uptr))[0uptr] == '{' &&
                    $::meta::delimiter($::quote { () })[1uptr] == ')', "four delimiters");
    $::meta::bytes number = $::meta::slice($::meta::spelling($::quote { xx17u32yy }), 2uptr, 5uptr);
    $::static_assert($::meta::is_kind($::meta::parse(number), "integer"), "exact byte slice parse");
    $::static_assert($::meta::len($::meta::parse($::meta::slice(number, 0uptr, 0uptr))) == 0uptr,
                    "empty byte view");
    $::static_assert($::meta::is_kind($::meta::parse($::meta::spelling($::quote { "é😀" })), "string"),
                    "valid multi-byte UTF-8 is preserved");
    return $::meta::parse(number);
}
$::static_assert(inspect!{} == 17u32, "inspection output");
global u32 entry() { return inspect!{}; }
]=])

check(contexts pass [=[
namespace Library {
    static u32 selected() { return 17u32; }
    [[macro]] static $::meta::tokens rebuild(in $::meta::tokens input) {
        return $::meta::parse($::meta::spelling(input));
    }
    [[macro]] static $::meta::tokens copied(in $::meta::tokens input) {
        return $::meta::children(input);
    }
    [[macro]] static $::meta::tokens rebuilt_fresh(in $::meta::tokens input) {
        return $::meta::parse($::meta::spelling($::meta::gensym("selected")));
    }
}
static u32 selected() { return 29u32; }
$::static_assert(Library::rebuild!{ selected }() == 17u32, "reconstruction uses definition context");
$::static_assert(Library::copied!{ (selected) }() == 29u32, "children preserve input context");
$::static_assert(Library::rebuilt_fresh!{}() == 17u32, "spelling does not recover a private identity");
global u32 entry() { return 1u32; }
]=])
check(splice pass [=[
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "value");
    $::static_assert($::meta::is_kind(node, "core"), "node overload remains distinct");
    $::meta::tokens wrapped = $::quote { ($::unquote(node)) };
    $::meta::tokens copied = $::meta::children(wrapped);
    $::meta::tokens(node);
    ($::meta::tokens(node));
    $::meta::span(copied);
    ($::meta::span(copied));
    $::meta::tokens cast_tokens = ($::meta::tokens)copied;
    $::static_assert($::meta::is_kind(copied, "splice"), "children preserve structured splice");
    $::meta::span span = ($::meta::span)$::meta::span(copied);
    return $::quote { 10u32 * $::unquote(copied) };
}
syntax Inspect : expression { prefix "inspect"; match "(" value:expr ")"; expand inspect; }
syntax Inspect;
$::static_assert(inspect(3u32 + 4u32) == 70u32, "copied structured expression retains grouping");
global u32 entry() { return inspect(3u32 + 4u32); }
]=])
check(splice_spelling "requires a lexical leaf" [=[
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "value");
    $::meta::tokens one = $::quote { $::unquote(node) };
    $::meta::spelling(one);
    return $::quote { 1u32 };
}
syntax Inspect : expression { prefix "inspect"; match "(" value:expr ")"; expand inspect; }
syntax Inspect;
global u32 entry() { return inspect(1u32); }
]=])
check(diagnostics pass [=[
[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) {
    $::meta::span span = $::meta::span(input);
    $::meta::warning(span, "token warning");
    $::meta::note(span, "token note");
    return $::quote { 1u32 };
}
global u32 entry() { return inspect!{ (anchor) }; }
]=])
check(error "token error" [=[
[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) {
    $::meta::error($::meta::span(input), "token error");
    return $::quote { 1u32 };
}
global u32 entry() { return inspect!{ anchor }; }
]=])
foreach(operation spelling children delimiter span)
    if(operation STREQUAL spelling)
        set(result_type "$::meta::bytes")
    elseif(operation STREQUAL children)
        set(result_type "$::meta::tokens")
    elseif(operation STREQUAL delimiter)
        set(result_type "const u8 *")
    else()
        set(result_type "$::meta::span")
    endif()
    check(${operation}_many "requires exactly one token-tree element"
        "[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) { ${result_type} result = $::meta::${operation}(input); return input; } global u32 entry() { return inspect!{a b}; }")
    check(${operation}_empty "requires exactly one token-tree element"
        "[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) { ${result_type} result = $::meta::${operation}(input); return input; } global u32 entry() { return inspect!{}; }")
    check(${operation}_unused_type "incompatible argument type"
        "static $::meta::tokens unused() { ${result_type} result = $::meta::${operation}(1u32); return $::quote { 1u32 }; }")
endforeach()

check(constructors pass [=[
namespace Library {
    static u32 selected() { return 17u32; }
    static $::meta::tokens build(in $::meta::tokens input) {
        $::meta::span span = $::meta::span(input);
        $::meta::tokens name = $::meta::token("identifier", "selected", span);
        return $::meta::concat(name, $::meta::group("()", $::quote {}, span));
    }
    [[macro]] static $::meta::tokens construct(in $::meta::tokens input) {
        $::static_assert($::meta::is_kind($::meta::token("identifier", "namespace"), "identifier"), "keyword leaf");
        $::static_assert($::meta::is_kind($::meta::token("builtin", "$::alignof"), "builtin"), "builtin leaf");
        $::static_assert($::meta::is_kind($::meta::token("floating", "1.5f64"), "floating"), "floating leaf");
        $::static_assert($::meta::is_kind($::meta::token("character", "'a'"), "character"), "character leaf");
        $::static_assert($::meta::is_kind($::meta::token("string", "\"hello\""), "string"), "string leaf");
        $::static_assert($::meta::is_kind($::meta::token("punctuation", "->"), "punctuation"), "punctuation leaf");
        $::meta::tokens integer = $::meta::token("integer", $::meta::spelling($::quote { 7u32 }));
        $::static_assert($::meta::is_kind(integer, "integer"), "exact-byte leaf");
        $::static_assert($::meta::is_kind($::meta::group("[]", integer), "group"), "bracket group");
        $::static_assert($::meta::len($::meta::children($::meta::group("{}", $::quote {}))) == 0uptr, "empty block");
        $::static_assert($::meta::delimiter($::meta::group("[[]]", $::quote { packed }))[2uptr] == ']', "attribute group");
        return build(input);
    }
}
static u32 selected() { return 29u32; }
$::static_assert(Library::construct!{ caller_span } == 17u32, "span does not import caller lookup");
global u32 entry() { return Library::construct!{ caller_span }; }
]=])

check(constructed_splice pass [=[
[[syntax_expander]] static $::meta::tokens construct(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "value");
    $::meta::tokens contents = $::quote { $::unquote(node) };
    $::meta::tokens group = $::meta::group("()", contents, $::meta::node_span(node));
    $::static_assert($::meta::is_kind($::meta::children(group), "splice"), "opaque copied child");
    return $::quote { 10u32 * $::unquote(group) };
}
syntax Construct : expression { prefix "construct"; match "(" value:expr ")"; expand construct; }
syntax Construct;
$::static_assert(construct(3u32 + 4u32) == 70u32, "constructed group and splice compose");
global u32 entry() { return construct(3u32 + 4u32); }
]=])

foreach(text "" " x" "x " "x/*comment*/" "x y" "1u32")
    string(MAKE_C_IDENTIFIER "bad_${text}" case)
    check(token_${case} "must spell exactly one leaf"
        "[[macro]] static $::meta::tokens construct(in $::meta::tokens input) { return $::meta::token(\"identifier\", \"${text}\"); } global u32 entry() { return construct!{}; }")
endforeach()
foreach(delimiter "(" ")" "[" "]" "[[" "]]" "{" "}")
    string(HEX "${delimiter}" case)
    check(token_delimiter_${case} "cannot construct group delimiters"
        "[[macro]] static $::meta::tokens construct(in $::meta::tokens input) { return $::meta::token(\"punctuation\", \"${delimiter}\"); } global u32 entry() { return construct!{}; }")
endforeach()
foreach(kind group splice nonexistent)
    check(token_kind_${kind} "requires a lexical leaf kind"
        "[[macro]] static $::meta::tokens construct(in $::meta::tokens input) { return $::meta::token(\"${kind}\", \"leaf\"); } global u32 entry() { return construct!{}; }")
endforeach()
check(group_delimiter "requires one of" [=[
[[macro]] static $::meta::tokens construct(in $::meta::tokens input) { return $::meta::group("<>", input); }
global u32 entry() { return construct!{}; }
]=])
foreach(operation token group)
    if(operation STREQUAL token)
        set(contents "\"leaf\"")
    else()
        set(contents "input")
    endif()
    check(${operation}_span_type "incompatible argument type"
        "static $::meta::tokens unused(in $::meta::tokens input) { return $::meta::${operation}(\"identifier\", ${contents}, 1u32); }")
    check(${operation}_arity "invalid argument count"
        "static $::meta::tokens unused() { return $::meta::${operation}(\"identifier\"); }")
endforeach()
set(budget_source [=[
[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) {
    for (u32 at = 0u32; at < 2000u32; ++at) {
        $::meta::bytes discarded = $::meta::spelling(input);
    }
    return $::quote { 1u32 };
}
global u32 entry() { return inspect!{ retained_spelling }; }
]=])
check(discarded_budget "translation-time meta memory budget exceeded" "${budget_source}"
    -feval-memory-limit=16384)
check(discarded_budget_ample pass "${budget_source}" -feval-memory-limit=262144)
foreach(operation span tokens)
    if(operation STREQUAL span)
        set(argument "$::quote { leaf }")
    else()
        set(argument "node")
    endif()
    foreach(query sizeof "$::alignof")
        string(MAKE_C_IDENTIFIER "${query}" query_case)
        check(${operation}_${query_case} "runtime (size|layout|object)|translation-only"
            "static $::meta::tokens unused(in $::meta::syntax node) { ${query}($::meta::${operation}(${argument})); return $::quote { 1u32 }; }")
    endforeach()
endforeach()
foreach(query sizeof "$::alignof")
    string(MAKE_C_IDENTIFIER "${query}" query_case)
    foreach(call IN ITEMS
            "$::meta::len()"
            "$::meta::len(1u32)"
            "$::meta::len($::quote { one }, $::quote { two })"
            "$::meta::child_count(1u32)"
            "$::syntax::count(1u32, \"field\")"
            "$::meta::is_kind(1u32, \"integer\")"
            "$::meta::is_kind($::quote { one }, 1u32)"
            "$::meta::len($::meta::parse(1u32))"
            "$::meta::len($::meta::token(\"identifier\", 1u32))"
            "$::meta::len($::meta::group(\"()\", 1u32))"
            "$::meta::len($::meta::concat($::quote { one }, $::meta::spelling($::quote { two })))"
            "$::meta::cap($::meta::alloc(1.0f64))"
            "$::meta::data($::meta::freeze($::meta::alloc(1uptr), 1.0f64))")
        string(MD5 key "${call}")
        check(unevaluated_${query_case}_${key}
            "(argument (count|type) for translation-only operation|same sequence type)"
            "$::static_assert(${query}(${call}) > 0uptr, \"source type check\"); global u32 entry() { return 1u32; }")
    endforeach()
    check(unevaluated_${query_case}_unquote "unquote requires a token value or syntax node"
        "$::static_assert(${query}($::meta::len($::quote { $::unquote(1u32) })) > 0uptr, \"invalid unquote type\");")
    foreach(nested sizeof "$::alignof")
        string(MAKE_C_IDENTIFIER "${nested}" nested_case)
        foreach(operand "void" "$::quote {}" "$::meta::spelling($::quote { leaf })")
            string(MD5 key "${operand}")
            check(unevaluated_${query_case}_${nested_case}_${key}
                "(requires a complete object type|meta values have no runtime size or alignment)"
                "$::static_assert(${query}(${nested}(${operand})) > 0uptr, \"invalid nested query\");")
        endforeach()
    endforeach()
endforeach()
check(unevaluated_controls pass [=[
$::static_assert(sizeof($::meta::len($::quote { absent!() })) == sizeof(uptr), "opaque input is not expanded");
$::static_assert($::alignof($::meta::len($::quote {})) == $::alignof(uptr), "target-owned alignment");
$::static_assert(sizeof($::meta::is_kind($::quote { leaf }, "invalid kind")) == sizeof(bool), "names checked only on execution");
$::static_assert(sizeof($::meta::len($::meta::parse("\""))) == sizeof(uptr), "invalid text is not parsed");
$::static_assert(sizeof($::meta::len($::meta::at($::quote {}, 99uptr))) == sizeof(uptr), "no indexed read");
$::static_assert(sizeof($::meta::len($::meta::token("integer", "not-an-integer"))) == sizeof(uptr), "no token construction");
$::static_assert(sizeof($::meta::len($::meta::group("invalid delimiter", $::quote {}))) == sizeof(uptr), "no group construction");
$::static_assert(sizeof($::meta::cap($::meta::alloc(~0uptr))) == sizeof(uptr), "no allocation");
$::static_assert(sizeof($::meta::data($::meta::freeze($::meta::alloc(0uptr), 99uptr))) == sizeof(u8 *), "no buffer access");
$::static_assert(sizeof($::meta::len($::quote { $::unquote($::meta::at($::quote {}, 99uptr)) })) == sizeof(uptr), "valid unquote remains unexecuted");
$::static_assert(sizeof(sizeof($::meta::len($::quote {}))) == sizeof(uptr), "valid nested query");
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::static_assert(sizeof($::syntax::count(input, "absent")) == sizeof(uptr), "no match lookup");
    $::static_assert(sizeof($::meta::child_count($::meta::child($::syntax::node(input, "value"), 999uptr))) == sizeof(uptr), "no tree traversal");
    $::static_assert(sizeof($::meta::child_count($::meta::parse("invalid category", $::quote {}, $::syntax::context(input)))) == sizeof(uptr), "no fragment parse");
    return $::quote { 7u32 };
}
syntax Inspect : expression { prefix "inspect"; match "(" value:expr ")"; expand inspect; }
syntax Inspect;
$::static_assert(inspect(1u32) == 7u32, "unevaluated node and match APIs");
global u32 entry() { return 1u32; }
]=])
set(unevaluated_capture [=[
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Drop : expression { prefix "drop"; match "(" value:expr ")"; expand drop; }
syntax Keep : expression { prefix "keep"; match "(" value:expr ")"; expand keep; }
syntax Drop, Keep;
]=])
check(unevaluated_discard pass "${unevaluated_capture}
$::static_assert(drop(sizeof($::meta::len(1u32))) == 1u32, \"discarded intrinsic\");
$::static_assert(drop(sizeof(sizeof(void))) == 1u32, \"discarded nested query\");
$::static_assert(drop(sizeof($::meta::len($::quote { $::unquote(1u32) }))) == 1u32, \"discarded unquote\");
$::static_assert(drop(sizeof((u32)$::quote {})) == 1u32, \"discarded opaque cast\");
$::static_assert(drop(sizeof($::quote {} == $::quote {})) == 1u32, \"discarded opaque comparison\");
global u32 entry() { return 1u32; }")
check(unevaluated_survival "incompatible argument type for translation-only operation"
    "${unevaluated_capture} $::static_assert(keep(sizeof($::meta::len(1u32))) > 0uptr, \"surviving intrinsic\");")
check(opaque_survival "quote cannot enter runtime expressions"
    "${unevaluated_capture} $::static_assert(keep(sizeof((u32)$::quote {})) > 0uptr, \"surviving opaque cast\");")
check(unevaluated_global "invalid argument count for translation-only operation"
    "global uptr size = sizeof($::meta::len());")
check(unevaluated_array "incompatible argument type for translation-only operation"
    "global u32 values[sizeof($::meta::child_count(1u32))];")
foreach(query sizeof "$::alignof")
    string(MAKE_C_IDENTIFIER "${query}" query_case)
    foreach(expression IN ITEMS
            "!$::quote {}" "(u32)$::quote {}" "(void *)$::quote {}"
            "$::quote {} == $::quote {}"
            "$::meta::len(($::meta::tokens)1u32)"
            "(u32)(1u32 ? $::quote {} : $::meta::spelling($::quote { one }))"
            "$::quote {} ? 1u32 : 2u32"
            "(u32)($::quote {} + $::quote {})"
            "(u32)&$::quote {}" "(u32)($::quote {} = $::quote {})"
            "(u32)++$::quote {}" "(u32)($::quote {}).member")
        string(MD5 key "${expression}")
        check(opaque_${query_case}_${key}
            "(opaque meta|incompatible meta value|quote cannot enter runtime|conditional meta|meta assignment)"
            "$::static_assert(${query}(${expression}) > 0uptr, \"opaque source constraint\");")
    endforeach()
    check(opaque_${query_case}_assignment "meta assignment requires a local cell"
        "$::static_assert(${query}($::meta::len($::quote {} = $::quote {})) > 0uptr, \"invalid meta destination\");")
endforeach()
foreach(type tokens syntax syntax_match span context bytes buffer)
    set(meta_type "$::meta::${type}")
    set(meta_declarations "static ${meta_type} value() { ${meta_type} unassigned; return unassigned; } static uptr measure(in ${meta_type} input) { return 1uptr; }")
    foreach(expression IN ITEMS "!value()" "value() == value()" "(u32)value()"
            "(u32)((${meta_type} *)0uptr)" "(u32)&value()"
            "(u32)(value() = value())" "(u32)(1u32 ? value() : 1u32)"
            "(u32)value()()" "(u32)value()[0uptr]" "(u32)value().field")
        string(MD5 key "${expression}")
        foreach(query sizeof "$::alignof")
            string(MAKE_C_IDENTIFIER "${query}" query_case)
            check(opaque_${type}_${query_case}_${key}
                "(opaque meta|incompatible meta value|conditional meta|meta assignment)"
                "${meta_declarations} $::static_assert(${query}(${expression}) > 0uptr, \"opaque source constraint\");")
        endforeach()
    endforeach()
    foreach(query sizeof "$::alignof")
        string(MAKE_C_IDENTIFIER "${query}" query_case)
        check(opaque_${type}_${query_case}_assignment "meta assignment requires a local cell"
            "${meta_declarations} $::static_assert(${query}(measure(value() = value())) > 0uptr, \"invalid meta destination\");")
    endforeach()
    check(opaque_${type}_unselected "incompatible meta value in cast"
        "${meta_declarations} $::static_assert(1u32 || (u32)value(), \"untaken source constraint\");")
    check(opaque_${type}_copy_controls pass "${meta_declarations}
static uptr inspect(in ${meta_type} input, in const ${meta_type} other) {
    return sizeof(measure(input = other)) + sizeof(measure(1u32 ? other : input));
}
$::static_assert(sizeof(measure((${meta_type})value())) == sizeof(uptr), \"same-type cast\");
$::static_assert(sizeof(measure(1u32 ? value() : value())) == sizeof(uptr), \"same-type selection\");
$::static_assert(sizeof(inspect(value(), value())) == sizeof(uptr), \"unevaluated typed calls\");
global u32 entry() { return 1u32; }")
endforeach()
check(unknown_kind "unknown token-tree kind" [=[
[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) {
    $::meta::is_kind(input, "core"); return input;
}
global u32 entry() { return inspect!{a}; }
]=])
check(group_spelling "requires a lexical leaf" [=[
[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) {
    $::meta::spelling(input); return input;
}
global u32 entry() { return inspect!{(a)}; }
]=])
foreach(operation children delimiter)
    check(${operation}_leaf "requires a balanced group"
        "[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) { $::meta::${operation}(input); return input; } global u32 entry() { return inspect!{a}; }")
endforeach()
check(zero_byte "contains a zero byte" [=[
[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) {
    $::meta::buffer storage = $::meta::alloc(3uptr);
    u8 *data = $::meta::data(storage);
    data[0uptr] = 'a'; data[1uptr] = 0u8; data[2uptr] = 'b';
    return $::meta::parse($::meta::freeze(storage, 3uptr));
}
global u32 entry() { return inspect!{}; }
]=])
foreach(bytes "192u8, 175u8" "237u8, 160u8, 128u8" "244u8, 144u8, 128u8, 128u8" "226u8")
    string(MAKE_C_IDENTIFIER "${bytes}" case)
    string(REPLACE ", " ";" values "${bytes}")
    set(assignments "data[0uptr] = 34u8;")
    set(index 1)
    foreach(byte IN LISTS values)
        string(APPEND assignments " data[${index}uptr] = ${byte};")
        math(EXPR index "${index} + 1")
    endforeach()
    string(APPEND assignments " data[${index}uptr] = 34u8;")
    math(EXPR size "${index} + 1")
    check(utf8_${case} "input is not valid UTF-8"
        "[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) { $::meta::buffer storage = $::meta::alloc(${size}uptr); u8 *data = $::meta::data(storage); ${assignments} return $::meta::parse($::meta::freeze(storage, ${size}uptr)); } global u32 entry() { return inspect!{}; }")
endforeach()

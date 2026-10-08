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
    set(trace "while evaluating call to 'descend'")
    if(name STREQUAL probe_chain_limit)
        # This constraint is found while checking the first definition whose
        # source-proof chain is too deep, before descend itself is reached.
        set(trace "while evaluating call to 'probe32'")
    elseif(name MATCHES "^context_unused")
        set(trace "")
    endif()
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags} ${ARGN}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: expected success\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:" OR
               NOT err MATCHES "${trace}")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}' and call ancestry\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
set(recursive [=[
[[eval_only]] static u32 descend(in u32 value) {
    if (value) return 1u32 + descend(value - 1u32);
    return 0u32;
}
]=])
check(default_depth pass "${recursive}\n$::static_assert(descend(192u32) == 192u32, \"deep result\");")
check(increased_depth pass "${recursive}\n$::static_assert(descend(511u32) == 511u32, \"deeper result\");"
    -feval-depth-limit=512)
check(default_limit "translation-time recursion depth exceeded 256"
    "${recursive}\n$::static_assert(descend(300u32) == 300u32, \"default depth limit\");")
check(reduced_limit "translation-time recursion depth exceeded 32"
    "${recursive}\n$::static_assert(descend(64u32) == 64u32, \"reduced depth limit\");"
    -feval-depth-limit=32)
check(step_limit "translation-time instruction budget exceeded 500"
    "${recursive}\n$::static_assert(descend(192u32) == 192u32, \"work limit\");"
    -feval-step-limit=500)

# Required constants encountered while checking source must use the same pump
# and recursion budget, even when they recursively need the body being checked.
foreach(constraint case alignment designator)
    if(constraint STREQUAL case)
        set(body "switch (0u32) { case descend(0u32): break; }")
    elseif(constraint STREQUAL alignment)
        set(body "[[aligned(descend(0u32))]] u32 object;")
    else()
        set(body "u32 values[1] = { [descend(0u32)] = 0u32 };")
    endif()
    check(validation_${constraint}_limit "translation-time recursion depth exceeded 256"
        "[[eval_only]] static u32 descend(in u32 value) { if (0u32) { ${body} } return descend(value); }\n$::static_assert(descend(0u32), \"cyclic validation\");")
endforeach()

# Source-value probes must not reset budgets or start recursive native pumps.
# Exercise vector bounds, integer-zero comparisons and explicit pointer casts.
foreach(proof vector equality cast)
    if(proof STREQUAL vector)
        set(body "u32 [[ext_vector_type(4)]] lanes = 0u32; if (0u32) return lanes[$::eval(descend(0u32))];")
    elseif(proof STREQUAL equality)
        set(body "u32 *pointer = (u32 *)0uptr; if (0u32) return pointer == $::eval(descend(0u32));")
    else()
        set(body "if (0u32) { u32 *pointer = (u32 *)$::eval(descend(0u32)); }")
    endif()
    set(source "[[eval_only]] static u32 descend(in u32 value) { ${body} return value; }\n$::static_assert(descend(0u32) == 0u32, \"cyclic source probe\");")
    check(probe_${proof}_default "translation-time recursion depth exceeded 256" "${source}")
    check(probe_${proof}_small "translation-time recursion depth exceeded 8" "${source}" -feval-depth-limit=8)
endforeach()

set(source [=[
[[eval_only]] static u32 descend(in u32 value) {
    if (value) return descend(value - 1u32);
    return 0u32;
}
[[eval_only]] static u32 checked() {
    u32 [[ext_vector_type(4)]] lanes = 7u32;
    return lanes[$::eval(descend(192u32))];
}
$::static_assert(checked() == 7u32, "deep source probe result");
]=])
check(probe_deep_success pass "${source}")
check(probe_deep_work "translation-time instruction budget exceeded 500" "${source}" -feval-step-limit=500)
check(probe_deep_limit "translation-time recursion depth exceeded 32" "${source}" -feval-depth-limit=32)

# An isolated source proof retains expansion capabilities but not invocation
# locals. Definition checking defers only values that need the absent context;
# the invoked body still checks constraints in untaken branches.
set(context_counter [=[
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
]=])
set(context_body [=[
    u32 *pointer = (u32 *)0uptr;
    if (pointer != $::eval(count($::quote {}))) return $::quote {0u32};
    if (pointer != $::eval(count($::meta::parse("")))) return $::quote {0u32};
    if (pointer != $::eval(count($::meta::call_site($::quote {name})) - 1uptr))
        return $::quote {0u32};
    if (pointer != $::eval(count($::meta::gensym("name")) - 1uptr))
        return $::quote {0u32};
    u32 *casted = (u32 *)$::eval(count($::quote {}));
    u32 [[ext_vector_type(4)]] lanes = 7u32;
    $::static_assert(lanes[$::eval(count($::quote {}))] == 7u32, "contextual lane proof");
    return $::quote {17u32};
]=])
check(context_macro pass "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${context_body} }\n$::static_assert(descend!() == 17u32, \"macro context\");")
check(context_syntax pass "${context_counter}\n[[syntax_expander]] static $::meta::tokens descend(in $::meta::syntax_match input) { ${context_body} }\nsyntax Check : expression { prefix \"context_check\"; match \"(\" \")\"; expand descend; }\nsyntax Check;\n$::static_assert(context_check() == 17u32, \"syntax context\");")
check(context_untaken_nonzero "pointer/integer equality requires an integer constant zero"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 *pointer = (u32 *)0uptr; if (0u32) { if (pointer == $::eval(count($::quote {value}))) return input; } return $::quote {17u32}; }\n$::static_assert(descend!() == 17u32, \"untaken proof still checked\");")
check(context_unused_deferred pass
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 *pointer = (u32 *)0uptr; if (pointer == $::eval(count($::quote {}))) return input; return $::quote {}; }")
check(context_unused_independent "pointer/integer equality requires an integer constant zero"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 *pointer = (u32 *)0uptr; if (pointer == $::eval(1u32 ? 1uptr : count($::quote {}))) return input; return $::quote {}; }")
check(context_unused_bad_type "opaque meta values do not support binary operators"
    "[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 *pointer = (u32 *)0uptr; if (pointer == $::eval($::quote {} + 0uptr)) return input; return $::quote {}; }")
set(source "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 *pointer = (u32 *)0uptr; if (pointer == $::eval(count($::meta::parse(\"((((((((((0u32))))))))))\")) - 1uptr)) return $::quote {17u32}; return $::quote {0u32}; }\n$::static_assert(descend!() == 17u32, \"probe token depth\");")
check(context_token_depth_success pass "${source}")
check(context_token_depth_limit "translation-time token-tree nesting depth exceeded" "${source}" -feval-depth-limit=8)
string(REPEAT " " 8192 parse_trivia)
set(source "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 *pointer = (u32 *)0uptr; if (pointer == $::eval(count($::meta::parse(\"${parse_trivia}\")))) return $::quote {17u32}; return $::quote {0u32}; }\n$::static_assert(descend!() == 17u32, \"probe token bytes\");")
check(context_token_bytes_success pass "${source}")
check(context_token_bytes_limit "input exceeds target uptr or byte capacity" "${source}" -feval-byte-limit=4096)

set(required_context_body [=[
    [[aligned($::eval(count($::quote {one})))]] u32 aligned_value;
    struct Entry { u32 value; u32 other; };
    struct Entry values[3] = {
        [$::eval(count($::quote {}))] = { .value = 3u32, .other = 5u32 },
        { .value = 7u32, .other = 11u32 },
        [2u32] = { .value = 13u32, .other = 17u32 }
    };
    $::static_assert(values[0u32].value == 3u32 && values[1u32].other == 11u32 &&
                     values[2u32].value == 13u32, "symbolic positions become actual destinations");
    switch (0u32) { case $::eval(count($::quote {})): break; default: return $::quote {0u32}; }
    return $::quote {17u32};
]=])
check(context_required_macro pass "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${required_context_body} }\n$::static_assert(descend!() == 17u32, \"required macro context\");")
check(context_required_syntax pass "${context_counter}\n[[syntax_expander]] static $::meta::tokens descend(in $::meta::syntax_match input) { ${required_context_body} }\nsyntax Check : expression { prefix \"context_check\"; match \"(\" \")\"; expand descend; }\nsyntax Check;\n$::static_assert(context_check() == 17u32, \"required syntax context\");")
check(context_unused_required pass "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${required_context_body} }")

# Inferred extents come from the same concrete initializer plan used to store
# values. Positional entries follow the selected element, and nested layout,
# zero fill and later sizeof queries use the completed invocation-local type.
set(inferred_context_body [=[
    u32 values[] = { [$::eval(count($::quote {one two}))] = 7u32, 11u32, [0u32] = 3u32 };
    $::static_assert(sizeof(values) == 4uptr * sizeof(u32), "inferred contextual extent");
    $::static_assert(values[0u32] == 3u32 && values[1u32] == 0u32 &&
        values[2u32] == 7u32 && values[3u32] == 11u32, "inferred contents");
    struct Entry { u8 name[4]; uptr value; };
    struct Entry entries[] = {
        [$::eval(count($::quote {one}))] = { .name = "abc", .value = sizeof(uptr) },
        { .name = "def", .value = 19uptr }
    };
    $::static_assert(sizeof(entries) == 3uptr * sizeof(struct Entry), "record extent");
    $::static_assert(entries[0u32].value == 0uptr && entries[1u32].name[2u32] == 'c' &&
        entries[1u32].value == sizeof(uptr) && entries[2u32].value == 19uptr, "record storage");
    u32 matrix[][2] = { [$::eval(count($::quote {one two}))][1u32] = 23u32 };
    $::static_assert(sizeof(matrix) == 6uptr * sizeof(u32) && matrix[2u32][1u32] == 23u32 &&
        matrix[0u32][0u32] == 0u32, "chained inferred destination");
    u8 text[] = "abc";
    u8 empty[] = "";
    $::static_assert(sizeof(text) == 4uptr && sizeof(empty) == 1uptr && text[3u32] == 0u8,
        "direct inferred strings");
    return $::quote {17u32};
]=])
check(context_inferred_macro pass "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${inferred_context_body} }\n$::static_assert(descend!() == 17u32, \"inferred macro context\");")
check(context_inferred_syntax pass "${context_counter}\n[[syntax_expander]] static $::meta::tokens descend(in $::meta::syntax_match input) { ${inferred_context_body} }\nsyntax Check : expression { prefix \"context_check\"; match \"(\" \")\"; expand descend; }\nsyntax Check;\n$::static_assert(context_check() == 17u32, \"inferred syntax context\");")
check(context_unused_inferred pass "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${inferred_context_body} }")
check(context_inferred_budget "translation-time object exceeds target layout or byte budget"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 values[] = { [$::eval(count($::quote {one}) * 4096uptr)] = 7u32 }; return $::quote {17u32}; }\n$::static_assert(descend!() == 17u32, \"inferred allocation budget\");"
    -feval-byte-limit=4096)
check(context_unused_inferred_empty "an omitted array bound requires a nonempty initializer"
    "[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { if (0u32) { u32 values[] = {}; } return input; }")
check(context_inferred_extent_limit "inferred array bound exceeds the language limit|array initializer designator is out of range"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { if (0u32) { u8 values[] = { [$::eval(count($::quote {one}) * 4294967295uptr)] = 7u8 }; } return $::quote {17u32}; }\n$::static_assert(descend!() == 17u32, \"inferred extent limit checked even when untaken\");")
check(context_inferred_duplicate "duplicate destination in aggregate initializer"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { if (0u32) { u32 values[] = { [$::eval(count($::quote {}))] = 1u32, [0u32] = 2u32 }; } return $::quote {17u32}; }\n$::static_assert(descend!() == 17u32, \"inferred duplicate checked even when untaken\");")
check(context_inferred_vla pass [=[
[[eval_only]] static u32 descend(in uptr count) {
    u32 values[count] = { [2u32] = 7u32 };
    $::static_assert(sizeof(values) == 5uptr * sizeof(u32) && values[4u32] == 0u32,
        "explicit VLA extent is not replaced by initializer minimum");
    return values[2u32];
}
$::static_assert(descend(5uptr) == 7u32, "explicit VLA execution");
]=])
check(context_inferred_vla_range "array initializer designator is out of range" [=[
[[eval_only]] static u32 descend(in uptr count) {
    u32 values[count] = { [2u32] = 7u32 };
    return 17u32;
}
$::static_assert(descend(2uptr) == 17u32, "VLA capacity checked");
]=])

# A missing expansion context can leave an inferred outer extent unknown.
# Later required queries inherit that dependency, including transitively,
# without making element/pointer/alignment queries or unselected arms defer.
set(dependent_context_body [=[
    u32 values[] = { [$::eval(count($::quote {one two}))] = 7u32 };
    u32 following[] = { [sizeof(values) / sizeof(u32)] = 11u32 };
    u32 third[] = { [sizeof(following) / sizeof(u32)] = 13u32 };
    [[aligned(sizeof(*(&values)) / sizeof(u32) + 1uptr)]] u32 aligned_value;
    u32 *zero = (u32 *)$::eval(sizeof(*(&values)) - 3uptr * sizeof(u32));
    u32 *selected = (u32 *)$::eval(sizeof(*(1u32 ? &values : &values)) - 3uptr * sizeof(u32));
    u32 [[ext_vector_type(4)]] lanes = 17u32;
    $::static_assert(sizeof(third) == 5uptr * sizeof(u32) && third[4u32] == 13u32,
        "transitive inferred extent");
    $::static_assert(following[3u32] == 11u32 && following[0u32] == 0u32,
        "dependent initializer contents");
    $::static_assert(lanes[sizeof(values) / sizeof(u32) - 3uptr] == 17u32,
        "dependent source vector proof");
    u32 independent[] = { [1u32 ? 0uptr : sizeof(values)] = 19u32 };
    $::static_assert(sizeof(independent) == sizeof(u32), "unselected extent stays independent");
    {
        u32 values[2];
        u32 *shadow = (u32 *)$::eval(sizeof(values) - 2uptr * sizeof(u32));
    }
    return $::quote {17u32};
]=])
check(context_dependent_macro pass "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${dependent_context_body} }\n$::static_assert(descend!() == 17u32, \"dependent macro extent\");")
check(context_dependent_syntax pass "${context_counter}\n[[syntax_expander]] static $::meta::tokens descend(in $::meta::syntax_match input) { ${dependent_context_body} }\nsyntax Check : expression { prefix \"context_check\"; match \"(\" \")\"; expand descend; }\nsyntax Check;\n$::static_assert(context_check() == 17u32, \"dependent syntax extent\");")
check(context_unused_dependent pass "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${dependent_context_body} }")
set(dependent_array "u32 values[] = { [$::eval(count($::quote {one two}))] = 7u32 };")
foreach(constraint alignment designator pointer case)
    if(constraint STREQUAL alignment)
        set(body "[[aligned(sizeof(values) / sizeof(u32))]] u32 object;")
        set(reason "aligned argument must be a positive power-of-two")
    elseif(constraint STREQUAL designator)
        set(body "u32 following[1] = { [sizeof(values) / sizeof(u32)] = 11u32 };")
        set(reason "array initializer designator is out of range")
    elseif(constraint STREQUAL pointer)
        set(body "u32 *pointer = (u32 *)0uptr; if (pointer == $::eval(sizeof(values))) return input;")
        set(reason "pointer/integer equality requires an integer constant zero")
    else()
        set(body "switch (0u32) { case sizeof(values): break; case 3uptr * sizeof(u32): break; }")
        set(reason "duplicate case value in switch")
    endif()
    set(definition "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${dependent_array} if (0u32) { ${body} } return $::quote {17u32}; }")
    check(context_unused_dependent_${constraint} pass "${definition}")
    check(context_dependent_${constraint} "${reason}" "${definition}\n$::static_assert(descend!() == 17u32, \"dependent untaken constraint\");")
endforeach()
check(context_unused_dependent_independent pass
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${dependent_array} u32 *element = (u32 *)$::eval(sizeof(values[0u32]) - sizeof(u32)); u32 *pointer = (u32 *)$::eval(sizeof(&values) - sizeof(u32 *)); u32 *alignment = (u32 *)$::eval($::alignof(values) - $::alignof(u32)); return input; }")
check(context_unused_dependent_unselected "array initializer designator is out of range"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${dependent_array} u32 fixed[1] = { [1u32 ? 1uptr : sizeof(values)] = 11u32 }; return input; }")
check(context_unused_dependent_case_unselected "duplicate case value in switch"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${dependent_array} switch (0u32) { case 0u32: break; case 1u32 ? 0uptr : sizeof(values): break; } return input; }")
check(context_unused_dependent_inner pass
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 values[][2] = { { [$::eval(count($::quote {}))] = 7u32 } }; u32 *pointer = (u32 *)$::eval(sizeof(values) - 2uptr * sizeof(u32)); return input; }")
check(context_unused_dependent_inner_error "array initializer designator is out of range"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 values[][2] = { { [$::eval(count($::quote {}))] = 7u32 } }; u32 fixed[2] = { [sizeof(values) / sizeof(u32)] = 11u32 }; return input; }")
check(context_unused_dependent_incomplete "sizeof requires a complete object type with fixed size"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${dependent_array} u32 fixed[1] = { [sizeof(u32[])] = 11u32 }; return input; }")
check(context_unused_dependent_vla "sizeof requires a complete object type with fixed size"
    "[[eval_only]] static u32 descend(in uptr count) { u32 values[count]; u32 following[] = { [sizeof(values)] = 11u32 }; return 17u32; }")

# Extent-sensitive source conversions defer only the extent, not the pointee
# shape/qualifiers. Invocation validation repeats them with completed types.
set(pointer_context_helper [=[
[[eval_only]] static u32 read_array(in u32 (*pointer)[3]) { return (*pointer)[2u32]; }
]=])
set(pointer_context_body [=[
    u32 values[] = { [$::eval(count($::quote {one two}))] = 7u32 };
    u32 (*pointer)[3] = &values;
    const u32 (*qualified)[3] = &values;
    u32 (*assigned)[3] = (u32 (*)[3])0uptr;
    assigned = &values;
    $::static_assert((*pointer)[2u32] == 7u32 && (*qualified)[0u32] == 0u32 &&
        (*assigned)[2u32] == 7u32 && read_array(&values) == 7u32, "inferred pointer conversions");
    $::static_assert((&values + 1uptr) - &values == 1iptr, "inferred pointer stride");
    $::static_assert((&values + 1uptr) - pointer == 1iptr, "extent-sensitive pointer subtraction");
    u32 matrix[][2] = { [$::eval(count($::quote {one}))] = { 11u32, 13u32 } };
    u32 (*nested)[2][2] = &matrix;
    $::static_assert((*nested)[1u32][1u32] == 13u32, "nested pointed-to array");
    return $::quote {17u32};
]=])
check(context_pointer_macro pass "${context_counter}\n${pointer_context_helper}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${pointer_context_body} }\n$::static_assert(descend!() == 17u32, \"inferred macro pointers\");")
check(context_pointer_syntax pass "${context_counter}\n${pointer_context_helper}\n[[syntax_expander]] static $::meta::tokens descend(in $::meta::syntax_match input) { ${pointer_context_body} }\nsyntax Check : expression { prefix \"context_check\"; match \"(\" \")\"; expand descend; }\nsyntax Check;\n$::static_assert(context_check() == 17u32, \"inferred syntax pointers\");")
check(context_unused_pointer pass "${context_counter}\n${pointer_context_helper}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${pointer_context_body} }")
foreach(boundary initializer assignment call subtraction)
    if(boundary STREQUAL initializer)
        set(body "u32 (*pointer)[4] = &values;")
    elseif(boundary STREQUAL assignment)
        set(body "u32 (*pointer)[4] = (u32 (*)[4])0uptr; pointer = &values;")
    elseif(boundary STREQUAL call)
        set(body "read_array(&values);")
    else()
        set(body "u32 (*pointer)[4] = (u32 (*)[4])0uptr; iptr distance = &values - pointer;")
    endif()
    set(array "${dependent_array}")
    if(boundary STREQUAL call)
        set(array "u32 values[] = { [$::eval(count($::quote {one two three}))] = 7u32 };")
    endif()
    set(definition "${context_counter}\n${pointer_context_helper}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${array} if (0u32) { ${body} } return $::quote {17u32}; }")
    check(context_unused_pointer_${boundary} pass "${definition}")
    if(boundary STREQUAL subtraction)
        set(reason "pointer subtraction requires matching pointer types")
    else()
        set(reason "implicit pointer conversion discards qualifiers or uses incompatible pointee types")
    endif()
    check(context_pointer_${boundary} "${reason}" "${definition}\n$::static_assert(descend!() == 17u32, \"untaken extent mismatch\");")
endforeach()
check(context_unused_pointer_element "implicit pointer conversion discards qualifiers or uses incompatible pointee types"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { ${dependent_array} if (0u32) { i32 (*pointer)[3] = &values; } return input; }")
check(context_unused_pointer_qualifiers "implicit pointer conversion discards qualifiers or uses incompatible pointee types"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { const u32 values[] = { [$::eval(count($::quote {one two}))] = 7u32 }; if (0u32) { u32 (*pointer)[3] = &values; } return input; }")
check(context_unused_pointer_inner "implicit pointer conversion discards qualifiers or uses incompatible pointee types"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 values[][2] = { [$::eval(count($::quote {one}))] = { 7u32, 11u32 } }; if (0u32) { u32 (*pointer)[2][3] = &values; } return input; }")
check(context_unused_pointer_return "eval_only function cannot return a pointer"
    "static u32 (*descend(in $::meta::tokens input))[3] { return (u32 (*)[3])0uptr; }")
check(context_pointer_addresses pass [=[
[[eval_only]] static u32 descend() {
    u32 scalar = 7u32;
    u32 values[3] = { 11u32, 13u32, 17u32 };
    $::static_assert((&scalar + 1uptr) - &scalar == 1iptr, "scalar local address arithmetic");
    $::static_assert((&values + 1uptr) - &values == 1iptr, "fixed-array local address arithmetic");
    return scalar + values[0u32];
}
$::static_assert(descend() == 18u32, "local addresses need not already have backing storage");
]=])
check(context_unused_pointer_address_proof "runtime local or parameter is not a translation-time value" [=[
[[eval_only]] static u32 descend() {
    u32 scalar = 7u32;
    u32 values[1] = { [$::eval((&scalar + 1uptr) - &scalar - 1iptr)] = 11u32 };
    return values[0u32];
}
]=])
foreach(constraint alignment designator case)
    if(constraint STREQUAL alignment)
        set(body "[[aligned($::eval(count($::quote {a b c})))]] u32 object;")
        set(reason "aligned argument must be a positive power-of-two")
    elseif(constraint STREQUAL designator)
        set(body "u32 values[1] = { [$::eval(count($::quote {one}))] = 1u32 };")
        set(reason "array initializer designator is out of range")
    else()
        set(body "switch (0u32) { case $::eval(count($::quote {})): break; case 0u32: break; }")
        set(reason "duplicate case value in switch")
    endif()
    set(definition "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { if (0u32) { ${body} } return $::quote {17u32}; }")
    check(context_unused_${constraint} pass "${definition}")
    check(context_invoked_${constraint} "${reason}" "${definition}\n$::static_assert(descend!() == 17u32, \"untaken required constraint\");")
endforeach()
set(unknown_index "$::eval(count($::quote {}))")
check(context_unused_alignment_type "required expression is not an integer"
    "[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { [[aligned($::quote {})]] u32 object; return input; }")
check(context_unused_designator_type "conversion cannot convert between a pointer and a floating type"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { struct Entry { u32 *pointer; }; struct Entry values[1] = { [${unknown_index}] = { .pointer = 1.5f64 } }; return input; }")
check(context_unused_designator_member "record initializer has no member named 'missing'"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { struct Entry { u32 value; }; struct Entry values[1] = { [${unknown_index}] = { .missing = 1u32 } }; return input; }")
check(context_unused_designator_duplicate "duplicate destination in aggregate initializer"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { struct Entry { u32 value; }; struct Entry values[1] = { [${unknown_index}] = { .value = 1u32, .value = 2u32 } }; return input; }")
check(context_unused_designator_known_range "array initializer designator is out of range"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 values[1] = { [${unknown_index}] = 1u32, [8u32] = 2u32 }; return input; }")
check(context_unused_designator_minimum_range "array initializer designator is out of range"
    "${context_counter}\n[[macro]] static $::meta::tokens descend(in $::meta::tokens input) { u32 values[1] = { [${unknown_index}] = 1u32, 2u32 }; return input; }")
foreach(constraint vector pointer alignment designator case)
    set(value "$::eval($::meta::len($::quote {}))")
    if(constraint STREQUAL vector)
        set(body "u32 [[ext_vector_type(4)]] lanes = 0u32; if (0u32) return lanes[${value}];")
    elseif(constraint STREQUAL pointer)
        set(body "u32 *pointer = (u32 *)0uptr; if (0u32) return pointer == ${value};")
    elseif(constraint STREQUAL alignment)
        set(body "if (0u32) { [[aligned(${value} + 1uptr)]] u32 object; }")
    elseif(constraint STREQUAL designator)
        set(body "if (0u32) { u32 values[1] = { [${value}] = 1u32 }; }")
    else()
        set(body "if (0u32) switch (0u32) { case ${value}: break; }")
    endif()
    set(definition "[[eval_only]] static u32 descend() { ${body} return 17u32; }")
    check(context_unused_helper_${constraint} pass "${definition}")
    check(context_missing_${constraint} "meta operation requires an active expansion context"
        "${definition}\n$::static_assert(descend() == 17u32, \"a call needs actual context\");")
endforeach()

foreach(resource memory byte)
    set(capacity 512)
    if(resource STREQUAL byte)
        set(capacity 2048)
    endif()
    set(source "[[eval_only]] static u32 descend() { $::meta::buffer data = $::meta::alloc(${capacity}uptr); return 0u32; }\n[[eval_only]] static u32 checked() { u32 [[ext_vector_type(4)]] lanes = 7u32; return lanes[$::eval(descend())]; }\n$::static_assert(checked() == 7u32, \"probe allocation\");")
    check(probe_${resource}_success pass "${source}")
    if(resource STREQUAL memory)
        check(probe_memory_limit "translation-time meta memory budget exceeded 1024" "${source}" -feval-memory-limit=1024)
    else()
        check(probe_byte_limit "capacity exceeds target uptr or 1024 bytes" "${source}" -feval-byte-limit=1024)
    endif()
endforeach()

# A long acyclic chain of different definitions also used to consume one host
# pump per source proof. Every edge is required even though its access is untaken.
set(chain "[[eval_only]] static u32 probe0() { return 0u32; }\n")
foreach(index RANGE 1 96)
    math(EXPR previous "${index} - 1")
    string(APPEND chain "[[eval_only]] static u32 probe${index}() { u32 [[ext_vector_type(4)]] lanes = 0u32; if (0u32) return lanes[$::eval(probe${previous}())]; return 0u32; }\n")
endforeach()
string(APPEND chain "[[eval_only]] static u32 descend() { return probe96(); }\n$::static_assert(descend() == 0u32, \"acyclic probe chain\");")
check(probe_chain_success pass "${chain}")
check(probe_chain_limit "translation-time recursion depth exceeded 32" "${chain}" -feval-depth-limit=32)

check(probe_value_isolation pass [=[
enum { Index = 99u32 };
[[eval_only]] static u32 checked(in u32 index) {
    u32 [[ext_vector_type(4)]] lanes = 7u32;
    u32 Index = index;
    u8 bytes[7];
    if (0u32) return lanes[Index];
    return lanes[sizeof(bytes) - 7uptr];
}
$::static_assert(checked(99u32) == 7u32, "probe has types, not invocation values or shadowed globals");
]=])

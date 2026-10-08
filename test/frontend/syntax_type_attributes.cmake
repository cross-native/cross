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
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
set(owner [=[
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote { 1u32 }; }
[[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) {
    return $::quote { sizeof($::unquote($::syntax::node(input, "value"))) };
}
syntax Drop : expression { prefix "drop"; match "(" value:type ")"; expand drop; }
syntax Emit : expression { prefix "emit"; match "(" value:type ")"; expand emit; }
syntax Drop, Emit;
]=])
foreach(pair
        "[[vendor::annotation]] u32|not valid as a type qualifier"
        "u32 [[atomic(1)]]|takes no arguments"
        "u32 [[atomic, atomic]]|duplicate 'atomic'"
        "u32 [[address_space(-1)]] *|nonnegative target registry number"
        "u32 [[address_space(7)]]|requires a pointer type"
        "u32 [[ext_vector_type(0)]]|requires one positive integer argument"
        "bool [[ext_vector_type(4)]]|vector element type"
        "u32 [[ext_vector_type(+)]]|expected expression"
        "u32 [[ext_vector_type(4, 8)]]|requires one positive integer argument"
        "u32 (*)(in u16) [[abi(1)]]|requires one nonempty string"
        "u32 (*)(in u16) [[clobber()]]|requires string arguments"
        "struct Item [[vendor::annotation]] *|record attributes on a type use"
        "enum E [[underlying(f64)]] { value = 0 }|requires a non-bool integer type")
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 type)
    list(GET fields 1 error)
    string(SHA256 key "${type}")
    string(SUBSTRING "${key}" 0 12 key)
    check(drop_${key} pass "${owner}\nglobal u32 entry() { return drop(${type}); }")
    check(emit_${key} "${error}" "${owner}\nglobal uptr entry() { return emit(${type}); }")
endforeach()
check(opaque_bound pass "${owner}\nglobal u32 entry() { return drop(u32 [[ext_vector_type(never!())]]); }")
check(mismatched_tokens "syntax-match error|mismatched|expected" "${owner}\nglobal u32 entry() { return drop(u32 [[vendor::annotation({ [bad) })]]); }")

# Missing/inferable array bounds are grammar, while the initializer needed to
# finish an object type is a constraint on source that survives its owner.
foreach(case file local local_static local_scalar local_macro file_macro)
    set(category function_def)
    set(declaration "u8 bytes[];")
    set(expected "omitted array bound requires")
    if(case STREQUAL file)
        set(category declaration)
        set(captured "static u8 bytes[];")
    elseif(case STREQUAL file_macro)
        set(category declaration)
        set(captured "static u8 bytes[] = unknown!();")
        set(expected "procedural macro is not visible")
    else()
        if(case STREQUAL local_static)
            set(declaration "static u8 bytes[];")
        elseif(case STREQUAL local_scalar)
            set(declaration "u32 values[] = 1u32;")
        elseif(case STREQUAL local_macro)
            set(declaration "u8 bytes[] = unknown!();")
            set(expected "procedural macro is not visible")
        endif()
        set(captured "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${declaration} } return input; }")
    endif()
    foreach(action discard keep project)
        set(body "return $::quote {};")
        set(diagnostic pass)
        if(action STREQUAL keep)
            set(body "return $::quote { $::unquote($::syntax::node(input, \"value\")) };")
            set(diagnostic "${expected}")
        elseif(action STREQUAL project)
            set(body "return $::meta::tokens($::syntax::node(input, \"value\"));")
            set(diagnostic "${expected}")
        endif()
        check(captured_array_${case}_${action} "${diagnostic}" "
[[syntax_expander]] static $::meta::tokens handle(in $::meta::syntax_match input) { ${body} }
syntax Handle : item { prefix \"handle\"; match value:${category}; expand handle; }
syntax Handle;
handle ${captured}
global u32 entry() { return 9u32; }
")
    endforeach()
endforeach()

# Extract only a use, dropping its captured declaration. Its private binding
# must preserve invalid type semantics through both structured and token output.
set(extraction [=[
static uptr lanes<T, U>() { return 4uptr; }
static bool is_return(in $::meta::syntax node) {
    return $::meta::is_production(node, "statement") &&
        $::meta::is_production($::meta::child($::meta::child(node, 0uptr), 0uptr), "jump_statement");
}
static $::meta::syntax find_return(in $::meta::syntax node) {
    if (is_return(node)) return node;
    for (uptr index = 0uptr; index < $::meta::child_count(node); ++index) {
        $::meta::syntax result = find_return($::meta::child(node, index));
        if (is_return(result)) return result;
    }
    return node;
}
[[syntax_expander]] static $::meta::tokens extract(in $::meta::syntax_match input) {
    $::meta::syntax node = find_return($::syntax::node(input, "function"));
#ifdef PROJECT_CAPTURE
    return $::quote { global uptr entry() { $::unquote($::meta::tokens(node)) } };
#else
    return $::quote { global uptr entry() { $::unquote(node) } };
#endif
}
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) { return $::quote {}; }
syntax Extract : item { prefix "extract"; match function:function_def; expand extract; }
syntax Discard : item { prefix "discard"; match function:function_def; expand discard; }
syntax Extract, Discard;
]=])
foreach(pair
        "typedef bool Bad [[ext_vector_type(4)]];|Bad|vector element type"
        "typedef u32 Bad [[ext_vector_type(0)]];|Bad|requires one positive integer argument"
        "typedef u32 Bad [[ext_vector_type(+)]];|Bad|requires one positive integer argument"
        "typedef u32 Bad [[ext_vector_type(4, 8)]];|Bad|requires one positive integer argument"
        "typedef u32 Bad [[vendor::annotation]];|Bad|not valid on a typedef"
        "typedef u32 [[atomic(1)]] Bad;|Bad|takes no arguments"
        "typedef u32 [[address_space(7)]] Bad;|Bad|requires a pointer type"
        "typedef u32 * [[vendor::annotation]] Bad;|Bad|not valid as a type qualifier"
        "typedef u32 (*Bad)(in u16) [[abi(1)]];|Bad|requires one nonempty string"
        "typedef u32 Bad(in u16 [[atomic(1)]]);|Bad *|takes no arguments"
        "typedef bool [[ext_vector_type(4)]] Bad[2];|Bad|vector element type"
        "typedef bool Bad [[ext_vector_type(4)]]; typedef Bad *Alias;|Alias|vector element type"
        "typedef u32 Bad; typedef u32 Bad [[ext_vector_type(0)]];|Bad|requires one positive integer argument"
        "typedef u32 Bad [[ext_vector_type(0)]]; typedef u32 Bad;|Bad|requires one positive integer argument"
        "typedef u32 Bad [[ext_vector_type(0)]]; typedef Bad Bad; typedef Bad Bad;|Bad|requires one positive integer argument"
        "typedef bool Bad [[ext_vector_type(4)]]; typedef u32 Pack [[ext_vector_type(sizeof(Bad))]];|Pack|vector element type"
        "enum E [[underlying(f64)]] { value = 0 };|enum E|requires a non-bool integer type"
        "enum E [[underlying(i16), underlying(u16)]] { value = 0 }; typedef enum E Bad;|Bad|more than one 'underlying'"
        "enum E [[underlying(f64)]];|enum E|requires a non-bool integer type"
        "typedef enum E [[underlying(f64)]] { value = 0 } Bad;|Bad|requires a non-bool integer type"
        "typedef u16 Good [[ext_vector_type(4)]];|Good|pass"
        "typedef u16 Good [[ext_vector_type(lanes<u16, u32>())]];|Good|pass"
        "typedef u32 Good; typedef Good Bad [[ext_vector_type(0)]];|Good|pass"
        "typedef uptr Good [[vector_size(sizeof(uptr) * 2uptr)]];|Good|pass")
    # Semicolons belong to Cross declarations, not the CMake tuple delimiter.
    string(REPLACE ";" "@SEMICOLON@" escaped "${pair}")
    string(REPLACE "|" ";" fields "${escaped}")
    list(GET fields 0 declaration)
    string(REPLACE "@SEMICOLON@" ";" declaration "${declaration}")
    list(GET fields 1 use)
    list(GET fields 2 error)
    string(SHA256 key "${pair}")
    string(SUBSTRING "${key}" 0 12 key)
    check(discard_binding_${key} pass "${extraction}\ndiscard static uptr original() { ${declaration} return sizeof(${use}); }")
    foreach(output structured projected)
        set(projection)
        if(output STREQUAL projected)
            set(projection -DPROJECT_CAPTURE)
        endif()
        check(extract_${output}_${key} "${error}"
            "${extraction}\nextract static uptr original() { ${declaration} return sizeof(${use}); }" ${projection})
    endforeach()
endforeach()

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(definitions [=[
struct Left { u32 value; };
struct Right { u32 value; };
union Other { u32 value; };
static void take(in struct Right value) { return; }
global void output(out struct Right value);
]=])
function(reject name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${definitions}\n${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls
            ${ARGN}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
           NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
function(reject_body name expected body)
    set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { struct Left left = {3u32}; ${body} } return input; }")
    reject(${name}_unused "${expected}" "${helper}\nglobal u32 entry() { return 7u32; }")
    reject(${name}_called "${expected}" "${helper}\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(7u32); }")
endfunction()

set(nominal "a record value requires the same nominal record type")
reject_body(initializer "${nominal}" "struct Right right = left;")
reject_body(assignment "${nominal}" "struct Right right = {0u32}; right = left;")
reject_body(argument "${nominal}" "take(left);")
reject_body(copy_out "${nominal} in parameter copy-out" "output(left);")
reject_body(record_cast "${nominal}" "struct Right right = (struct Right)left;")
reject_body(scalar_cast "${nominal}" "u32 value = (u32)left;")
reject_body(scalar_to_record "${nominal}" "struct Left value = (struct Left)3u32;")
reject_body(union_copy "${nominal}" "union Other value = left;")
reject_body(conditional "conditional record operands must have the same nominal record type"
    "struct Right right = {5u32}; struct Left value = (bool)1u8 ? left : right;")
reject_body(update "record values do not support update or compound assignment" "++left;")
reject_body(compound "record values do not support update or compound assignment" "left += left;")
reject_body(brace_member "no member|unknown member" "struct Left value = {.missing = 3u32};")
reject_body(brace_record_type "${nominal}" "struct Holder { struct Right item; } value = {left};")
reject_body(brace_duplicate "duplicate destination" "u32 values[2] = {[0uptr] = 1u32, [0uptr] = 2u32};")
reject_body(brace_range "out of range" "u32 values[2] = {[2uptr] = 1u32};")
reject_body(brace_excess "excess entry" "struct Left value = {1u32, 2u32};")
reject_body(brace_union "excess entry in union" "union Other value = {.value = 1u32, .value = 2u32};")
reject_body(brace_scalar "brace initializer requires an aggregate" "u32 value = {1u32};")
reject_body(brace_runtime_index "constant|uninitialized|runtime" "uptr index = 0uptr; u32 values[2] = {[index] = 1u32};")
reject_body(brace_string "string initializer does not fit" "struct Text { u8 bytes[2]; } value = {\"ab\"};")
reject_body(array_scalar "aggregate initializer requires a brace list" "u32 values[2] = 3u32;")
reject_body(array_string_type "aggregate initializer requires a brace list" "u32 values[2] = \"a\";")
reject_body(nested_array_scalar "aggregate initializer requires a brace list" "u32 values[1][2] = {3u32};")
reject_body(brace_designator_type "integer|constant" "u32 values[2] = {[1.5f64] = 1u32};")
foreach(operator "+" "==" "&&")
    string(MD5 key "${operator}")
    reject_body(binary_${key} "built-in operator .* cannot consume record values" "left ${operator} left;")
endforeach()
foreach(operator "-" "!")
    string(MD5 key "${operator}")
    reject_body(unary_${key} "built-in operator .* cannot consume record values" "${operator}left;")
endforeach()
reject(return "${nominal}" [=[
static struct Right helper(in $::meta::tokens input) {
    struct Left value = {3u32};
    return value;
}
global u32 entry() { return 7u32; }
]=])
reject(generic "${nominal}" [=[
static T helper<T>(in T input) {
    if (0u32) { struct Right wrong = input; }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    struct Left value = {3u32};
    struct Left copied = helper(value);
    return input;
}
global u32 entry() { return apply!(7u32); }
]=])
reject(recursive_designator "recursion depth exceeded" [=[
static uptr index() { u8 data[2] = {[index()] = 0u8}; return 0uptr; }
global u32 entry() { return 7u32; }
]=] -feval-depth-limit=8)

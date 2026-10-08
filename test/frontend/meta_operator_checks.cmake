# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
function(reject name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${ARGN}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
           NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
function(reject_body name expected body)
    set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }")
    reject(${name}_unused "${expected}" "${helper}\nglobal u32 entry() { return 9u32; }")
    reject(${name}_called "${expected}" "${helper}\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }")
endfunction()
foreach(operator "%" "&" "|" "^" "<<" ">>")
    string(SHA256 key "${operator}")
    string(SUBSTRING "${key}" 0 10 key)
    reject_body(float_binary_${key} "operator requires integer operands" "1.5f64 ${operator} 1u32;")
    reject_body(float_compound_${key} "operator requires integer operands" "f64 value = 1.5f64; value ${operator}= 1u32;")
    reject_body(float_vector_${key} "operator requires integer operands"
        "typedef f32 Lanes [[ext_vector_type(4)]]; Lanes value = 1.5f32; value ${operator}= 1u32;")
endforeach()
reject_body(float_complement "operator requires integer operands" "~1.5f64;")
reject_body(pointer_negate "scalar operator requires numeric operands" "u32 *p = (u32 *)0uptr; -p;")
reject_body(pointer_plus "scalar operator requires numeric operands" "u32 *p = (u32 *)0uptr; +p;")
reject_body(pointer_multiply "scalar operator requires numeric operands" "u32 *p = (u32 *)0uptr; p * 2u32;")
reject_body(pointer_float "pointer arithmetic requires one pointer and one integer operand" "u32 *p = (u32 *)0uptr; p + 1.5f64;")
reject_body(integer_minus_pointer "pointer arithmetic requires one pointer and one integer operand" "u32 *p = (u32 *)0uptr; 1uptr - p;")
reject_body(pointer_pair "pointer subtraction requires matching pointer types" "u32 *p = (u32 *)0uptr; p + p;")
reject_body(pointer_compound_pair "pointer subtraction requires matching pointer types" "u32 *p = (u32 *)0uptr; p -= p;")
reject_body(pointer_mismatch "pointer subtraction requires matching pointer types" "u32 *p; u16 *q; p - q;")
reject_body(pointer_void "pointer arithmetic requires a complete pointed-to object type" "void *p = (void *)0uptr; p += 1u32;")
reject_body(pointer_incomplete "pointer arithmetic requires a complete pointed-to object type" "struct Missing; struct Missing *p; ++p;")
reject_body(pointer_function "pointer arithmetic requires a complete pointed-to object type" "u32 (*p)(); p++;")
reject_body(pointer_array "pointer arithmetic requires a complete pointed-to object type" "u32 (*p)[]; p + 1u32;")
reject_body(vector_shape "vector operator requires matching lane counts"
    "typedef u32 A [[ext_vector_type(4)]]; typedef u32 B [[ext_vector_type(2)]]; A a = 1u32; B b = 2u32; a + b;")
reject_body(label_update "scalar operator requires numeric operands" "label value = (label)0uptr; ++value;")
reject_body(unevaluated "operator requires integer operands" "sizeof(1.5f64 & 1u32);")
reject(required_unevaluated "operator requires integer operands"
    "global uptr value = sizeof(1.5f64 & 1u32); global u32 entry() { return 9u32; }")
reject(required_short_circuit "operator requires integer operands"
    "global bool value = 0u32 && (1.5f64 & 1u32); global u32 entry() { return 9u32; }")
reject(eval_only_unused "operator requires integer operands"
    "[[eval_only]] static u32 helper() { if (0u32) 1.5f64 << 1u32; return 9u32; }\nglobal u32 entry() { return 9u32; }")
reject(ordinary_unevaluated "operator requires integer operands"
    "global uptr entry() { return sizeof(1.5f64 ^ 1u32); }")
reject(generic "operator requires integer operands" [=[
static T wrong<T>(in T value) { if (0u32) return value & 1u32; return value; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { wrong(1.5f64); return input; }
global u32 entry() { return apply!(9u32); }
]=])
reject(direct_macro "operator requires integer operands" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { if (0u32) ~1.5f64; return input; }
global u32 entry() { return apply!(9u32); }
]=])
reject(direct_expander "operator requires integer operands" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) { f64 value = 1.5f64; value <<= 1u32; }
    return $::quote { 9u32 };
}
syntax Form : expression { prefix "form"; match "(" ")"; expand expand; }
syntax Form;
global u32 entry() { return form (); }
]=])
reject(pointer_mips "pointer arithmetic requires a complete pointed-to object type"
    "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { void *p; ++p; } return input; }\nglobal u32 entry() { return 9u32; }"
    -mprofile=mips64-n64)

# ABI aliases are model entities, not distinct callable types. Exercise both
# ordinary lowering and a reached meta helper, including a user model alias.
foreach(model shipped custom)
    set(flags)
    if(model STREQUAL custom)
        set(canonical test_sysv)
        set(alias test_abi)
        list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
    else()
        set(canonical cross)
        set(alias cross_abi)
    endif()
    set(source "typedef u32 (*Direct)() [[abi(\"${canonical}\")]];\ntypedef u32 (*Alias)() [[abi(\"${alias}\")]];\nglobal iptr subtract(in Direct *first, in Alias *second) { return first - second; }\nstatic $::meta::tokens helper(in $::meta::tokens input) { Direct values[2]; Alias *alias = (Alias *)values; if (values - alias != 0iptr) return $::quote { 0u32 }; return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\n$::static_assert(apply!(9u32) == 9u32, \"callable ABI alias\");\n")
    file(WRITE "${OUTPUT}/abi_alias_${model}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
            "${OUTPUT}/abi_alias_${model}.x" -o "${OUTPUT}/abi_alias_${model}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${model} callable ABI alias was rejected\n${out}\n${err}")
        endif()
    endforeach()
endforeach()
reject(unknown_callback_alias "unknown callable ABI 'missing_abi'"
    "typedef u32 (*Unknown)() [[abi(\"missing_abi\")]]; global u32 entry() { return 9u32; }")

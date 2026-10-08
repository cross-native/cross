# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(definitions [=[
typedef u32 Words [[ext_vector_type(4)]];
typedef u32 Pair [[ext_vector_type(2)]];
typedef u32 Scalable [[scalable_vector(4)]];
static void nothing() {}
]=])
function(reject name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${definitions}\n${source}")
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
foreach(operator "==" "!=")
    if(operator STREQUAL "==")
        set(key equal)
    else()
        set(key unequal)
    endif()
    reject_body(pointer_nonzero_${key} "pointer/integer equality requires an integer constant zero"
        "u32 *p; p ${operator} 1u32;")
    reject_body(nonzero_pointer_${key} "pointer/integer equality requires an integer constant zero"
        "u32 *p; 1u32 ${operator} p;")
    reject_body(pointer_runtime_zero_${key} "pointer/integer equality requires an integer constant zero"
        "u32 *p; u32 zero = 0u32; p ${operator} zero;")
    reject_body(pointer_unevaluated_zero_${key} "pointer/integer equality requires an integer constant zero"
        "u32 *p; u32 zero = 0u32; sizeof(zero ${operator} p);")
    reject(required_null_${key} "pointer/integer equality requires an integer constant zero"
        "global uptr value = sizeof((u32 *)0uptr ${operator} 1u32);")
    reject(runtime_null_${key} "pointer/integer equality requires an integer constant zero"
        "global bool entry(in u32 *p, in u32 zero) { return p ${operator} zero; }")
endforeach()
reject_body(pointer_integer_order "ordered pointer comparison requires two pointer operands"
    "u32 *p; p < 0;")
foreach(operator "==" "!=" "<" "<=" ">" ">=")
    string(SHA256 key "${operator}")
    string(SUBSTRING "${key}" 0 10 key)
    reject_body(void_${key} "comparison requires numeric or pointer operands" "nothing() ${operator} 1u32;")
    reject_body(pointer_float_${key} "comparison cannot mix pointer and floating types" "u32 *p; p ${operator} 1.5f64;")
    reject_body(float_pointer_${key} "comparison cannot mix pointer and floating types" "u32 *p; 1.5f64 ${operator} p;")
    reject_body(vector_shape_${key} "vector operator requires matching lane counts" "Words a; Pair b; a ${operator} b;")
    reject_body(vector_scalable_${key} "vector operator requires matching lane counts" "Words a; Scalable b; a ${operator} b;")
    reject_body(vector_pointer_${key} "vector comparison requires numeric operands" "Words a; u32 *p; a ${operator} p;")
    reject_body(label_integer_${key} "label comparisons require == or != between two label operands" "label a; a ${operator} 0uptr;")
endforeach()
foreach(operator "<" "<=" ">" ">=")
    string(SHA256 key "${operator}")
    string(SUBSTRING "${key}" 0 10 key)
    reject_body(label_order_${key} "label comparisons require == or != between two label operands" "label a; label b; a ${operator} b;")
endforeach()
foreach(operator "&&" "||")
    string(SHA256 key "${operator}")
    string(SUBSTRING "${key}" 0 10 key)
    reject_body(logical_void_${key} "logical operator requires scalar operands" "0u32 ${operator} nothing();")
    reject_body(logical_vector_${key} "logical operator requires scalar operands" "Words a; a ${operator} 1u32;")
endforeach()
reject_body(not_void "logical operator requires scalar operands" "!nothing();")
reject_body(conditional_void "conditional operands must both be void or both produce values" "1u32 ? 1u32 : nothing();")
reject_body(conditional_pointer_float "conditional operands cannot mix pointer and floating types" "u32 *p; 1u32 ? p : 1.5f64;")
reject_body(conditional_float_pointer "conditional operands cannot mix pointer and floating types" "u32 *p; 1u32 ? 1.5f64 : p;")
reject_body(conditional_shape "conditional vector operands require matching lane counts" "Words a; Pair b; 1u32 ? a : b;")
reject_body(conditional_scalable "conditional vector operands require matching lane counts" "Words a; Scalable b; 1u32 ? a : b;")
reject_body(conditional_pointer_vector "conditional vector operands require numeric elements or scalar operands" "Words a; u32 *p; 1u32 ? p : a;")
reject_body(conditional_label "conditional label operands must both have label type" "label a; 1u32 ? a : 0uptr;")
reject_body(conditional_selector "conditional selector must be scalar" "Words a; a ? 1u32 : 2u32;")
reject_body(conditional_array_bound "conditional pointer operands have no compatible common type"
    "u32 a[3]; u32 b[4]; 1u32 ? &a : &b;")
reject_body(conditional_array_bound_reverse "conditional pointer operands have no compatible common type"
    "u32 a[3]; u32 b[4]; 0u32 ? &b : &a;")
reject_body(conditional_unrelated "conditional pointer operands have no compatible common type"
    "u32 *a; i32 *b; 1u32 ? a : b;")
reject_body(conditional_nested_void "conditional pointer operands have no compatible common type"
    "u32 **a; void **b; 1u32 ? a : b;")
reject_body(conditional_nonzero "conditional pointer/integer operands require an integer constant zero"
    "u32 *a; 1u32 ? a : 1u32;")
reject_body(conditional_runtime_zero "conditional pointer/integer operands require an integer constant zero"
    "u32 *a; u32 zero = 0u32; sizeof(1u32 ? zero : a);")
reject(conditional_callable_abi "conditional pointer operands have no compatible common type" [=[
typedef u32 (*A)(in u32) [[abi("odd_abi")]];
typedef u32 (*B)(in u32) [[abi("stack_result_abi")]];
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) { A a; B b; 1u32 ? a : b; } return input;
}
global u32 entry() { return 9u32; }
]=] "--model=${MODEL}")
reject(conditional_deferred_bound "conditional pointer operands have no compatible common type" [=[
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    u32 a[] = { [$::eval(count($::quote {one two}))] = 7u32 };
    u32 b[4];
    if (0u32) { u32 (*p)[3] = 1u32 ? &a : &b; }
    return $::quote {17u32};
}
$::static_assert(apply!() == 17u32, "pointer join constraint");
]=])
reject_body(unevaluated "comparison cannot mix pointer and floating types" "u32 *p; sizeof(p == 1.5f64);")
reject(required_unevaluated "comparison requires numeric or pointer operands"
    "global uptr value = sizeof(nothing() == 1u32); global u32 entry() { return 9u32; }")
reject(required_conditional "conditional operands cannot mix pointer and floating types"
    "global uptr value = sizeof(1u32 ? (u32 *)0uptr : 1.5f64); global u32 entry() { return 9u32; }")
reject(required_short_circuit "logical operator requires scalar operands"
    "global bool value = 0u32 && nothing(); global u32 entry() { return 9u32; }")
reject(ordinary_unevaluated "comparison cannot mix pointer and floating types"
    "global uptr entry(in u32 *p) { return sizeof(p != 1.5f64); }")
reject(generic "comparison cannot mix pointer and floating types" [=[
static bool wrong<T>(in T value) { if (0u32) return value == 1.5f64; return 1u32; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { u32 local; wrong(&local); return input; }
global u32 entry() { return apply!(9u32); }
]=])
reject(direct_macro "conditional operands cannot mix pointer and floating types" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    if (0u32) { u32 *p; 1u32 ? p : 1.5f64; }
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(direct_expander "vector operator requires matching lane counts" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) { Words a; Pair b; a == b; }
    return $::quote { 9u32 };
}
syntax Form : expression { prefix "form"; match "(" ")"; expand expand; }
syntax Form;
global u32 entry() { return form (); }
]=])
foreach(profile custom mips32 mips64_be mips64_le)
    if(profile STREQUAL custom)
        set(flags "--model=${MODEL}" -mabi=odd_abi)
    elseif(profile STREQUAL mips32)
        set(flags -mprofile=r3000-o32)
    elseif(profile STREQUAL mips64_be)
        set(flags -mprofile=mips64-n64)
    else()
        set(flags -mprofile=mips64el-n64)
    endif()
    reject(profile_${profile} "comparison cannot mix pointer and floating types"
        "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { u32 *p; p == 1.5f64; } return input; }\nglobal u32 entry() { return 9u32; }"
        ${flags})
endforeach()

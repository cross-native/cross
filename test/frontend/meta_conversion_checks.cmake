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
global void take(in u32 *value);
global void output(out const u32 *value);
global void floating_output(out f64 value);
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
set(pointer "implicit pointer conversion discards qualifiers or uses incompatible pointee types")
set(floating "conversion cannot convert between a pointer and a floating type")
set(cast_floating "explicit cast cannot convert between a pointer and a non-integer type")
set(deep_types "typedef u32 DeepMutable0; typedef const u32 DeepQualified0; typedef const u32 DeepUnsafe0; typedef u16 DeepOther0;\n")
foreach(index RANGE 0 39)
    math(EXPR next "${index} + 1")
    string(APPEND deep_types "typedef DeepMutable${index} *DeepMutable${next}; typedef DeepUnsafe${index} *DeepUnsafe${next}; typedef DeepOther${index} *DeepOther${next};\n")
    if(index EQUAL 39)
        string(APPEND deep_types "typedef DeepQualified${index} *DeepQualified${next};\n")
    else()
        string(APPEND deep_types "typedef DeepQualified${index} *const DeepQualified${next};\n")
    endif()
endforeach()
# Removing an implementation depth cutoff must not weaken leaf compatibility
# or the protected intermediate cells required by nested qualification.
reject_body(deep_const_loss "${pointer}" "${deep_types} DeepQualified40 source; DeepMutable40 destination = source;")
reject_body(deep_nested_const "${pointer}" "${deep_types} DeepMutable40 source; DeepUnsafe40 destination = source;")
reject_body(deep_pointee "${pointer}" "${deep_types} DeepMutable40 source; DeepOther40 destination = source;")
reject_body(const_initializer "${pointer}" "const u32 value = 1u32; u32 *p = &value;")
reject_body(volatile_assignment "${pointer}" "volatile u32 *source; u32 *destination; destination = source;")
reject_body(atomic_initializer "${pointer}" "u32 [[atomic]] *source; u32 *destination = source;")
reject_body(nested_const "${pointer}" "u32 **source; const u32 **destination = source;")
reject_body(nested_volatile "${pointer}" "u32 **source; volatile u32 **destination = source;")
reject_body(nominal "${pointer}" "struct A { u32 value; }; struct B { u32 value; }; struct A *a; struct B *b = a;")
reject_body(pointee "${pointer}" "u32 *source; u16 *destination = source;")
reject_body(argument "${pointer}" "const u32 *source; take(source);")
reject_body(copy_out "${pointer} in parameter copy-out" "u32 *destination; output(destination);")
reject_body(brace_leaf "${pointer}" "const u32 *source; struct Holder { u32 *pointer; } value = {source};")
reject_body(pointer_to_float "${floating}" "u32 *pointer; f64 number = pointer;")
reject_body(float_to_pointer "${floating}" "u32 *pointer = 1.5f64;")
reject_body(cast_to_float "${cast_floating}" "u32 *pointer; (f64)pointer;")
reject_body(cast_to_pointer "${cast_floating}" "(u32 *)1.5f64;")
reject_body(float_copy_out "${floating} in parameter copy-out" "u32 *destination; floating_output(destination);")
reject_body(vector_shape "vector conversion requires matching lane counts" "Words source = 1u32; Pair destination = source;")
reject_body(vector_to_scalar "vector value cannot convert to a non-vector type" "Words source = 1u32; u32 destination = source;")
reject_body(pointer_to_vector "vector conversion requires numeric scalar or vector operands" "u32 *source; Words destination = source;")
reject_body(vector_assignment "vector conversion requires matching lane counts" "Words source = 1u32; Pair destination = 2u32; destination = source;")
reject_body(vector_cast "vector value cannot convert to a non-vector type" "Words source = 1u32; (u32)source;")
reject_body(unevaluated "${cast_floating}" "sizeof((u32 *)1.5f64);")
reject(return_pointer "${pointer} in return" "static u32 *helper(in $::meta::tokens input) { const u32 *pointer; return pointer; }\nglobal u32 entry() { return 9u32; }")
reject(return_float "${floating} in return" "static f64 helper(in $::meta::tokens input) { u32 *pointer; return pointer; }\nglobal u32 entry() { return 9u32; }")
reject(eval_only "${pointer}" "[[eval_only]] static u32 helper() { const u32 *source; u32 *destination = source; return 9u32; }\nglobal u32 entry() { return 9u32; }")
reject(required_unevaluated "${cast_floating}" "global uptr size = sizeof((u32 *)1.5f64);")
reject(required_short_circuit "${cast_floating}" "global bool value = 0u32 && (bool)((u32 *)1.5f64);")
reject(direct_macro "${pointer}" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    if (0u32) { const u32 *source; u32 *destination = source; }
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(direct_expander "${pointer}" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) { const u32 *source; u32 *destination = source; }
    return $::quote { 9u32 };
}
syntax Form : expression { prefix "form"; match "(" ")"; expand expand; }
syntax Form;
global u32 entry() { return form (); }
]=])
reject(generic "${pointer}" [=[
static T *helper<T>(in const T *source) { if (0u32) { T *destination = source; } return (T *)0uptr; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { u32 value = 1u32; helper(&value); return input; }
global u32 entry() { return apply!(9u32); }
]=])
reject(custom_model "${pointer}" "static $::meta::tokens helper(in $::meta::tokens input) { const u32 *source; u32 *destination = source; return input; }\nglobal u32 entry() { return 9u32; }"
    "--model=${MODEL}" -mabi=odd_abi)
reject(mips "${floating}" "static $::meta::tokens helper(in $::meta::tokens input) { u32 *pointer = 1.5f64; return input; }\nglobal u32 entry() { return 9u32; }"
    -mprofile=mips64-n64)

# Nested qualification permits read-only pointer-value conversion, not pointer
# type punning or writing through the protected intermediate pointer cell.
foreach(pointee "const void" "const u16")
    string(MD5 key "${pointee}")
    reject(pointer_cell_alias_${key} "meta pointer read violates aggregate effective type"
        "static $::meta::tokens helper(in $::meta::tokens input) { u32 value = 9u32; u32 *pointer = &value; ${pointee} *const *view = (${pointee} *const *)&pointer; ${pointee} *copied = *view; return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }")
endforeach()
reject_body(qualified_cell_write "cannot write a const subobject"
    "u32 value = 9u32; u32 *pointer = &value; const u32 *const *view = &pointer; *view = &value;")

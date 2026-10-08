# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
set(directory "${OUTPUT}.cases")
file(MAKE_DIRECTORY "${directory}")

function(compile case source)
    file(WRITE "${directory}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S -fno-eval-calls ${ARGN}
        "${directory}/${case}.x" -o "${directory}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    set(result "${result}" PARENT_SCOPE)
    set(err "${out}${err}" PARENT_SCOPE)
endfunction()

function(reject case expected source)
    compile("${case}" "${source}" ${ARGN})
    if(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+")
        message(FATAL_ERROR "${case}: missing located diagnostic '${expected}'\n${err}")
    endif()
endfunction()

foreach(level O0 O2)
    foreach(attribute IN ITEMS always_inline cold eval_only hot may_alias musttail naked
            no_stack_protector noinit noinline noreturn packed raw_inline retain returns_twice
            runtime_only thread_local used weak)
        reject(attribute_arguments_${attribute}_${level} "${attribute} does not take arguments"
            "[[${attribute}(123u32)]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }\nglobal u32 entry() { return 9u32; }"
            -${level})
        reject(attribute_arguments_runtime_${attribute}_${level} "${attribute} does not take arguments"
            "[[${attribute}(123u32)]] static u32 helper(in u32 input) { return input; }\nglobal u32 entry() { return 9u32; }"
            -${level})
    endforeach()
    foreach(attribute IN ITEMS cold eval_only noinline packed used)
        reject(attribute_arguments_called_${attribute}_${level} "${attribute} does not take arguments"
            "static $::meta::tokens helper(in $::meta::tokens input) [[${attribute}(123u32)]] { return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }"
            -${level})
        reject(attribute_arguments_generic_${attribute}_${level} "${attribute} does not take arguments"
            "[[${attribute}(123u32)]] static T helper<T>(in T input) { return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }"
            -${level})
    endforeach()
    foreach(used unused called)
        set(void_helper "static void helper(in $::meta::tokens input) { if (0u32) return 3u32; return; }")
        set(value_helper "static void empty() { return; }\nstatic u32 helper(in $::meta::tokens input) { if (0u32) return empty(); return 9u32; }")
        if(used STREQUAL "called")
            set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { helper(input); return input; }\nglobal u32 entry() { return apply!(9u32); }")
        else()
            set(suffix "global u32 entry() { return 9u32; }")
        endif()
        reject(return_void_value_${used}_${level} "void function cannot return a value"
            "${void_helper}\n${suffix}" -${level})
        reject(return_value_void_${used}_${level} "non-void function cannot return a void expression"
            "${value_helper}\n${suffix}" -${level})
    endforeach()
    foreach(control break continue case default duplicate_default switch_continue after_loop after_switch)
        if(control STREQUAL "break")
            set(body "break;")
            set(expected "break has no enclosing loop or switch")
        elseif(control STREQUAL "continue")
            set(body "continue;")
            set(expected "continue has no enclosing loop")
        elseif(control STREQUAL "case")
            set(body "case 1u32: ;")
            set(expected "case/default has no enclosing switch|case label is not inside a switch")
        elseif(control STREQUAL "default")
            set(body "default: ;")
            set(expected "case/default has no enclosing switch|default label is not inside a switch")
        elseif(control STREQUAL "duplicate_default")
            set(body "switch (0u32) { default: ; if (0u32) { default: ; } }")
            set(expected "duplicate default label in switch")
        elseif(control STREQUAL "switch_continue")
            set(body "switch (0u32) { default: continue; }")
            set(expected "continue has no enclosing loop")
        elseif(control STREQUAL "after_loop")
            set(body "while (0u32) { break; } continue;")
            set(expected "continue has no enclosing loop")
        else()
            set(body "switch (0u32) { default: break; } break;")
            set(expected "break has no enclosing loop or switch")
        endif()
        set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }")
        reject(control_unused_${control}_${level} "${expected}"
            "${helper}\nglobal u32 entry() { return 9u32; }" -${level})
        reject(control_called_${control}_${level} "${expected}"
            "${helper}\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }" -${level})
    endforeach()
    reject(control_eval_only_${level} "break has no enclosing loop or switch" [=[
[[eval_only]] static u32 helper() { if (0u32) break; return 9u32; }
global u32 entry() { return 9u32; }
]=] -${level})
    reject(control_generic_${level} "continue has no enclosing loop" [=[
static T helper<T>(in T input) { if (0u32) continue; return input; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=] -${level})
    reject(control_macro_case_${level} "case/default has no enclosing switch|case label is not inside a switch" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { if (0u32) { case 1u32: ; } return input; }
global u32 entry() { return apply!(9u32); }
]=] -${level})
    reject(control_expander_default_${level} "case/default has no enclosing switch|default label is not inside a switch" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) { default: ; }
    return $::quote { 9u32 };
}
syntax Form : expression { prefix "form"; match body:paren; expand expand; }
syntax Form;
global u32 entry() { return form (); }
]=] -${level})
    reject(meta_untaken_label_owner_${level} "cannot take a code label address of a translation-only function" [=[
static label unavailable(in $::meta::tokens input) {
    if (0u32) return point;
    return (label)0uptr;
    point: ;
}
[[macro]] static $::meta::tokens invalid(in $::meta::tokens input) {
    label address = unavailable(input);
    return $::quote { 0u32 };
}
global u32 entry() { return invalid!(); }
]=] -${level})
    reject(meta_unused_label_owner_${level} "cannot take a code label address of a translation-only function" [=[
static label unavailable(in $::meta::tokens input) {
    return (label)0uptr;
    label values[1] = {point};
    point: ;
}
global u32 entry() { return 1u32; }
]=] -${level})
    reject(generic_meta_untaken_label_owner_${level} "cannot take a code label address of a translation-only function" [=[
[[generic(T)]] static label unavailable(in T input) {
    if (0u32) return point;
    return (label)0uptr;
    point: ;
}
[[macro]] static $::meta::tokens invalid(in $::meta::tokens input) {
    label address = unavailable(input);
    return $::quote { 0u32 };
}
global u32 entry() { return invalid!(); }
]=] -${level})
    reject(generic_meta_label_owner_${level} "cannot take a code label address of a translation-only function" [=[
[[generic(T)]] static label unavailable(in T input) {
    return point;
    point: ;
}
[[macro]] static $::meta::tokens invalid(in $::meta::tokens input) {
    label address = unavailable(input);
    return $::quote { 0u32 };
}
global u32 entry() { return invalid!(); }
]=] -${level})
    reject(meta_label_owner_${level} "cannot take a code label address of a translation-only function" [=[
static label unavailable(in $::meta::tokens input) {
    label values[1] = {point};
    return values[0];
    point: ;
}
[[macro]] static $::meta::tokens invalid(in $::meta::tokens input) {
    label address = unavailable(input);
    return $::quote { 0u32 };
}
global u32 entry() { return invalid!(); }
]=] -${level})
    reject(label_freeze_${level} "opaque code label representation cannot be frozen as bytes" [=[
static void owner() { point: ; }
[[macro]] static $::meta::tokens invalid(in $::meta::tokens input) {
    $::meta::buffer storage = $::meta::alloc(sizeof(label));
    *((label *)$::meta::data(storage)) = owner::point;
    $::meta::bytes bytes = $::meta::freeze(storage, sizeof(label));
    return $::quote { 0u32 };
}
global u32 entry() { return invalid!(); }
]=] -${level})
endforeach()

foreach(type tokens syntax syntax_match span context bytes buffer)
    reject(${type}_global "meta values cannot have runtime object storage"
        "$::meta::${type} invalid; global u32 entry() { return 1u32; }")
    reject(${type}_record "meta values cannot be record members|record member has an incomplete or non-object type"
        "struct Invalid { $::meta::${type} value; }; global u32 entry() { return 1u32; }")
    reject(${type}_linkage "meta type in its signature must be static"
        "$::meta::${type} invalid(in $::meta::${type} value) { return value; }")
    reject(${type}_out "eval_only parameters must use 'in'"
        "static void invalid(out $::meta::${type} value) { return; }")
    reject(${type}_runtime "cannot be both eval_only and runtime_only"
        "[[runtime_only]] static $::meta::${type} invalid(in $::meta::${type} value) { return value; }")
    reject(${type}_nested "meta types cannot be nested in runtime function types"
        "static $::meta::${type} *invalid(in $::meta::${type} *value) { return value; }")
    reject(${type}_runtime_local "meta values cannot have runtime local storage"
        "global u32 entry() { $::meta::${type} value; return 1u32; }")
    reject(${type}_expander_pointer "meta values cannot have runtime local storage"
        "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { $::meta::${type} *invalid; return input; }\nglobal u32 entry() { return apply!(9u32); }")
endforeach()

foreach(type tokens syntax syntax_match span context bytes buffer)
    reject(${type}_untaken_return "incompatible meta value in return"
        "static $::meta::${type} helper(in $::meta::${type} input) { if ((bool)0u8) return 7u32; return input; }\nglobal u32 entry() { return 1u32; }")
    reject(${type}_untaken_initializer "incompatible meta value in local initializer"
        "static $::meta::${type} helper(in $::meta::${type} input) { if ((bool)0u8) { u32 invalid = input; } return input; }\nglobal u32 entry() { return 1u32; }")
    reject(${type}_untaken_assignment "incompatible meta value in assignment"
        "static $::meta::${type} helper(in $::meta::${type} input) { if ((bool)0u8) { input = 7u32; } return input; }\nglobal u32 entry() { return 1u32; }")
    reject(${type}_untaken_operator "opaque meta values do not support unary"
        "static $::meta::${type} helper(in $::meta::${type} input) { if ((bool)0u8) { &input; } return input; }\nglobal u32 entry() { return 1u32; }")
    reject(${type}_untaken_callee "opaque meta values are not callable"
        "static $::meta::${type} helper(in $::meta::${type} input) { if ((bool)0u8) input(); return input; }\nglobal u32 entry() { return 1u32; }")
endforeach()
reject(helper_untaken_call "incompatible meta value in call argument" [=[
static void consume(in u32 value) { return; }
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) consume(input);
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_intrinsic "incompatible argument type for translation-only operation" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) { uptr invalid = $::meta::len(7u32); }
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_quote "unquote requires a token value or syntax node" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) return $::quote { $::unquote(7u32) };
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_variadic "meta values cannot enter variadic arguments" [=[
static void consume(in u32 value, ...);
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) consume(1u32, input);
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_arity "meta helper call has an invalid argument count" [=[
static $::meta::tokens consume(in $::meta::tokens value) { return value; }
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) return consume();
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_generic_untaken_return "incompatible meta value in return" [=[
static T helper<T>(in T input) {
    if ((bool)0u8) return 7u32;
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
reject(helper_untaken_condition "conditional meta selector must be scalar" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    struct Condition { u32 x; } invalid = {1u32};
    if ((bool)0u8) return invalid ? input : input;
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_const "cannot write a const cell" [=[
static $::meta::tokens helper(in const $::meta::tokens input) {
    if ((bool)0u8) input = $::quote { 7u32 };
    return input;
}
global u32 entry() { return 1u32; }
]=])

foreach(expression IN ITEMS "1u32 = 2u32" "++1u32" "((u32)1u32) = 2u32"
        "((bool)1u8 ? 1u32 : 2u32) = 3u32" "value() = 2u32" "value = value"
        "Enumeration = 2u32")
    string(MD5 key "${expression}")
    reject(helper_untaken_lvalue_${key} "assignment or update requires an object lvalue"
        "static u32 value() { return 1u32; } enum E { Enumeration };\nstatic $::meta::tokens helper(in $::meta::tokens input) { if ((bool)0u8) { ${expression}; } return input; }\nglobal u32 entry() { return 1u32; }")
endforeach()
reject(helper_untaken_void_target "dereference requires a pointer to an object or function type"
    "static $::meta::tokens helper(in $::meta::tokens input) {
        if ((bool)0u8) { *((void *)0uptr) = 2u32; } return input;
    } global u32 entry() { return 1u32; }")
foreach(body IN ITEMS
        "const u32 [[ext_vector_type(4)]] value = 1u32; value[0uptr] = 2u32;"
        "typedef u32 Vector [[ext_vector_type(4)]]; const Vector value = 1u32; ++value[0uptr];"
        "typedef u32 Vector [[ext_vector_type(4)]]; const Vector *pointer = (const Vector *)0uptr; (*pointer)[0uptr] += 1u32;"
        "typedef u32 Row[2]; const Row value = {1u32, 2u32}; value[0uptr] = 3u32;"
        "struct R { u32 values[2]; }; const struct R value = {{1u32, 2u32}}; value.values[0uptr] = 3u32;"
        "struct R { u32 value; }; const struct R *pointer = (const struct R *)0uptr; pointer->value += 1u32;")
    string(MD5 key "${body}")
    reject(helper_untaken_const_subobject_${key} "cannot write a const"
        "static $::meta::tokens helper(in $::meta::tokens input) { if ((bool)0u8) { ${body} } return input; }\nglobal u32 entry() { return 1u32; }")
endforeach()
reject(helper_untaken_temporary_member "record value is not an object designator" [=[
struct R { u32 value; };
static struct R object() { struct R value = {1u32}; return value; }
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) object().value = 2u32;
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_generic_const_lane "cannot write a const" [=[
static $::meta::tokens helper<T>(in T input) {
    if ((bool)0u8) { const T value = input; value[0uptr] = 2u32; }
    return $::quote { 7u32 };
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    u32 [[ext_vector_type(4)]] value = 1u32;
    return helper(value);
}
global u32 entry() { return apply!(); }
]=])

foreach(declaration IN ITEMS "void invalid;" "void invalid[2];" "u32 invalid(in u32 value);")
    string(MD5 key "${declaration}")
    reject(helper_untaken_nonobject_${key} "requires an object type|array element type cannot be void"
        "static $::meta::tokens helper(in $::meta::tokens input) { if ((bool)0u8) { ${declaration} } return input; }\nglobal u32 entry() { return 1u32; }")
endforeach()
set(designator_prefix [=[
struct Record { u32 value; };
struct Incomplete;
static $::meta::tokens helper(in $::meta::tokens input) {
    struct Record object = {1u32};
    struct Incomplete *unknown = (struct Incomplete *)0uptr;
    u32 scalar = 1u32;
    u32 array[2] = {1u32, 2u32};
    if ((bool)0u8) {
]=])
set(designator_suffix "} return input; }\nglobal u32 entry() { return 1u32; }")
foreach(expression IN ITEMS "&7u32" "&(scalar + 1u32)" "&((u32)scalar)"
        "&(scalar ? scalar : scalar)")
    string(MD5 key "${expression}")
    reject(helper_untaken_address_${key} "address-of requires an object or function lvalue"
        "${designator_prefix}${expression};${designator_suffix}")
endforeach()
reject(helper_untaken_void_address "dereference requires a pointer to an object or function type"
    "${designator_prefix}&*((void *)0uptr);${designator_suffix}")
foreach(expression IN ITEMS "&value()" "&Enumeration" "&make().field")
    string(MD5 key "${expression}")
    reject(helper_untaken_value_address_${key} "address-of requires an object or function lvalue|record value is not an object designator"
        "struct Record { u32 field; }; static struct Record make() { struct Record result = {1u32}; return result; } static u32 value() { return 1u32; } enum E { Enumeration };\nstatic $::meta::tokens helper(in $::meta::tokens input) { if ((bool)0u8) { ${expression}; } return input; }\nglobal u32 entry() { return 1u32; }")
endforeach()
foreach(expression IN ITEMS "&object.field" "&(object.field)" "&pointer->field" "&make().field")
    string(MD5 key "${expression}")
    reject(helper_untaken_bitfield_address_${key} "cannot take the address of a bit-field"
        "struct Bits { u32 field : 3; }; static struct Bits make() { struct Bits result = {1u32}; return result; }\nstatic $::meta::tokens helper(in $::meta::tokens input) { struct Bits object = {1u32}; struct Bits *pointer = &object; if ((bool)0u8) { ${expression}; } return input; }\nglobal u32 entry() { return 1u32; }")
endforeach()
reject(helper_generic_bitfield_address "cannot take the address of a bit-field" [=[
struct Bits { u32 field : 3; };
static $::meta::tokens helper<T>(in T object, in $::meta::tokens input) {
    if ((bool)0u8) &object.field;
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    struct Bits object = {1u32}; return helper(object, input);
}
global u32 entry() { return apply!(7u32); }
]=])
foreach(expression IN ITEMS "&7u32" "&object.field")
    string(MD5 key "${expression}")
    set(expected "address-of requires an object or function lvalue|cannot take the address of a bit-field")
    reject(macro_untaken_address_${key} "${expected}"
        "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { struct Bits { u32 field : 3; } object = {1u32}; if ((bool)0u8) ${expression}; return input; }\nglobal u32 entry() { return apply!(7u32); }")
    reject(syntax_untaken_address_${key} "${expected}"
        "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { struct Bits { u32 field : 3; } object = {1u32}; if ((bool)0u8) ${expression}; return $::quote { 7u32 }; }\nsyntax Apply : expression { prefix \"apply\"; match \"(\" \")\"; expand expand; }\nsyntax Apply;\nglobal u32 entry() { return apply(); }")
endforeach()
reject(required_untaken_address "address-of requires an object or function lvalue"
    "global u32 entry() { return $::eval((bool)0u8 ? (u32)(uptr)&7u32 : 1u32); }")
reject(required_untaken_bitfield_address "cannot take the address of a bit-field"
    "struct Bits { u32 field : 3; }; static struct Bits object = {1u32}; global u32 entry() { return $::eval((bool)0u8 ? (u32)(uptr)&object.field : 1u32); }")
foreach(expression IN ITEMS "object.missing" "(&object)->missing")
    string(MD5 key "${expression}")
    reject(helper_untaken_member_${key} "has no member named 'missing'"
        "${designator_prefix}${expression};${designator_suffix}")
endforeach()
foreach(expression IN ITEMS "scalar->value" "object->value")
    string(MD5 key "${expression}")
    reject(helper_untaken_arrow_${key} "pointer member access requires a pointer to a record"
        "${designator_prefix}${expression};${designator_suffix}")
endforeach()
foreach(expression IN ITEMS "scalar.value" "(&scalar)->value")
    string(MD5 key "${expression}")
    reject(helper_untaken_record_${key} "member access requires a record object"
        "${designator_prefix}${expression};${designator_suffix}")
endforeach()
reject(helper_untaken_incomplete_member "member access requires a complete record type"
    "${designator_prefix}unknown->value;${designator_suffix}")
foreach(expression IN ITEMS "object[0uptr]" "scalar[0uptr]")
    string(MD5 key "${expression}")
    reject(helper_untaken_index_${key} "subscript requires an array, pointer, or vector"
        "${designator_prefix}${expression};${designator_suffix}")
endforeach()
foreach(expression IN ITEMS "array[1.0f64]" "array[&scalar]")
    string(MD5 key "${expression}")
    reject(helper_untaken_subscript_${key} "subscript index must have an integer type"
        "${designator_prefix}${expression};${designator_suffix}")
endforeach()
reject(helper_untaken_dereference "dereference requires a pointer operand"
    "${designator_prefix}*scalar;${designator_suffix}")
foreach(expression IN ITEMS "scalar()" "(&scalar)()")
    string(MD5 key "${expression}")
    reject(helper_untaken_call_designator_${key} "called expression does not have a function type"
        "${designator_prefix}${expression};${designator_suffix}")
endforeach()
reject(helper_generic_member "has no member named 'missing'" [=[
struct Record { u32 value; };
static $::meta::tokens helper<T>(in T value, in $::meta::tokens input) {
    if ((bool)0u8) value.missing;
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    struct Record value = {1u32}; return helper(value, input);
}
global u32 entry() { return apply!(7u32); }
]=])
reject(helper_generic_call_designator "called expression does not have a function type" [=[
static $::meta::tokens helper<T>(in T value, in $::meta::tokens input) {
    if ((bool)0u8) value();
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(1u32, input); }
global u32 entry() { return apply!(7u32); }
]=])
reject(helper_const_array_arrow "cannot write a const subobject" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    struct Record { u32 values[2]; };
    const struct Record array[1] = {{{1u32, 2u32}}};
    if ((bool)0u8) array->values[0uptr] = 3u32;
    return input;
}
global u32 entry() { return 1u32; }
]=])
foreach(query IN ITEMS "sizeof(void)" "sizeof(Function)" "sizeof(value)"
        "sizeof(struct Incomplete)" "sizeof(u32 [[scalable_vector(4)]])"
        "sizeof(u32[])" "sizeof(IncompleteArray)"
        "$::alignof(void)" "$::alignof(Function)" "$::alignof(value)"
        "$::alignof(struct Incomplete)")
    string(MD5 key "${query}")
    reject(helper_untaken_layout_${key} "requires a complete object type"
        "struct Incomplete; typedef u32 Function(); typedef u32 IncompleteArray[]; static u32 value() { return 1u32; }\nstatic $::meta::tokens helper(in $::meta::tokens input) { if ((bool)0u8) { uptr invalid = ${query}; } return input; }\nglobal u32 entry() { return 1u32; }")
endforeach()
foreach(query IN ITEMS "sizeof(value.field)" "sizeof((value.field))"
        "sizeof(pointer->field)" "sizeof(make().field)"
        "$::alignof(value.field)" "$::alignof((pointer->field))"
        "$::alignof(make().field)")
    string(MD5 key "${query}")
    reject(helper_untaken_bitfield_layout_${key} "cannot be applied to a bit-field"
        "struct Bits { u32 field : 3; }; static struct Bits make() { struct Bits result = {1u32}; return result; }\nstatic $::meta::tokens helper(in $::meta::tokens input) { struct Bits value = {1u32}; struct Bits *pointer = &value; if ((bool)0u8) { uptr invalid = ${query}; } return input; }\nglobal u32 entry() { return 1u32; }")
endforeach()
foreach(actual IN ITEMS "void" "Function" "struct Incomplete" "u32 [[scalable_vector(4)]]" "u32[]")
    string(MD5 key "${actual}")
    reject(helper_generic_layout_${key} "sizeof requires a complete object type"
        "struct Incomplete; typedef u32 Function();\nstatic $::meta::tokens helper<T>(in $::meta::tokens input) { if ((bool)0u8) { uptr invalid = sizeof(T); } return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<${actual}>(input); }\nglobal u32 entry() { return apply!(7u32); }")
endforeach()
reject(helper_generic_bitfield_layout "sizeof cannot be applied to a bit-field" [=[
struct Bits { u32 field : 3; };
static $::meta::tokens helper<T>(in T value, in $::meta::tokens input) {
    if ((bool)0u8) { uptr invalid = sizeof(value.field); }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    struct Bits value = {1u32}; return helper(value, input);
}
global u32 entry() { return apply!(7u32); }
]=])
foreach(query IN ITEMS "sizeof(void)" "sizeof(u32[])")
    string(MD5 key "${query}")
    reject(expander_untaken_layout_${key} "sizeof requires a complete object type"
        "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { if ((bool)0u8) { uptr invalid = ${query}; } return input; }\nglobal u32 entry() { return apply!(7u32); }")
    reject(required_unselected_layout_${key} "sizeof requires a complete object type"
        "global uptr value = (bool)1u8 ? 7uptr : ${query};")
endforeach()
reject(expander_untaken_bitfield_layout "alignof cannot be applied to a bit-field" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    struct Bits { u32 field : 3; } value = {1u32};
    if ((bool)0u8) { uptr invalid = $::alignof(value.field); }
    return $::quote { 7u32 };
}
syntax Value : expression { prefix "value"; match "(" ")"; expand expand; }
syntax Value;
global u32 entry() { return value(); }
]=])
reject(helper_untaken_incomplete "local object requires a complete record type" [=[
struct Missing;
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) { struct Missing invalid; }
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_scalable_array "array element (cannot have scalable-vector type|type cannot be a scalable vector)" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) { u32 [[scalable_vector(4)]] invalid[2]; }
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_record_condition "condition must be scalar" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    struct Condition { u32 value; } condition = {1u32};
    if ((bool)0u8) { if (condition) return input; }
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_scalar_conditional "conditional selector must be scalar" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    struct Condition { u32 value; } condition = {1u32};
    if ((bool)0u8) { u32 value = condition ? 1u32 : 2u32; }
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_switch "switch condition must have an integer type" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) { switch (1.0f64) { case 1u32: break; } }
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_array_bound "array bound must have an integer type" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    f64 count = 3.0f64;
    if ((bool)0u8) { u32 invalid[count]; }
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_missing_return "non-void function must return a value" [=[
static u32 helper(in $::meta::tokens input) {
    if ((bool)0u8) return;
    return 7u32;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_untaken_ordinary_arity "function call requires 1 arguments" [=[
static void consume(in u32 value) { return; }
static $::meta::tokens helper(in $::meta::tokens input) {
    if ((bool)0u8) consume();
    return input;
}
global u32 entry() { return 1u32; }
]=])
reject(helper_generic_void_object "local declaration requires an object type" [=[
static $::meta::tokens helper<T>(in $::meta::tokens input) {
    if ((bool)0u8) { T invalid; }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<void>(input); }
global u32 entry() { return apply!(7u32); }
]=])
foreach(declarator IN ITEMS "*invalid" "invalid[2]")
    string(MD5 key "${declarator}")
    reject(helper_generic_nested_meta_${key} "meta values cannot have runtime local storage|opaque meta values cannot appear inside runtime"
        "static $::meta::tokens helper<T>(in T input) { if ((bool)0u8) { T ${declarator}; } return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(7u32); }")
endforeach()

# Opaque selection must not create conversions or hide an ill-typed arm.
reject(conditional_mixed_meta "conditional meta operands must have the same type" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::meta::buffer selected = (bool)1u8 ? $::meta::alloc(1uptr) : input;
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(conditional_runtime_arm "conditional meta operands must have the same type" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::meta::tokens selected = (bool)1u8 ? input : 0u32;
    return selected;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(conditional_meta_selector "conditional meta selector must be scalar" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::meta::buffer selected = input ? $::meta::alloc(1uptr) : $::meta::alloc(2uptr);
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(conditional_frozen_alias "after freeze|invalidated" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::meta::buffer left = $::meta::alloc(0uptr), right = $::meta::alloc(0uptr);
    $::meta::buffer selected = (bool)1u8 ? left : right;
    $::meta::bytes frozen = $::meta::freeze(selected, 0uptr);
    $::meta::bytes again = $::meta::freeze(left, 0uptr);
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(inferred_empty_after_pointer "materialized byte array has an invalid target-sized bound" [=[
static u32 anchor;
static uptr width<u32 *P>() { return sizeof(P); }
static uptr pointer_width = width<&anchor>();
static const u8 invalid[] = $::meta::freeze($::meta::alloc(0uptr), 0uptr);
global u32 entry() { return (u32)pointer_width; }
]=])
reject(inferred_void_after_pointer "array element type cannot be void" [=[
static u32 anchor;
static uptr width<u32 *P>() { return sizeof(P); }
static uptr pointer_width = width<&anchor>();
static void invalid[] = {1u32};
global u32 entry() { return (u32)pointer_width; }
]=])

set(invoke [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
reject(later_definition "unresolved name 'helper'"
    "${invoke}\nstatic $::meta::tokens helper(in $::meta::tokens input) { return input; }")
reject(recursion "translation-time recursion depth exceeded"
    "static $::meta::tokens helper(in $::meta::tokens input) { return helper(input); }\n${invoke}"
    -feval-depth-limit=32)
reject(steps "instruction budget exceeded"
    "static $::meta::tokens helper(in $::meta::tokens input) { while ((bool)1u8) {} return input; }\n${invoke}"
    -feval-step-limit=256)

# Constructed token trees are bounded even if the helper discards the value.
# Keep adjacent '[' tokens separate so bracket and attribute groups are distinct.
foreach(delimiter IN ITEMS paren bracket attribute brace)
    if(delimiter STREQUAL paren)
        set(open "( ")
        set(close ") ")
    elseif(delimiter STREQUAL bracket)
        set(open "[ ")
        set(close "] ")
    elseif(delimiter STREQUAL attribute)
        set(open "[[ ")
        set(close "]] ")
    else()
        set(open "{ ")
        set(close "} ")
    endif()
    string(REPEAT "${open}" 16 opens)
    string(REPEAT "${close}" 16 closes)
    foreach(constructor IN ITEMS parse quote)
        if(constructor STREQUAL parse)
            set(value "$::meta::parse(\"${opens}value ${closes}\")")
        else()
            set(value "$::quote { ${opens}value ${closes} }")
        endif()
        set(source "static $::meta::tokens helper(in $::meta::tokens input) { $::meta::tokens discarded = ${value}; return input; }\n${invoke}")
        reject(token_${constructor}_${delimiter}_depth "token-tree nesting depth exceeded"
            "${source}" -feval-depth-limit=8)
        compile(token_${constructor}_${delimiter}_ample "${source}" -feval-depth-limit=32)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "bounded ${constructor}/${delimiter} failed with ample depth\n${err}")
        endif()
    endforeach()
endforeach()

set(composed [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    $::meta::tokens child = $::quote { (((((((value))))))) };
    $::meta::tokens discarded = $::quote { ((((((( $::unquote(child) ))))))) };
    return input;
}
]=])
reject(composed_token_depth "token-tree nesting depth exceeded"
    "${composed}${invoke}" -feval-depth-limit=8)
compile(composed_token_ample "${composed}${invoke}" -feval-depth-limit=32)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "composed quotation failed with ample depth\n${err}")
endif()
compile(token_exact_depth
    "static $::meta::tokens helper(in $::meta::tokens input) { $::meta::tokens value = $::meta::parse(\"((((((((value))))))))\"); return input; }\n${invoke}"
    -feval-depth-limit=8)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "exact token depth boundary failed\n${err}")
endif()

string(REPEAT "( " 16 inspection_opens)
string(REPEAT ") " 16 inspection_closes)
foreach(operation IN ITEMS "$::meta::len(input)" "$::meta::at(input, 0uptr)"
        "$::meta::slice(input, 0uptr, 0uptr)" "$::meta::concat(input, $::quote {})")
    string(MD5 key "${operation}")
    set(source "static $::meta::tokens helper(in $::meta::tokens input) { ${operation}; return $::quote { 9u32 }; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(${inspection_opens}value ${inspection_closes}); }")
    reject(token_inspection_${key}_depth "token-tree nesting depth exceeded"
        "${source}" -feval-depth-limit=8)
    compile(token_inspection_${key}_ample "${source}" -feval-depth-limit=32)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "token inspection failed with ample depth: ${operation}\n${err}")
    endif()
endforeach()

string(REPEAT " " 1024 parse_whitespace)
set(parse_work "static $::meta::tokens helper(in $::meta::tokens input) { $::meta::tokens value = $::meta::parse(\"${parse_whitespace}\"); return input; }\n${invoke}")
reject(parse_lexical_work "instruction budget exceeded" "${parse_work}" -feval-step-limit=256)
compile(parse_lexical_work_ample "${parse_work}" -feval-step-limit=10000)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "empty token parse failed with ample work\n${err}")
endif()
string(REPEAT "word " 80 inspected_tokens)
set(inspect_work "static $::meta::tokens helper(in $::meta::tokens input) { for (u32 i = 0u32; i < 8u32; ++i) $::meta::len(input); return $::quote { 9u32 }; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(${inspected_tokens}); }")
# Leave enough for lexical/matching/cycle-key setup, but less than eight
# traversals of 80 tokens. This specifically tests evaluator instruction work.
reject(token_inspection_work "instruction budget exceeded" "${inspect_work}" -feval-step-limit=600)
compile(token_inspection_work_ample "${inspect_work}" -feval-step-limit=10000)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "token inspection failed with ample work\n${err}")
endif()

string(REPEAT " " 65536 parse_padding)
set(parse_memory "static $::meta::tokens helper(in $::meta::tokens input) { $::meta::tokens value = $::meta::parse(\"${parse_padding}\"); return input; }\n${invoke}")
reject(parse_temporary_memory "meta memory budget exceeded" "${parse_memory}"
    -feval-memory-limit=32768 -feval-step-limit=200000)
compile(parse_temporary_memory_ample "${parse_memory}"
    -feval-memory-limit=1048576 -feval-step-limit=200000)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "empty token parse failed with ample temporary memory\n${err}")
endif()

# Explicit projection exposes individual delimiter tokens for later quotation.
# They are not complete token trees until composed, but still consume work.
compile(projected_delimiter_composition [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::meta::syntax node = $::meta::parse("stmt", $::quote { {} }, $::syntax::context(input));
    $::meta::syntax compound = $::meta::child($::meta::child(node, 0uptr), 0uptr);
    $::meta::tokens open = $::meta::tokens($::meta::child(compound, 0uptr));
    $::meta::tokens close = $::meta::tokens($::meta::child(compound, 1uptr));
    $::meta::tokens fragment = $::quote { $::unquote(open) };
    return $::quote { $::unquote(fragment) return 9u32; $::unquote(close) };
}
syntax Body : statement { prefix "body"; match input:paren; expand expand; }
syntax Body;
global u32 entry() { body (); }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "projected delimiter composition failed\n${err}")
endif()

reject(volatile "meta cells require automatic translation-only storage"
    "static $::meta::tokens helper(in $::meta::tokens input) { volatile $::meta::tokens value = input; return value; }\n${invoke}")
reject(static_storage "meta cells require automatic translation-only storage"
    "static $::meta::tokens helper(in $::meta::tokens input) { static $::meta::tokens value = input; return value; }\n${invoke}")
reject(meta_cast "incompatible meta value in cast"
    "static $::meta::tokens helper(in $::meta::tokens input) { u32 value = (u32)input; return input; }\n${invoke}")
reject(input_const "cannot write a const cell"
    "static $::meta::tokens helper(in const $::meta::tokens input) { input = $::quote { 1u32 }; return input; }\n${invoke}")
reject(runtime_quote "quote cannot enter runtime expressions"
    "global u32 entry() { return $::quote { 1u32 }; }")
reject(helper_address "eval-only function 'helper' has no runtime address"
    "static $::meta::tokens helper(in $::meta::tokens input) { return input; }\nglobal uptr entry() { return (uptr)&helper; }")
reject(unused_register "meta cells require automatic translation-only storage"
    "static $::meta::tokens helper(in $::meta::tokens input) { register $::meta::tokens saved = input; return saved; }")
reject(missing_definition "eval_only requires a visible function definition"
    "static $::meta::tokens helper(in $::meta::tokens input);")
reject(conflicting_declaration "meta helper 'helper'.*incompatible interfaces"
    "static $::meta::tokens helper(in $::meta::tokens input);\nstatic $::meta::tokens helper(in u32 input) { return $::quote { 9u32 }; }")
reject(duplicate_definition "duplicate definition of meta helper 'helper'"
    "static $::meta::tokens helper() { return $::quote { 9u32 }; }\nstatic $::meta::tokens helper() { return $::quote { 7u32 }; }")

# These instances are reached only by expansion, never by the runtime program.
# Their constraints still apply, including assertions that instantiate helpers.
reject(operator_runtime_only "runtime_only" [=[
enum Count [[underlying(u32)]] { One = 1u32 };
[[operator("+"), runtime_only]] static enum Count add(in enum Count left, in enum Count right) { return left; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    enum Count value = One + One; return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(operator_duplicate_const "duplicate exact operator binding" [=[
enum Count [[underlying(u32)]] { One = 1u32 };
[[operator("+" )]] static enum Count add(in const enum Count left, in enum Count right) { return left; }
[[operator("+" )]] static enum Count other(in enum Count left, in const enum Count right) { return right; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return input; }
global u32 entry() { return apply!(9u32); }
]=])
reject(operator_generic_constraint "static_assert failed: operator helper constraint" [=[
enum Count [[underlying(u32)]] { One = 1u32 };
static T copy<T>(in T value) {
    $::static_assert(sizeof(T) == 0uptr, "operator helper constraint"); return value;
}
[[operator("+" )]] static enum Count add(in enum Count left, in enum Count right) { return copy(left); }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    enum Count value = One + One; return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(generic_assertion "static_assert failed: expansion constraint" [=[
static T helper<T>(in T input) {
    $::static_assert(sizeof(T) == 0uptr, "expansion constraint");
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    u32 value = helper(9u32); return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(nested_generic_assertion "static_assert failed: nested expansion constraint" [=[
static bool constraint<T>() {
    $::static_assert(sizeof(T) == 0uptr, "nested expansion constraint");
    return (bool)1u8;
}
static T helper<T>(in T input) {
    $::static_assert(constraint<T>(), "outer expansion constraint");
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    u32 value = helper(9u32); return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(enum_forward "has no value in this syntax context" [=[
enum Invalid { First = Second, Second = 2 };
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    u32 value = (u32)First; return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(enum_cycle "has no value in this syntax context" [=[
static u32 cycle();
enum Invalid { First = cycle() };
static u32 cycle() { return (u32)First; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    u32 value = (u32)First; return input;
}
global u32 entry() { return apply!(9u32); }
]=])

reject(expander_assertion "static_assert failed: required expander assertion" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::static_assert(0u32, "required expander assertion"); return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(expander_untaken_initializer "meta values cannot initialize runtime aggregate members" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    if ((bool)0u8) { struct Value { u32 x; } value = { input }; }
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(expander_untaken_const "cannot write a const subobject" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    const struct Value { u32 x; } value = { 7u32 };
    if ((bool)0u8) value.x = 3u32;
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(expander_duplicate_initializer "initialized more than once|duplicate" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    if ((bool)0u8) { struct Value { u32 x; } value = { .x = 1u32, .x = 2u32 }; }
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(expander_unassigned_member "unassigned" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    struct Value { u32 x; } value;
    if (value.x != 0u32) return input;
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(expander_expired_local "lifetime|scope|expired|dead" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    u32 outer = 1u32;
    u32 *pointer = &outer;
    { u32 inner = 7u32; pointer = &inner; }
    if (*pointer != 0u32) return input;
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(expander_invalid_vla "array bound must be a positive" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    uptr count = $::meta::len(input) - 1uptr;
    u32 values[count];
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
reject(expander_small_vla "initializer.*out of range|too many" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    uptr count = $::meta::len(input);
    u32 values[count] = {1u32, 2u32};
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])

compile(call_trace [=[
static void failure(in $::meta::tokens input) { u32 invalid = 1u32 / 0u32; }
static void forward(in $::meta::tokens input) { return failure(input); }
static $::meta::tokens helper(in $::meta::tokens input) { forward(input); return input; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
if(NOT result EQUAL 1 OR NOT err MATCHES "division by zero" OR
   NOT err MATCHES "while evaluating call to 'failure'" OR
   NOT err MATCHES "while evaluating call to 'forward'" OR
   NOT err MATCHES "while evaluating call to 'helper'")
    message(FATAL_ERROR "helper failure lost call ancestry or was swallowed by void return\n${err}")
endif()

# An ordinary helper can forward void and can be introduced by a surviving
# textual expansion. Such helpers have no emitted symbol, including with
# automatic ordinary-call evaluation disabled.
compile(generated_helper [=[
[[macro]] static $::meta::tokens introduce(in $::meta::tokens input) {
    return $::quote {
        static void check(in $::meta::tokens value) { return; }
        static void forward(in $::meta::tokens value) { return check(value); }
        static $::meta::tokens helper(in $::meta::tokens value) { forward(value); value = $::quote { 9u32 }; return value; }
    };
}
introduce!()
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "generated helper declarations were unavailable\n${err}")
endif()
file(READ "${directory}/generated_helper.s" assembly)
if(assembly MATCHES "helper|forward|check")
    message(FATAL_ERROR "meta-only helper acquired a runtime symbol\n${assembly}")
endif()

compile(header_helper [=[
[[syntax_expander]] static $::meta::tokens define(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "header"))
        { return $::quote { 9u32 }; } };
}
syntax Define : item { prefix "define_helper"; match header:function_header ";"; expand define; }
syntax Define;
define_helper static $::meta::tokens helper();
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(); }
global u32 entry() { return apply!(); }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "composed helper header was published without its definition\n${err}")
endif()

compile(redeclared_helper [=[
static $::meta::tokens helper(in const $::meta::tokens input);
static $::meta::tokens helper(in const $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(9u32); }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "visible helper definition did not satisfy its declaration\n${err}")
endif()

# Attribute semantics belong to surviving declarations, not speculative public
# tree recognition. Discarding a captured declaration must not validate it.
compile(discard_attribute_arguments [=[
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
syntax Drop : item { prefix "drop"; match body:function_def; expand drop; }
syntax Drop;
drop [[eval_only(123u32)]] static u32 discarded() { return 9u32; }
global u32 entry() { return 9u32; }
]=])
if(NOT result EQUAL 0)
    message(FATAL_ERROR "discarded capture underwent attribute semantic validation\n${err}")
endif()
reject(surviving_attribute_arguments "eval_only does not take arguments" [=[
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Keep : item { prefix "keep"; match body:function_def; expand keep; }
syntax Keep;
keep [[eval_only(123u32)]] static u32 retained() { return 9u32; }
global u32 entry() { return 9u32; }
]=])

set(attributed_helper [=[
[[cold, noinline]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }
[[eval_only]] static u32 constant() { return 9u32; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    if (constant() != 9u32) return $::quote { invalid_result };
    return helper(input);
}
global u32 entry() { return apply!(9u32); }
]=])
foreach(mode native custom mips mipsel mips64 mips64el)
    set(flags -O2)
    if(mode STREQUAL custom)
        list(APPEND flags "--model=${CMAKE_CURRENT_LIST_DIR}/../model/custom.y" -mprofile=test-profile)
    elseif(NOT mode STREQUAL native)
        list(APPEND flags -target "${mode}-unknown-linux-gnu")
    endif()
    compile(attributed_helper_${mode} "${attributed_helper}" ${flags})
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${mode} valid attributed helpers failed\n${err}")
    endif()
    file(READ "${directory}/attributed_helper_${mode}.s" assembly)
    if(assembly MATCHES "helper|constant|apply")
        message(FATAL_ERROR "${mode} attributed translation-only helper acquired a runtime symbol\n${assembly}")
    endif()
endforeach()

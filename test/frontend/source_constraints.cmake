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
set(definitions [=[
struct Pair { u32 value; };
struct Other { u32 value; };
struct Bits { u32 value : 3; };
struct Incomplete;
static struct Pair make() { struct Pair value = {1u32}; return value; }
static struct Other other() { struct Other value = {1u32}; return value; }
static void empty() {}
static u32 take(in u32 value) { return value; }
static u32 output(out u32 value) { value = 1u32; return 1u32; }
static u32 update(inout u32 value) { value += 1u32; return value; }
#ifdef VOID_VARIADIC_CASE
global u32 tail(in u32 first, ...);
#endif
typedef u32 Words [[ext_vector_type(4)]];
global const u32 readonly = 1u32;
global u32 writable;
global struct Pair array[2];
global struct Pair held;
global struct Bits bits;
global struct Incomplete *incomplete;
global void *opaque;
typedef struct Incomplete IncompleteRow[2];
global IncompleteRow *incomplete_row;
global u32 (*callback)(in u32 value);
]=])
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${definitions}\n${source}")
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
function(reject_expression name expression expected)
    foreach(query sizeof "$::alignof")
        string(MAKE_C_IDENTIFIER "${query}" query_case)
        check(${name}_${query_case} "${expected}"
            "$::static_assert(${query}(${expression}) > 0uptr, \"source constraint\");" ${ARGN})
    endforeach()
endfunction()
reject_expression(address_rvalue "&1u32" "address-of requires an object or function lvalue")
reject_expression(assign_rvalue "1u32 = 2u32" "assignment or update requires an object lvalue")
reject_expression(update_rvalue "++1u32" "assignment or update requires an object lvalue")
reject_expression(assign_const "readonly = 2u32" "cannot write a const cell")
reject_expression(update_const "++readonly" "cannot write a const cell")
reject_expression(scalar_member "(u32)((1u32).field)" "member access requires a record object")
reject_expression(missing_member "(u32)make().missing" "no member named 'missing'")
reject_expression(array_member "(u32)array->missing" "no member named 'missing'")
reject_expression(incomplete_member "(u32)incomplete->field" "member access requires a complete record type")
reject_expression(pointer_member "(u32)((struct Pair *)0uptr)->missing" "no member named 'missing'")
reject_expression(scalar_index "(u32)1u32[0uptr]" "subscript requires an array, pointer, or vector")
reject_expression(floating_index "array[1.0f64]" "subscript index must have an integer type")
reject_expression(dereference "(u32)*1u32" "dereference requires a pointer operand")
set(index_element "subscript requires a complete object element type")
reject_expression(void_index "opaque[0uptr]" "${index_element}")
reject_expression(incomplete_index "incomplete[0uptr]" "${index_element}")
reject_expression(incomplete_row_index "incomplete_row[0uptr]" "${index_element}")
reject_expression(function_index "callback[0uptr]" "${index_element}")
reject_expression(address_void_index "&opaque[0uptr]" "address-of requires an object or function lvalue")
reject_expression(address_incomplete_index "&incomplete[0uptr]" "${index_element}")
reject_expression(address_incomplete_row_index "&incomplete_row[0uptr]" "${index_element}")
reject_expression(void_dereference "*opaque" "dereference requires a pointer to an object or function type")
reject_expression(incomplete_read_cast "(struct Incomplete)*incomplete" "value access requires a complete object type")
reject_expression(incomplete_layout "*incomplete" "requires a complete object type")
reject_expression(temporary_write "(u32)(make().value = 2u32)" "record value is not an object designator")
reject_expression(temporary_address "&make().value" "record value is not an object designator")
reject_expression(bitfield_address "&bits.value" "cannot take the address of a bit-field")
reject_expression(record_scalar "(u32)make()" "same nominal record type in cast")
reject_expression(record_condition "1u32 ? make() : other()" "conditional record operands must have the same nominal record type")
reject_expression(record_sum "make() + make()" "built-in operator.*cannot consume record values")
reject_expression(record_comparison "make() == make()" "built-in operator.*cannot consume record values")
reject_expression(record_cast "(struct Pair)1u32" "same nominal record type in cast")
reject_expression(record_assignment "held = other()" "same nominal record type in assignment")
reject_expression(record_update "++held" "record values do not support update or compound assignment")
reject_expression(record_compound "held += make()" "record values do not support update or compound assignment")
check(unselected_address "address-of requires an object or function lvalue"
    "$::static_assert(1u32 || (uptr)&1u32, \"unselected address\");")
check(unselected_member "no member named 'missing'"
    "$::static_assert(1u32 || (u32)make().missing, \"unselected member\");")
check(unselected_record "same nominal record type in cast"
    "$::static_assert(1u32 || (u32)make(), \"unselected record\");")
check(unselected_incomplete_index "${index_element}"
    "$::static_assert(1u32 || (uptr)&incomplete[0uptr], \"unselected element stride\");")

function(reject_pointer_body name body expected)
    set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }")
    check(${name}_unused "${expected}" "${helper} global u32 entry() { return 1u32; }")
    check(${name}_called "${expected}" "${helper}
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }")
    check(${name}_runtime "${expected}"
        "global u32 entry() { if (0u32) { ${body} } return 1u32; }")
    check(${name}_macro "${expected}"
        "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
            if (0u32) { ${body} } return input;
        } global u32 entry() { return apply!(1u32); }")
    check(${name}_expander "${expected}"
        "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
            if (0u32) { ${body} } return $::quote { 1u32 };
        }
        syntax Form : expression { prefix \"form\"; match \"(\" \")\"; expand expand; }
        syntax Form;
        global u32 entry() { return form(); }")
endfunction()
reject_pointer_body(void_element "opaque[0uptr];" "${index_element}")
reject_pointer_body(incomplete_element "incomplete[0uptr];" "${index_element}")
reject_pointer_body(incomplete_row_element "&incomplete_row[0uptr];" "${index_element}")
reject_pointer_body(function_element "callback[0uptr];" "${index_element}")
reject_pointer_body(void_target "*opaque;" "dereference requires a pointer to an object or function type")
reject_pointer_body(incomplete_value "*incomplete;" "value access requires a complete object type")
check(generic_incomplete_element "${index_element}" [=[
static $::meta::tokens select<T>(in $::meta::tokens input, in T *pointer) {
    if (0u32) { pointer[0uptr]; }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    return select<struct Incomplete>(input, (struct Incomplete *)0uptr);
}
global u32 entry() { return apply!(1u32); }
]=])
check(generic_void_element "${index_element}" [=[
static $::meta::tokens select<T>(in $::meta::tokens input, in T *pointer) {
    if (0u32) { pointer[0uptr]; }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    return select<void>(input, (void *)0uptr);
}
global u32 entry() { return apply!(1u32); }
]=])
check(generic_incomplete_value "value access requires a complete object type" [=[
static $::meta::tokens read<T>(in $::meta::tokens input, in T *pointer) {
    if (0u32) { *pointer; }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    return read<struct Incomplete>(input, (struct Incomplete *)0uptr);
}
global u32 entry() { return apply!(1u32); }
]=])
check(out_incomplete_not_read "same nominal record type in parameter copy-out"
    "$::static_assert(sizeof(output(*incomplete)) == sizeof(u32), \"out does not read\");")

# Void calls retain effects but provide no input value, even where an outer
# query, unused helper or unselected branch would otherwise erase the error.
set(void_value "void expression cannot supply a value")
foreach(type u32 u128 f64 bool "u32 *" label Words)
    string(MAKE_C_IDENTIFIER "${type}" type_case)
    reject_expression(void_cast_${type_case} "(${type})empty()" "${void_value}")
endforeach()
reject_expression(void_assignment "writable = empty()" "${void_value}")
reject_expression(void_input "take(empty())" "${void_value}")
reject_expression(void_inout "update(empty())" "${void_value}")
reject_expression(void_variadic "tail(1u32, empty())" "${void_value} in variadic argument" -DVOID_VARIADIC_CASE)
check(void_unselected "${void_value}"
    "$::static_assert(1u32 || (u32)empty(), \"unselected void cast\");")
function(reject_void_body name body)
    set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }")
    check(${name}_unused "${void_value}" "${helper} global u32 entry() { return 1u32; }" ${ARGN})
    check(${name}_called "${void_value}" "${helper}
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }" ${ARGN})
endfunction()
reject_void_body(void_initializer "u32 value = empty();")
reject_void_body(void_brace "u32 values[1] = {empty()};")
reject_void_body(void_assign "u32 value; value = empty();")
reject_void_body(void_call "take(empty());")
reject_void_body(void_update_call "update(empty());")
reject_void_body(void_tail_call "tail(1u32, empty());" -DVOID_VARIADIC_CASE)
check(void_reached "${void_value}" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    u32 value = empty();
    return input;
}
global u32 entry() { return apply!(1u32); }
]=])
check(void_generic "${void_value}" [=[
static T helper<T>(in $::meta::tokens input) { if (0u32) { T value = empty(); } return 1; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { helper<u32>(input); return input; }
global u32 entry() { return apply!(1u32); }
]=])
check(void_expander "${void_value}" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) { u32 value = empty(); }
    return $::quote { 1u32 };
}
syntax Form : expression { prefix "form"; match "(" ")"; expand expand; }
syntax Form;
global u32 entry() { return form(); }
]=])

check(valid_controls pass [=[
$::static_assert(sizeof(&writable) == sizeof(u32 *), "valid address without read");
$::static_assert(sizeof(++writable) == sizeof(u32), "no static update");
$::static_assert(sizeof(writable = 7u32) == sizeof(u32), "no static assignment");
$::static_assert(sizeof(array[0uptr]) == sizeof(struct Pair), "valid array selection");
$::static_assert(sizeof(array->value) == sizeof(u32), "array decay for arrow");
$::static_assert(sizeof(make().value) == sizeof(u32), "temporary member value");
$::static_assert(sizeof(((struct Pair *)0uptr)->value) == sizeof(u32), "no null dereference");
$::static_assert(sizeof((struct Pair)make()) == sizeof(struct Pair), "nominal copy cast");
$::static_assert(sizeof(held = make()) == sizeof(struct Pair), "record copy assignment");
$::static_assert(sizeof(1u32 ? make() : make()) == sizeof(struct Pair), "same record selection");
$::static_assert($::alignof(&readonly) == $::alignof(const u32 *), "const address is valid");
$::static_assert(sizeof(output(empty())) == sizeof(u32), "out actual supplies no value");
$::static_assert(sizeof(&(*incomplete)) == sizeof(struct Incomplete *), "incomplete designator does not read");
$::static_assert(sizeof(&(*incomplete_row)) == sizeof(IncompleteRow *), "incomplete array designator");
$::static_assert(sizeof((*callback)(1u32)) == sizeof(u32), "function designator remains callable");
$::static_assert(sizeof(&*callback) == sizeof(callback), "function address without call");
static void forward() { return empty(); }
global struct Incomplete *retain(in struct Incomplete *pointer) { return &(*pointer); }
global IncompleteRow *retain_row(in IncompleteRow *pointer) { return &(*pointer); }
global u32 invoke(in u32 (*function)(in u32 value)) { return (*function)(1u32); }
static $::meta::tokens inspect(in $::meta::tokens input) {
    u32 local;
    uptr width = sizeof(++local) + sizeof(&local);
    if (0u32) { empty(); output(empty()); forward(); }
    return input;
}
global u32 entry() { return 1u32; }
]=])

set(capture [=[
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
check(discard pass "${capture}
$::static_assert(drop(sizeof(&1u32)) == 1u32, \"discarded address\");
$::static_assert(drop(sizeof((u32)make().missing)) == 1u32, \"discarded member\");
$::static_assert(drop(sizeof(held = other())) == 1u32, \"discarded nominal constraint\");
$::static_assert(drop(sizeof((u32)empty())) == 1u32, \"discarded void cast\");
$::static_assert(drop(sizeof(opaque[0uptr])) == 1u32, \"discarded invalid stride\");
$::static_assert(drop(sizeof(&incomplete[0uptr])) == 1u32, \"discarded incomplete stride\");
$::static_assert(drop((struct Incomplete)*incomplete) == 1u32, \"discarded incomplete read\");
global u32 entry() { return 1u32; }")
check(survival "no member named 'missing'"
    "${capture} $::static_assert(keep(sizeof((u32)make().missing)) > 0uptr, \"surviving member\");")
check(void_survival "${void_value}"
    "${capture} $::static_assert(keep(sizeof((u32)empty())) > 0uptr, \"surviving void cast\");")
check(index_survival "${index_element}"
    "${capture} $::static_assert(keep(sizeof(&incomplete[0uptr])) > 0uptr, \"surviving incomplete stride\");")
check(incomplete_value_survival "value access requires a complete object type"
    "${capture} global u32 entry() { keep((struct Incomplete)*incomplete); return 1u32; }")

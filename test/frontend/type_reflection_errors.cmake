# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# Each case must fail with the message at the given line and column.
function(reject name location expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu -S -O0
        "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(status EQUAL 0 OR NOT err MATCHES "${name}.x:${location}: error: [^\n]*${expected}")
        message(FATAL_ERROR "${name}: missing '${expected}' at ${location}\n${out}\n${err}")
    endif()
endfunction()

# A macro that applies one query to the reflected input.
function(query name location expected statement)
    reject(${name} ${location} "${expected}" "struct pair { u32 a; u16 b; };
struct later;
enum tone { low, high };
[[macro]] static \$::meta::tokens apply(\$::meta::tokens input) {
    \$::meta::type t = \$::meta::type(input);
    ${statement}
    return \$::meta::parse(\"\");
}
")
endfunction()

query(unknown_kind 6:43 "unknown type kind 'record'" [=[
    bool r = $::meta::is_type_kind(t, "record"); }
apply! { struct pair } static void unused() {]=])
query(index_range 6:53 "member_name index is out of range" [=[
    $::meta::tokens n = $::meta::member_name(t, 2); }
apply! { struct pair } static void unused() {]=])
query(negative_index 6:44 "member_offset index is out of range" [=[
    uptr n = $::meta::member_offset(t, (i32)-1); }
apply! { struct pair } static void unused() {]=])
query(enumerator_range 6:57 "enumerator_name index is out of range" [=[
    $::meta::tokens n = $::meta::enumerator_name(t, 2); }
apply! { enum tone } static void unused() {]=])
query(incomplete_members 6:18 "member_count requires a complete record or union type" [=[
    uptr n = $::meta::member_count(t); }
apply! { struct later } static void unused() {]=])
query(incomplete_size 6:18 "size requires a complete object type" [=[
    uptr n = $::meta::size(t); }
apply! { struct later } static void unused() {]=])
query(scalar_members 6:18 "member_count requires a record or union type" [=[
    uptr n = $::meta::member_count(t); }
apply! { u32 } static void unused() {]=])
query(record_bits 6:17 "bits requires a scalar or enumeration type" [=[
    u32 n = $::meta::bits(t); }
apply! { struct pair } static void unused() {]=])
query(record_element 6:27 "element requires an array, vector, pointer, or enumeration type" [=[
    $::meta::type e = $::meta::element(t); }
apply! { struct pair } static void unused() {]=])
query(record_enumerators 6:18 "enumerator_count requires an enumeration type" [=[
    uptr n = $::meta::enumerator_count(t); }
apply! { struct pair } static void unused() {]=])
query(not_a_type 5:23 "type requires tokens that name a type" [=[
}
apply! { 1 + 2 } static void unused() {]=])
query(compare 6:23 "opaque meta values do not support binary operators" [=[
    bool same = t == t; }
apply! { u32 } static void unused() {]=])
reject(static_value 1:8 "meta values cannot have runtime object storage" [=[
global $::meta::type stored;
]=])
reject(outside_expansion 2:23 "meta operation requires an active expansion context" [=[
[[eval_only]] static uptr size() {
    $::meta::type t = $::meta::type($::meta::parse("u32"));
    return $::meta::size(t);
}
$::static_assert(size() == 4, "size");
]=])

# The operations are registered built-ins.
file(WRITE "${OUTPUT}/queries.x" [=[
#if $::has_builtin($::meta::type) && $::has_intrinsic($::meta::member_bit_offset) && $::has_intrinsic($::meta::type_tokens)
global u32 available = 1;
#endif
]=])
execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu -S "${OUTPUT}/queries.x"
    -o "${OUTPUT}/queries.s" RESULT_VARIABLE status ERROR_VARIABLE err)
file(READ "${OUTPUT}/queries.s" assembly)
if(NOT status EQUAL 0 OR NOT assembly MATCHES "available")
    message(FATAL_ERROR "reflection operations are not reported by \$::has_builtin\n${err}")
endif()
execute_process(COMMAND "${CC}" --print-builtins OUTPUT_VARIABLE builtins RESULT_VARIABLE status)
foreach(operation type same_type is_type_kind size align bits is_signed member_count member_name
                  member_type member_offset member_bit_offset member_bit_width element count
                  enumerator_count enumerator_name type_tokens)
    if(NOT builtins MATCHES "[$]::meta::${operation} type reflection")
        message(FATAL_ERROR "--print-builtins omits \$::meta::${operation}\n${builtins}")
    endif()
endforeach()

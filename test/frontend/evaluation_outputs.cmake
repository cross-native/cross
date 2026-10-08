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
elseif(MODE STREQUAL mips OR MODE STREQUAL mipsel)
    list(APPEND flags -target "${MODE}-unknown-elf" -mprofile=r3000-o32)
elseif(MODE STREQUAL mips64 OR MODE STREQUAL mips64el)
    list(APPEND flags -target "${MODE}-unknown-elf" -mabi=n64)
elseif(NOT MODE STREQUAL native)
    message(FATAL_ERROR "unknown profile ${MODE}")
endif()
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" ${flags} -S -${level} -fno-eval-calls
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${MODE}/${name}/${level}: unexpected rejection\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${MODE}/${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
set(fill "static void fill(out u32 value) { value = 73u32; }\n")
set(assert_run "$::static_assert($::eval(run()) == 1u32, \"output evaluation\");\n")
function(probe name expected body)
    check(${name} "${expected}" "${fill}static u32 run() { ${body} }\n${assert_run}")
endfunction()
check(unassigned "read of uninitialized"
    "static void bad(out u32 value) {} static u32 run() { u32 value; bad(value); return 1u32; } ${assert_run}")
check(read_before "(uninitialized|unassigned)"
    "static void bad(out u32 value) { value += 1u32; } static u32 run() { u32 value; bad(value); return 1u32; } ${assert_run}")
check(partial_record "was not assigned on normal return"
    "struct Pair { u8 first; uptr second; }; static void bad(out struct Pair value) { value.first = 1u8; } static u32 run() { struct Pair value; bad(value); return 1u32; } ${assert_run}")
check(partial_union "was not assigned on normal return"
    "struct Pair { u8 first; uptr second; }; union Choice { struct Pair pair; }; static void bad(out union Choice value) { value.pair.first = 1u8; } static u32 run() { union Choice value; bad(value); return 1u32; } ${assert_run}")
check(partial_bits "was not assigned on normal return"
    "struct Bits { u32 first : 3; u32 second : 5; }; static void bad(out struct Bits value) { value.first = 1u32; } static u32 run() { struct Bits value; bad(value); return 1u32; } ${assert_run}")
check(inout_unassigned "(uninitialized|unassigned)"
    "static void bad(inout u32 value) { value = 1u32; } static u32 run() { u32 value; bad(value); return 1u32; } ${assert_run}")
check(escaped_pointer "(lifetime|unassigned)"
    "static void bad(out u32 *value) { u32 local = 1u32; value = &local; } static u32 run() { u32 *value; bad(value); return *value; } ${assert_run}")
probe(expired_destination "lifetime" "u32 *value; { u32 local = 1u32; value = &local; } fill(*value); return 1u32;")
probe(static_destination "runtime/static storage" "static u32 value; fill(value); return 1u32;")
check(global_destination "runtime/static storage"
    "${fill}static u32 value; static u32 run() { fill(value); return 1u32; } ${assert_run}")
probe(volatile_destination "volatile" "volatile u32 value; fill(value); return 1u32;")
probe(atomic_destination "(atomic|volatile|supported storage)" "u32 [[atomic]] value; fill(value); return 1u32;")
check(runtime_local "not a translation-time value"
    "${fill}global u32 entry() { u32 value; $::eval(fill(value)); return 1u32; }")
check(eval_only_output "eval_only parameters must use 'in'"
    "[[eval_only]] static void invalid(out u32 value) { value = 1u32; }")
check(meta_output "eval_only parameters must use 'in'"
    "static $::meta::tokens invalid(out u32 value) { value = 1u32; return $::quote { 1u32 }; }")
check(noreturn_output "noreturn.*returned normally"
    "[[noreturn]] static void bad(out u32 value) { value = 1u32; return; } static u32 run() { u32 value; bad(value); return 1u32; } ${assert_run}")
check(failed_body "division by zero"
    "static u32 bad(out u32 value, in u32 divisor) { value = 1u32; return 1u32 / divisor; } static u32 run() { u32 value; return bad(value, 0u32); } ${assert_run}")
probe(const_discard pass "const u32 value; fill(value); return 1u32;")
check(complete_union pass
    "union Choice { uptr large; u8 small; }; static void small(out union Choice value) { value.small = 19u8; } static u32 run() { union Choice value; small(value); return value.small == 19u8 ? 1u32 : 0u32; } ${assert_run}")
check(addressed_cells pass [=[
static u32 separate(inout u32 first, inout u32 second) {
    u32 *pointer = &first; *pointer = 9u32; return second;
}
static u32 run() { u32 value = 3u32; u32 saved = separate(value, value); return saved == 3u32 && value == 3u32 ? 1u32 : 0u32; }
$::static_assert($::eval(run()) == 1u32, "independent addressed cells");
]=])
function(meta_probe name expected body)
    check(${name} "${expected}" "${fill}static $::meta::tokens helper(in $::meta::tokens input) { ${body} return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\n$::static_assert(apply!(1u32) == 1u32, \"allocated output storage\");")
endfunction()
set(buffer "$::meta::buffer buffer = $::meta::alloc(16uptr);")
meta_probe(allocated_output pass "${buffer} u32 *value = (u32 *)$::meta::data(buffer); fill(*value); if (*value != 73u32) return $::quote { 0u32 };")
meta_probe(frozen_destination "after freeze" "${buffer} u32 *value = (u32 *)$::meta::data(buffer); $::meta::bytes bytes = $::meta::freeze(buffer, 0uptr); fill(*value);")
meta_probe(unaligned_destination "aligned" "${buffer} u32 *value = (u32 *)($::meta::data(buffer) + 1uptr); fill(*value);")
meta_probe(out_of_bounds "(bounds|view)" "${buffer} u32 *value = (u32 *)($::meta::data(buffer) + 16uptr); fill(*value);")
meta_probe(effective_type "effective type" "${buffer} f32 *value = (f32 *)$::meta::data(buffer); *value = 1.5f32; fill(*(u32 *)value);")
# Direct vector transport is a native x86-64 test, not a claim of MIPS vector ABI
# support. Nested vector fields are separately exercised in the runtime matrix.
if(MODE STREQUAL native OR MODE STREQUAL custom)
    check(direct_vector pass [=[
typedef u32 Lanes [[ext_vector_type(4)]];
static void fill_vector(out Lanes value) { value = 17u32; }
static u32 run() { Lanes value; fill_vector(value); return value[0u32] == 17u32 && value[3u32] == 17u32 ? 1u32 : 0u32; }
$::static_assert($::eval(run()) == 1u32, "direct vector output evaluation");
]=])
    check(f80_outputs pass [=[
static void initialize(out f80 value) { value = 1.25f80; }
static void increment(inout f80 value) { value += 0.5f80; }
static u32 run() { f80 value; initialize(value); increment(value); return value == 1.75f80 ? 1u32 : 0u32; }
$::static_assert($::eval(run()) == 1u32, "extended floating output evaluation");
]=])
endif()
check(late_copyout_failure "effective type" [=[
static void fail_copy(out u32 first, out u32 second, in f32 *alias) {
    first = 1u32; second = 2u32; *alias = 1.5f32;
}
static $::meta::tokens helper(in $::meta::tokens input) {
    $::meta::buffer buffer = $::meta::alloc(4uptr);
    u32 *value = (u32 *)$::meta::data(buffer);
    u32 first;
    fail_copy(first, *value, (f32 *)value);
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
$::static_assert(apply!(1u32) == 1u32, "late copy-out failure");
]=])

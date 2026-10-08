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
typedef u32 *Pointer;
global u32 [[atomic]] object;
global const u32 [[atomic]] readonly;
global volatile u16 [[atomic]] narrow;
global f32 [[atomic]] real;
global Pointer [[atomic]] pointer;
global u32 plain;
global u64 wide;
global const u32 fixed;
global volatile u32 watched;
global struct Pair pair;
static void empty() {}
static u32 value() { return 1u32; }
]=])
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${definitions}\n${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: unexpected rejection\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
function(reject_body name expected body)
    set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }")
    check(${name}_unused "${expected}" "${helper} global u32 entry() { return 1u32; }")
    check(${name}_called "${expected}" "${helper}
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }")
endfunction()
foreach(operation load store exchange compare_exchange fetch_add fetch_sub fetch_and fetch_xor fetch_or)
    reject_body(${operation}_arity "atomic_${operation} requires [235] arguments" "$::atomic_${operation}();")
endforeach()
foreach(operation thread_fence signal_fence)
    reject_body(${operation}_arity "requires one memory-order argument" "$::atomic_${operation}();")
    reject_body(${operation}_order "expected one of.*memory::relaxed" "$::atomic_${operation}(3u32);")
endforeach()
reject_body(query_arity "requires one type or expression" "$::atomic_is_lock_free();")
reject_body(query_type "requires a scalar type or expression" "$::atomic_is_lock_free(pair);")
reject_body(query_void "requires a scalar type or expression" "$::atomic_is_lock_free(empty());")
reject_body(non_atomic "requires a pointer to an atomic-qualified scalar"
    "$::atomic_load(&plain, $::memory::relaxed);")
reject_body(non_pointer "requires a pointer to an atomic-qualified scalar"
    "$::atomic_load(1u32, $::memory::relaxed);")
reject_body(const_store "cannot modify a const atomic object"
    "$::atomic_store(&readonly, 1u32, $::memory::relaxed);")
foreach(operation add sub and xor or)
    reject_body(fetch_${operation}_type "require an integer object"
        "$::atomic_fetch_${operation}(&real, 1.0f32, $::memory::relaxed);")
endforeach()
reject_body(load_order "atomic load order must be relaxed, acquire, or seq_cst"
    "$::atomic_load(&object, $::memory::release);")
reject_body(store_order "atomic store order must be relaxed, release, or seq_cst"
    "$::atomic_store(&object, 1u32, $::memory::acquire);")
reject_body(order_value "expected one of.*memory::relaxed"
    "$::atomic_load(&object, 0u32);")
reject_body(expected_value "expected argument must be a pointer"
    "$::atomic_compare_exchange(&object, 0u32, 1u32, $::memory::seq_cst, $::memory::relaxed);")
foreach(expected wide fixed object)
    reject_body(expected_${expected} "expected pointer has an incompatible type"
        "$::atomic_compare_exchange(&object, &${expected}, 1u32, $::memory::seq_cst, $::memory::relaxed);")
endforeach()
foreach(argument pair "$::quote {}")
    string(MD5 key "${argument}")
    reject_body(value_${key} "atomic value argument requires a convertible scalar value"
        "$::atomic_store(&object, ${argument}, $::memory::relaxed);")
endforeach()
reject_body(value_void "void expression cannot supply a value"
    "$::atomic_exchange(&object, empty(), $::memory::relaxed);")
reject_body(value_pointer "implicit pointer-to-integer conversion requires an explicit cast"
    "$::atomic_compare_exchange(&object, &plain, &plain, $::memory::seq_cst, $::memory::relaxed);")
reject_body(pointer_nonzero "implicit integer-to-pointer conversion requires a constant zero"
    "$::atomic_store(&pointer, 1u32, $::memory::relaxed);")
reject_body(pointer_runtime "implicit integer-to-pointer conversion requires a constant zero"
    "$::atomic_store(&pointer, plain, $::memory::relaxed);")
reject_body(pointer_float "conversion cannot convert between a pointer and a floating type"
    "$::atomic_store(&pointer, 1.0f32, $::memory::relaxed);")
reject_body(pointer_pointee "implicit pointer conversion discards qualifiers or uses incompatible pointee types"
    "$::atomic_store(&pointer, &fixed, $::memory::relaxed);")
reject_body(store_void_value "void expression cannot supply a value"
    "sizeof((u32)$::atomic_store(&object, 1u32, $::memory::relaxed));")
foreach(query sizeof "$::alignof")
    string(MAKE_C_IDENTIFIER "${query}" key)
    check(layout_${key} "atomic load order must be relaxed, acquire, or seq_cst"
        "$::static_assert(${query}((u32)$::atomic_load(&object, $::memory::release)) > 0uptr, \"invalid order\");")
    check(layout_arity_${key} "atomic_load requires 2 arguments"
        "$::static_assert(${query}((u32)$::atomic_load()) > 0uptr, \"invalid count\");")
endforeach()
check(unselected "atomic load order must be relaxed, acquire, or seq_cst"
    "$::static_assert(1u32 || $::atomic_load(&object, $::memory::release), \"invalid source\");")
check(expander "atomic_load requires 2 arguments" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) $::atomic_load(); return $::quote { 1u32 };
}
global u32 entry() { return 1u32; }
]=])
check(generic "requires a pointer to an atomic-qualified scalar" [=[
static $::meta::tokens helper<T>(in T argument, in $::meta::tokens input) {
    if (0u32) $::atomic_load(argument, $::memory::relaxed); return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(1u32, input); }
global u32 entry() { return apply!(1u32); }
]=])
foreach(success relaxed acquire release acq_rel seq_cst)
    foreach(failure relaxed acquire release acq_rel seq_cst)
        set(expected "failure order is invalid or stronger than success")
        if(failure STREQUAL relaxed OR
           (failure STREQUAL acquire AND success MATCHES "^(acquire|acq_rel|seq_cst)$") OR
           (failure STREQUAL seq_cst AND success STREQUAL seq_cst))
            set(expected pass)
        endif()
        check(order_${success}_${failure} "${expected}"
            "static $::meta::tokens helper(in $::meta::tokens input) {
                if (0u32) $::atomic_compare_exchange(&object, &plain, 1u32, $::memory::${success}, $::memory::${failure});
                return input;
            }
            global u32 entry() { return 1u32; }")
    endforeach()
endforeach()
check(valid_types pass [=[
$::static_assert(sizeof($::atomic_load(&narrow, $::memory::relaxed)) == sizeof(u16), "unqualified narrow value");
$::static_assert(sizeof($::atomic_exchange(&pointer, (u32 *)0uptr, $::memory::relaxed)) == sizeof(u32 *), "pointer value");
$::static_assert(sizeof($::atomic_compare_exchange(&object, &watched, 1u32, $::memory::seq_cst, $::memory::acquire)) == sizeof(bool), "bool result");
$::static_assert(sizeof($::atomic_is_lock_free(u32)) == sizeof(bool), "type query result");
$::static_assert(sizeof($::atomic_is_lock_free(value())) == sizeof(bool), "expression query result");
$::static_assert(1u32 || $::atomic_load(&object, $::memory::relaxed), "valid unselected access");
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        $::atomic_load(&readonly, $::memory::acquire);
        $::atomic_store(&pointer, &plain, $::memory::release);
        $::atomic_store(&pointer, sizeof(uptr) - sizeof(uptr), $::memory::seq_cst);
        $::atomic_exchange(&real, 1u32, ($::memory::relaxed));
        $::atomic_compare_exchange(&object, &watched, 1.5f32, $::memory::acq_rel, $::memory::acquire);
        $::atomic_thread_fence($::memory::seq_cst);
        $::atomic_signal_fence($::memory::relaxed);
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32); }
]=])
check(context_zero pass [=[
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::atomic_store(&pointer, $::eval(count($::quote {})), $::memory::relaxed);
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32) + apply!(2u32); }
]=])
check(reached "atomic operations are not permitted during translation-time evaluation" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::atomic_signal_fence($::memory::seq_cst); return input;
}
global u32 entry() { return apply!(1u32); }
]=])
set(capture [=[
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote { 1u32 }; }
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Drop : expression { prefix "drop"; match "(" value:expr ")"; expand drop; }
syntax Keep : expression { prefix "keep"; match "(" value:expr ")"; expand keep; }
syntax Drop, Keep;
]=])
check(discard pass "${capture}
$::static_assert(drop($::atomic_load()) == 1u32, \"discarded arity\");
$::static_assert(drop($::atomic_load(&object, $::memory::release)) == 1u32, \"discarded order\");")
check(survival "atomic load order must be relaxed, acquire, or seq_cst" "${capture}
$::static_assert(keep(sizeof($::atomic_load(&object, $::memory::release))) > 0uptr, \"surviving order\");")

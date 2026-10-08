# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODE MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags)
set(profiles baseline)
set(other_widths 1)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
elseif(MODE MATCHES "^mips")
    list(APPEND flags -target "${MODE}-unknown-linux-gnu")
    if(NOT MODE MATCHES "^mips64")
        list(APPEND flags -march=vr4300)
    endif()
    set(profiles llsc no_llsc)
    set(other_widths 0)
endif()
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(profile IN LISTS profiles)
        set(active_flags ${flags} -DEXPECT_OTHER=${other_widths} -DEXPECT32=1)
        if(profile STREQUAL llsc)
            list(APPEND active_flags -mllsc)
        elseif(profile STREQUAL no_llsc)
            set(active_flags ${flags} -mno-llsc -DEXPECT_OTHER=0 -DEXPECT32=0)
        endif()
        foreach(level O0 O2)
            execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${active_flags}
                "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${profile}-${level}.s"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
            if(expected STREQUAL pass)
                if(NOT status EQUAL 0)
                    message(FATAL_ERROR "${name}/${profile}/${level}: unexpected rejection\n${out}\n${err}")
                endif()
            elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
                   NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
                message(FATAL_ERROR "${name}/${profile}/${level}: missing located '${expected}'\n${out}\n${err}")
            endif()
        endforeach()
    endforeach()
endfunction()
check(values pass [=[
namespace types { typedef u32 Word; }
typedef u32 *Pointer;
global u32 [[atomic]] object;
global volatile u32 watched;
[[runtime_only]] static u32 next() { return 3u32; }
[[eval_only]] static u32 poison() { return 1u32 / 0u32; }
static bool query<T>() { return $::atomic_is_lock_free(T); }
static bool pointer<T>() { return $::atomic_is_lock_free(T *); }
enum Answer [[underlying(u32)]] { query_value = $::atomic_is_lock_free(types::Word) };
$::static_assert(query_value == EXPECT32, "enumerator");
$::static_assert($::atomic_is_lock_free(enum Answer) == EXPECT32, "nominal scalar");
$::static_assert($::atomic_is_lock_free(u32) == EXPECT32, "selected feature");
$::static_assert($::atomic_is_lock_free(const u32 [[atomic]]) == EXPECT32, "qualified type");
$::static_assert($::atomic_is_lock_free(f32) == EXPECT32, "floating type");
$::static_assert($::atomic_is_lock_free(bool) == EXPECT_OTHER, "bool width");
$::static_assert($::atomic_is_lock_free(u8) == EXPECT_OTHER, "narrow width");
$::static_assert($::atomic_is_lock_free(u16) == EXPECT_OTHER, "short width");
$::static_assert($::atomic_is_lock_free(u64) == EXPECT_OTHER, "wide width");
$::static_assert($::atomic_is_lock_free(fptr) == $::atomic_is_lock_free(uptr), "address-sized floating");
$::static_assert(!$::atomic_is_lock_free(u128), "unsupported width");
$::static_assert(!$::atomic_is_lock_free(f80), "extended storage");
$::static_assert(!$::atomic_is_lock_free(label), "non-atomic scalar");
$::static_assert($::atomic_is_lock_free(Pointer) ==
    (sizeof(uptr) == 4uptr ? EXPECT32 : EXPECT_OTHER), "selected address model");
$::static_assert(query<types::Word>() == EXPECT32, "generic query");
$::static_assert(pointer<u8>() == $::atomic_is_lock_free(Pointer), "generic pointer");
$::static_assert($::atomic_is_lock_free(object) == EXPECT32, "no atomic read");
$::static_assert($::atomic_is_lock_free(watched) == EXPECT32, "no volatile read");
$::static_assert($::atomic_is_lock_free(next()) == EXPECT32, "no runtime call");
$::static_assert($::atomic_is_lock_free(poison()) == EXPECT32, "no eval-only call");
$::static_assert($::atomic_is_lock_free($::eval(poison())) == EXPECT32, "no nested required call");
$::static_assert($::atomic_is_lock_free((u32)1 / 0u32) == EXPECT32, "no division");
$::static_assert($::atomic_is_lock_free($::atomic_load(&object, $::memory::relaxed)) == EXPECT32, "no atomic call");
$::static_assert($::atomic_is_lock_free(u32 (*)(in u32)) == $::atomic_is_lock_free(Pointer), "function pointer");
static $::meta::tokens helper(in $::meta::tokens input) {
    $::static_assert(query<types::Word>() == EXPECT32, "helper query");
    u32 *zero = $::atomic_is_lock_free(u128);
    if (zero != 0uptr) return $::quote { 0u32 };
    if ($::atomic_is_lock_free(next()) == EXPECT32) return input;
    return $::quote { 0u32 };
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Keep : expression { prefix "keep"; match "(" value:expr ")"; expand keep; }
syntax Keep;
$::static_assert(keep($::atomic_is_lock_free(types::Word)) == EXPECT32, "public type query");
$::static_assert(keep($::atomic_is_lock_free(next())) == EXPECT32, "public expression query");
typedef u32 QueryPrefix;
[[syntax_expander]] static $::meta::tokens wide(in $::meta::syntax_match input) {
    return $::quote { 1u128 };
}
syntax Prefix : expression { prefix "QueryPrefix"; match "*"; expand wide; }
syntax Prefix;
$::static_assert(!$::atomic_is_lock_free(QueryPrefix *), "active expression prefix wins");
global u32 entry() {
    $::assume($::atomic_is_lock_free(next()));
    if ($::atomic_is_lock_free(poison()) != EXPECT32) return 1u32;
    if ($::atomic_is_lock_free($::eval(poison())) != EXPECT32) return 2u32;
    u32 local = 0u32;
    if ($::atomic_is_lock_free(local + poison()) != EXPECT32) return 3u32;
    {
        u64 local = 0u64;
        if ($::atomic_is_lock_free(local + poison()) != EXPECT_OTHER) return 4u32;
        if (sizeof(local + poison()) != sizeof(u64)) return 6u32;
    }
    if (sizeof(local + poison()) != sizeof(u32)) return 5u32;
    return apply!(61u32);
}
]=])
foreach(operand "void" "u32[2]" "u32(in u32)" "$::meta::tokens" "struct Pair")
    string(MAKE_C_IDENTIFIER "${operand}" key)
    check(invalid_type_${key} "requires a scalar type or expression"
        "struct Pair { u32 value; }; static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::atomic_is_lock_free(${operand}); return input; }")
endforeach()
check(array_no_decay "requires a scalar type or expression" [=[
global u32 array[2];
global bool entry() { return $::atomic_is_lock_free(array); }
]=])
check(function_no_decay "requires a scalar type or expression" [=[
static u32 value() { return 1u32; }
$::static_assert($::atomic_is_lock_free(value), "no decay");
]=])
check(invalid_nested "atomic load order must be relaxed, acquire, or seq_cst" [=[
global u32 [[atomic]] object;
$::static_assert(1u32 || $::atomic_is_lock_free($::atomic_load(&object, $::memory::release)), "still checked");
]=])
check(generic_invalid "requires a scalar type or expression" [=[
struct Pair { u32 value; };
static bool query<T>() { return $::atomic_is_lock_free(T); }
$::static_assert(query<struct Pair>(), "invalid instance");
]=])
check(future_local "unresolved name 'later'" [=[
global bool entry() {
    bool answer = $::atomic_is_lock_free(later);
    u32 later = 1u32;
    return answer;
}
]=])
check(discarded pass [=[
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote { 1u32 }; }
syntax Drop : expression { prefix "drop"; match "(" value:expr ")"; expand drop; }
syntax Drop;
$::static_assert(drop($::atomic_is_lock_free()) == 1u32, "discarded arity");
$::static_assert(drop($::atomic_is_lock_free(u32[2])) == 1u32, "discarded type");
]=])
check(expression_not_type "unresolved name 'u32'" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { u32 }; }
syntax Fake : expression { prefix "fake"; match "(" ")"; expand expand; }
syntax Fake;
global bool entry() { return $::atomic_is_lock_free(fake()); }
]=])

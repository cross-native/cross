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
struct Watched { volatile u32 value; };
global u32 writable;
global volatile u32 observed;
global u32 [[atomic]] atomic_value;
global struct Pair held;
global struct Watched watched;
global volatile u32 array[2];
static u32 read() { return 1u32; }
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
function(reject_body name expected body)
    set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }")
    check(${name}_unused "${expected}" "${helper} global u32 entry() { return 1u32; }")
    check(${name}_called "${expected}" "${helper}
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }")
endfunction()
reject_body(trap_arity "trap takes no arguments" "$::trap(1u32);")
reject_body(unreachable_arity "unreachable takes no arguments" "$::unreachable(1u32);")
reject_body(expect_arity "expect requires two arguments" "$::expect(1u32);")
reject_body(expect_empty "expect requires two arguments" "$::expect();")
reject_body(expect_type "expect requires a boolean, integer, or enumeration value" "$::expect(1.0f64, 1u32);")
reject_body(expect_pointer "expect requires a boolean, integer, or enumeration value" "$::expect(&writable, 1u32);")
reject_body(expect_constant "runtime/static storage cannot be read" "$::expect(1u32, writable);")
reject_body(expect_constant_type "expect requires an integer constant expectation" "$::expect(1u32, 1.0f64);")
reject_body(expect_void "void expression cannot supply a value" "u32 value = $::trap();")
reject_body(assume_arity "assume requires one argument" "$::assume();")
reject_body(assume_type "assume requires a scalar condition" "$::assume(held);")
reject_body(assume_assignment "assume condition must be side-effect-free" "$::assume(writable = 1u32);")
reject_body(assume_update "assume condition must be side-effect-free" "$::assume(++writable);")
reject_body(assume_call "assume condition must be side-effect-free" "$::assume(read());")
foreach(expression observed atomic_value watched.value "array[0uptr]" "*(&observed)" "&array[observed]")
    string(MD5 key "${expression}")
    reject_body(assume_qualified_${key} "assume requires a non-volatile, non-atomic condition" "$::assume(${expression});")
endforeach()
foreach(query sizeof "$::alignof")
    string(MAKE_C_IDENTIFIER "${query}" key)
    check(query_arity_${key} "expect requires two arguments"
        "$::static_assert(${query}((u32)$::expect(1u32)) > 0uptr, \"query\");")
    check(query_type_${key} "expect requires a boolean, integer, or enumeration value"
        "$::static_assert(${query}((u32)$::expect(1.0f64, 1u32)) > 0uptr, \"query\");")
    check(query_constant_${key} "runtime/static storage cannot be read"
        "$::static_assert(${query}($::expect(1u32, writable)) > 0uptr, \"query\");")
endforeach()
check(unselected "expect requires two arguments" "$::static_assert(1u32 || $::expect(1u32), \"unselected\");")
check(expander "trap takes no arguments" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) $::trap(1u32);
    return $::quote { 1u32 };
}
syntax Form : expression { prefix "form"; match "(" ")"; expand expand; }
syntax Form;
global u32 entry() { return form(); }
]=])
check(generic "expect requires a boolean, integer, or enumeration value" [=[
static T helper<T>(in $::meta::tokens input) { if (0u32) $::expect((T)1, 1u32); return (T)1; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { helper<f64>(input); return input; }
global u32 entry() { return apply!(1u32); }
]=])
check(valid pass [=[
$::static_assert(sizeof($::expect(writable, 1u32)) == sizeof(u32), "no runtime read");
$::static_assert(sizeof($::expect((u16)1, sizeof(uptr))) == sizeof(u16), "exact result type");
$::static_assert($::alignof($::expect(read(), 1u32 + 2u32)) == $::alignof(u32), "no call");
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        $::expect(writable, 1u32 + 2u32);
        $::assume(writable < 8u32);
        $::assume(held.value);
        $::assume(&observed != 0);
        $::assume(&array[writable] != 0);
        $::assume(sizeof(++writable) == sizeof(u32));
        $::assume(sizeof(observed) == sizeof(u32));
        $::assume($::alignof(atomic_value) == $::alignof(u32));
        $::trap(); $::unreachable();
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32); }
]=])
check(context_dependent pass [=[
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::expect(1u32, $::eval(count($::quote { token })));
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32) + apply!(2u32); }
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
$::static_assert(drop($::expect(1u32)) == 1u32, \"discarded arity\");
$::static_assert(drop($::assume(++writable)) == 1u32, \"discarded effect\");
global u32 entry() { return 1u32; }")
check(survival "expect requires two arguments" "${capture}
$::static_assert(keep(sizeof($::expect(1u32))) > 0uptr, \"surviving intrinsic\");")

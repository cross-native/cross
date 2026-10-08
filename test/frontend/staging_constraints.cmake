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
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
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
foreach(operation eval runtime)
    foreach(count 0 2)
        if(count EQUAL 0)
            set(arguments "")
        else()
            set(arguments "1u32, 2u32")
        endif()
        set(call "$::${operation}(${arguments})")
        set(expected "${operation} requires exactly one expression")
        set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) ${call}; return input; }")
        check(${operation}_${count}_unused "${expected}" "${helper} global u32 entry() { return 1u32; }")
        check(${operation}_${count}_invoked "${expected}" "${helper}
            [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
            global u32 entry() { return apply!(1u32); }")
        check(${operation}_${count}_layout "${expected}"
            "$::static_assert(sizeof((u32)${call}) == sizeof(u32), \"invalid operand\");")
        check(${operation}_${count}_unselected "${expected}"
            "$::static_assert(1u32 || (u32)${call}, \"unselected operand\");")
        check(${operation}_${count}_generic "${expected}"
            "static u32 helper<T>() { if (0u32) ${call}; return 1u32; }
             $::static_assert(helper<u16>(), \"instantiated body\");")
        check(${operation}_${count}_expander "${expected}"
            "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
                if (0u32) ${call}; return $::quote { 1u32 }; }
             syntax Bad : expression { prefix \"bad\"; match \"(\" \")\"; expand expand; }")
    endforeach()
endforeach()
check(runtime_meta "meta values cannot enter runtime expressions" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::runtime(input); return input;
}
]=])
check(runtime_meta_layout "meta values cannot enter runtime expressions" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    uptr size = sizeof((u32)$::runtime(input)); return input;
}
]=])
check(valid_types pass [=[
[[runtime_only]] static u16 runtime() { return 9u16; }
[[eval_only]] static u16 constant() { return 7u16; }
static void empty() {}
$::static_assert(sizeof($::eval(runtime())) == sizeof(u16), "unevaluated eval");
$::static_assert(sizeof($::runtime(runtime())) == sizeof(u16), "unevaluated runtime");
$::static_assert($::alignof($::eval(runtime())) == $::alignof(u16), "exact source type");
$::static_assert(1u32 || runtime(), "well-typed unselected runtime-only call");
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::runtime(runtime());
    $::eval(empty());
    return $::eval(input);
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32) + $::eval(constant()) + $::runtime(runtime()); }
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
    $::static_assert(drop($::eval()) == 1u32, \"discarded eval arity\");
    $::static_assert(drop($::runtime(1u32, 2u32)) == 1u32, \"discarded runtime arity\");")
check(survival "eval requires exactly one expression" "${capture}
    $::static_assert(keep(sizeof((u32)$::eval())) == sizeof(u32), \"surviving arity\");")

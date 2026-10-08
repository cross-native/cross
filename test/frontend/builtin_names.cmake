# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC CPP OUTPUT MODE MODEL)
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
foreach(name "$::missing" "$::target::missing" "$::meta::missing" "$::syntax::missing")
    string(MAKE_C_IDENTIFIER "${name}" key)
    foreach(query sizeof "$::alignof")
        string(MAKE_C_IDENTIFIER "${query}" query_key)
        check(${key}_${query_key} "unknown compiler builtin"
            "$::static_assert(${query}((u32)${name}()) > 0uptr, \"unknown call\");")
    endforeach()
    check(${key}_unselected "unknown compiler builtin"
        "$::static_assert(1u32 || (u32)${name}(), \"unknown unselected call\");")
    foreach(role helper macro syntax_expander)
        set(attribute "")
        set(type "$::meta::tokens")
        set(result "input")
        if(NOT role STREQUAL helper)
            set(attribute "[[${role}]]")
        endif()
        if(role STREQUAL syntax_expander)
            set(type "$::meta::syntax_match")
            set(result "$::quote { 1u32 }")
        endif()
        check(${key}_${role} "unknown compiler builtin"
            "${attribute} static $::meta::tokens apply(in ${type} input) {
                if (0u32) { ${name}(); } return ${result};
            }
            global u32 entry() { return 1u32; }")
    endforeach()
endforeach()
check(unknown_value "unknown compiler builtin" [=[
$::static_assert(sizeof((u32)$::memory::missing) == sizeof(u32), "unknown constant");
]=])
check(unknown_generic "unknown compiler builtin" [=[
static $::meta::tokens helper<T>(in T value, in $::meta::tokens input) {
    if (0u32) { $::missing(); } return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(1u32, input); }
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
$::static_assert(drop($::missing()) == 1u32, \"discarded call\");
$::static_assert(drop($::memory::missing) == 1u32, \"discarded value\");
global u32 entry() { return 1u32; }")
check(survival "unknown compiler builtin" "${capture}
$::static_assert(keep(sizeof((u32)$::missing())) > 0uptr, \"surviving call\");")
check(quoted pass [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::meta::tokens raw = $::quote { $::missing(); $::memory::missing; };
    if ($::meta::len(raw) == 0uptr) return $::quote { 0u32 };
    return input;
}
global u32 entry() { return apply!(1u32); }
]=])
check(known_operations pass [=[
global u32 [[atomic]] shared;
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        $::atomic_thread_fence($::memory::seq_cst);
        $::atomic_signal_fence($::memory::acquire);
        $::atomic_load(&shared, $::memory::relaxed);
        $::expect(1u32, 1u32); $::assume(1u32);
        $::trap(); $::unreachable();
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() {
#if $::has_instruction($::_nop)
    $::_nop();
#endif
    return apply!(1u32);
}
]=])

# Every listed core operation/constant must agree with both query paths.
# Machine names are deliberately tested through the selected target instead.
execute_process(COMMAND "${CC}" ${flags} --print-builtins
    RESULT_VARIABLE status OUTPUT_VARIABLE listed ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "builtin inspection failed\n${listed}\n${err}")
endif()
string(REPLACE "\n" ";" lines "${listed}")
set(queries "")
foreach(line IN LISTS lines)
    string(STRIP "${line}" line)
    string(REGEX MATCH "^([$]::[a-zA-Z_][a-zA-Z_0-9:]*) (.+)$" entry "${line}")
    if(NOT entry)
        continue()
    endif()
    set(name "${CMAKE_MATCH_1}")
    set(description "${CMAKE_MATCH_2}")
    if(description MATCHES "instruction|macro" OR description STREQUAL "query")
        continue()
    endif()
    if(description STREQUAL "atomic-order constant")
        set(is_intrinsic 0)
    else()
        set(is_intrinsic 1)
    endif()
    string(APPEND queries "#if !$::has_builtin(${name}) || $::has_intrinsic(${name}) != ${is_intrinsic}\n#error builtin registry mismatch ${name}\n#endif\n")
endforeach()
foreach(name "$::embed" "$::meta::len" "$::meta::at" "$::meta::slice" "$::meta::data" "$::meta::alloc" "$::meta::cap" "$::meta::freeze" "$::syntax::context")
    string(FIND "${listed}" "${name} " found)
    if(found EQUAL -1)
        message(FATAL_ERROR "implemented intrinsic missing from inspection: ${name}")
    endif()
endforeach()
string(APPEND queries [=[
#if $::has_builtin($::missing) || $::has_intrinsic($::missing)
#error unknown operation was advertised
#endif
#if $::has_instruction($::_nop) != $::has_builtin($::_nop) || $::has_intrinsic($::_nop)
#error instruction category must remain target-owned
#endif
global u32 entry() { return 1u32; }
]=])
check(queries pass "${queries}")
execute_process(COMMAND "${CPP}" ${flags} "${OUTPUT}/queries.x" -o "${OUTPUT}/queries.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "standalone cpp registry mismatch\n${out}\n${err}")
endif()

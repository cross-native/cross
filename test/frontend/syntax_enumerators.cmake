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
        foreach(folding normal noeval)
            set(optional)
            if(folding STREQUAL noeval)
                set(optional -fno-eval-calls)
            endif()
            execute_process(COMMAND "${CC}" ${flags} -S -${level} ${optional} ${ARGN}
                "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}-${folding}.s"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
            if(expected STREQUAL pass)
                if(NOT status EQUAL 0)
                    message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: unexpected rejection\n${out}\n${err}")
                endif()
            elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
                   NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
                message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: status ${status}, missing '${expected}'\n${out}\n${err}")
            endif()
            if(expected MATCHES "budget")
                string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" errors "${err}")
                list(LENGTH errors count)
                if(NOT count EQUAL 1)
                    message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: resource failure cascaded\n${err}")
                endif()
                if(NOT err MATCHES "while evaluating call to 'Numbers::next'" OR
                   NOT err MATCHES "while evaluating call to 'expand'" OR
                   (name MATCHES "^syntax_" AND NOT err MATCHES "syntax 'Value' defined here"))
                    message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: enum failure lost expansion ancestry\n${err}")
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()
set(chain "namespace Numbers { static u32 next(in u32 value) { u32 work = 0u32; for (u32 i = 0u32; i < 100u32; ++i) ++work; return value + work - 99u32; }\n")
foreach(index RANGE 0 240)
    if(index EQUAL 0)
        set(input "0u32")
    else()
        math(EXPR previous "${index} - 1")
        set(input "(u32)value${previous}")
    endif()
    string(APPEND chain "enum Number${index} [[underlying(u32)]] { value${index} = next(${input}) };\n")
    if(index EQUAL 64)
        set(short_chain "${chain}}\n")
    endif()
endforeach()
set(observer "[[macro]] static $::meta::tokens observe(in $::meta::tokens input) { if ((u32)result != 10u32) return $::quote { unexpected }; return $::quote { 42u32 }; }\nglobal u32 entry() { return observe!(); }\n")
check(same_declaration pass "static u32 identity(in u32 value) { return value; }\nenum Values [[underlying(u8)]] { first = identity(7u32), second = identity((u32)first + 2u32), result };\n${observer}")
check(value_range "not representable" "enum Values [[underlying(u8)]] { result = 256u32 };\n${observer}")
check(implicit_range "implicit enumerator value is not representable" "enum Values [[underlying(u8)]] { first = 255u32, result };\n${observer}")
check(forward_dependency "not a translation-time value" "static u32 identity(in u32 value) { return value; }\nenum First { result = identity((u32)later) };\nenum Later { later = identity(10u32) };\n${observer}")
check(static_read "runtime/static storage cannot be read" "static u32 runtime_value;\nstatic u32 read() { return runtime_value; }\nenum Values { result = read() };\n${observer}")
check(declaration_context pass [=[
namespace Source {
    static u32 identity(in u32 value) { return value; }
    enum Base { seed = identity(9u32) };
    enum Result { result = identity((u32)seed + 1u32) };
}
[[macro]] static $::meta::tokens observe(in $::meta::tokens input) {
    u32 seed = 999u32;
    if ((u32)Source::result != 10u32 || seed != 999u32) return $::quote { unexpected };
    return $::quote { 42u32 };
}
global u32 entry() { return observe!(); }
]=])
check(invocation_context "meta operation requires an active expansion context"
    "enum Values { result = (u32)$::meta::len($::quote { a b c d e f g h i j }) };\n${observer}")
check(unselected_context pass
    "enum Values { result = 0u32 ? (u32)$::meta::len($::quote { requires_context }) : 10u32 };\n${observer}")
set(word "enum Word [[underlying(uptr)]] { result = 1u128 << 32u32 };\n${observer}")
if(MODE STREQUAL mips OR MODE STREQUAL mipsel)
    check(pointer_width "not representable" "${word}")
else()
    string(REPLACE "10u32" "0u32" word "${word}")
    check(pointer_width pass "${word}")
endif()
string(APPEND chain "}\n")
foreach(role macro syntax)
    if(role STREQUAL macro)
        set(signature "[[macro]] static $::meta::tokens expand(in $::meta::tokens input)")
        set(activation "")
        set(invocation "expand!()")
    else()
        set(signature "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input)")
        set(activation "syntax Value : expression { prefix \"value\"; match body:paren; expand expand; }\nsyntax Value;\n")
        set(invocation "value()")
    endif()
    set(source "${chain}${signature} { if ((u32)Numbers::value240 != 241u32) return $::quote { unexpected }; return $::quote { 42u32 }; }\n${activation}global u32 entry() { return ${invocation}; }\n$::static_assert(entry() == 42u32, \"enum chain lost its value\");\n")
    # Each initializer is cheap alone. An active expansion pays for all of them,
    # rather than resetting its work accounting at each demand-driven enum edge.
    set(work_source "${short_chain}${signature} { if ((u32)Numbers::value64 != 65u32) return $::quote { unexpected }; return $::quote { 42u32 }; }\n${activation}global u32 entry() { return ${invocation}; }\n")
    check(${role}_work "instruction budget exceeded 10000" "${work_source}" -feval-step-limit=10000)
    # Preparing each enum can itself normalize a value argument. That proof is
    # part of the active expansion too, even with no invocation-owned caller.
    string(REPLACE " = next(" " = constant<(next(" generic_source "${work_source}")
    string(REPLACE ") };" "))>() };" generic_source "${generic_source}")
    set(generic_source "static u32 constant<u32 N>() { return N; }\n${generic_source}")
    check(${role}_generic_work "instruction budget exceeded 10000" "${generic_source}" -feval-step-limit=10000)
    check(${role}_generic_ample pass "${generic_source}")
    check(${role}_deep pass "${source}" -feval-depth-limit=512)
endforeach()

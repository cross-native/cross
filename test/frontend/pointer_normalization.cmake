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
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 45)
            if(expected STREQUAL pass)
                if(NOT status EQUAL 0)
                    message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: unexpected rejection\n${out}\n${err}")
                endif()
            elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
                   NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
                message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: missing '${expected}'\n${out}\n${err}")
            endif()
            if(expected MATCHES "budget")
                string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" errors "${err}")
                list(LENGTH errors count)
                if(NOT count EQUAL 1)
                    message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: resource failure cascaded\n${err}")
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()
set(symbolic "&items[2]")
set(absolute "(u8*)0x1000uptr")
foreach(index RANGE 1 240)
    set(symbolic "(${symbolic} + 0uptr)")
    set(absolute "(${absolute} + 0uptr)")
endforeach()
check(symbolic pass "
    global u32 items[4];
    static u32 *selected() { return ${symbolic}; }
    static uptr width(in u32 *value) { return sizeof(*value); }
    static u32 read<u32 *Pointer>() { return *Pointer; }
    [[macro]] static $::meta::tokens expand(in $::meta::tokens input) {
        if (width(selected()) != sizeof(u32)) return $::quote { unexpected };
        return $::quote { 9u32 };
    }
    global u32 entry() { return expand!() + read<selected()>(); }")
check(absolute pass "
    static u8 *selected() { return ${absolute}; }
    static uptr value() { return (uptr)selected(); }
    $::static_assert(value() == 0x1000uptr, \"absolute address changed\");
    global uptr entry() { return (uptr)selected(); }")
set(work [=[
    static u32 work() {
        for (u32 i = 0u32; i < 500u32; ++i) { u32 *pointer = 0u32; }
        return 42u32;
    }
    [[macro]] static $::meta::tokens expand(in $::meta::tokens input) {
        if (work() != 42u32) return $::quote { unexpected };
        return $::quote { 42u32 };
    }
    global u32 entry() { return expand!(); }
]=])
check(work "instruction budget exceeded 1000" "${work}" -feval-step-limit=1000)
check(ample pass "${work}")

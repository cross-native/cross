# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT TARGET)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
if("${TARGET}" STREQUAL "x86_64")
    set(target_flags -target x86_64-unknown-linux-gnu)
else()
    set(target_flags -target mips-unknown-elf -march=vr4300 -mabi=o32)
endif()

function(compile name source)
    execute_process(COMMAND "${CC}" -S ${target_flags} -fno-eval-calls
        -finline-limit=0 ${ARGN} "${source}" -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${name}.s" text)
    set(assembly "${text}" PARENT_SCOPE)
endfunction()

set(removed once_loop once_group once_registered once_inner once_outer)
set(kept once_recursive once_cycle_a once_cycle_b once_addressed once_used
    once_retained once_forbidden once_exported once_alias_target once_label_owner
    once_patched once_leaf once_vla once_large_stack once_tail_owner once_tail_target once_clone_target)

foreach(mode O0 Og O1 O2 O3 Os Oz disabled-before disabled-after all-disabled independent explicit)
    set(enabled ON)
    if(mode STREQUAL "disabled-before")
        set(flags -fno-inline-functions-called-once -O2)
        set(enabled OFF)
    elseif(mode STREQUAL "disabled-after")
        set(flags -O2 -fno-inline-functions-called-once)
        set(enabled OFF)
    elseif(mode STREQUAL "all-disabled")
        set(flags -fno-inline -O2)
        set(enabled OFF)
    elseif(mode STREQUAL "independent")
        set(flags -fno-inline-functions -O2)
    elseif(mode STREQUAL "explicit")
        set(flags -finline-functions-called-once -O0)
    else()
        set(flags -${mode})
        if(mode STREQUAL "O0" OR mode STREQUAL "Og")
            set(enabled OFF)
        endif()
    endif()
    compile(${mode} "${SOURCE}" ${flags})
    foreach(symbol IN LISTS kept)
        if(NOT assembly MATCHES "${symbol}:\r?\n")
            message(FATAL_ERROR "${mode} removed protected ${symbol}")
        endif()
    endforeach()
    foreach(symbol IN LISTS removed)
        if(enabled AND assembly MATCHES "${symbol}:\r?\n")
            message(FATAL_ERROR "${mode} kept single-call ${symbol}")
        elseif(NOT enabled AND NOT assembly MATCHES "${symbol}:\r?\n")
            message(FATAL_ERROR "${mode} ignored disabled control for ${symbol}")
        endif()
    endforeach()
    if(mode STREQUAL "O3" AND "${TARGET}" STREQUAL "x86_64" AND
       NOT assembly MATCHES "once_clone_target[.]const[.]")
        message(FATAL_ERROR "specialization did not compose with called-once inlining")
    endif()
    if(assembly MATCHES "once_wrapper:\r?\n")
        message(FATAL_ERROR "${mode} kept mandatory wrapper")
    endif()
endforeach()

# The bound has both an absolute threshold and a relative growth limit.
# A single huge callee may inline; accumulating many bodies must stop.
string(REPEAT "    value = value * 33u32 + input;\n" 320 operations)
set(body "{ volatile u32 value = input;\n${operations}    return value; }\n")
set(growth "")
foreach(i RANGE 1 5)
    string(APPEND growth "static u32 growth_${i}(in u32 input) ${body}")
endforeach()
string(APPEND growth "global u32 growth_entry(in u32 input) { return growth_1(input) + growth_2(input) + growth_3(input) + growth_4(input) + growth_5(input); }\n")
file(WRITE "${OUTPUT}-growth.x" "${growth}")
compile(growth "${OUTPUT}-growth.x" -O2)
set(present 0)
foreach(i RANGE 1 5)
    if(assembly MATCHES "growth_${i}:\r?\n")
        math(EXPR present "${present} + 1")
    endif()
endforeach()
if(present EQUAL 0 OR present EQUAL 5)
    message(FATAL_ERROR "large-function growth did not allow then bound inlining (${present})")
endif()
string(REPEAT "    value = value * 33u32 + input;\n" 960 huge_operations)
file(WRITE "${OUTPUT}-huge.x"
    "static u32 huge(in u32 input) { volatile u32 value = input;\n${huge_operations} return value; }\nglobal u32 huge_entry(in u32 input) { return huge(input); }\n")
compile(huge "${OUTPUT}-huge.x" -O2)
if(assembly MATCHES "_huge:\r?\n")
    message(FATAL_ERROR "large-function threshold became an absolute callee limit")
endif()

# The stack baseline includes callers, not the new callee's own large frame.
file(WRITE "${OUTPUT}-stack.x" [=[
static u32 stack_callee(in u32 input) {
    volatile u8 data[4096]; data[input & 4095u32] = (u8)input;
    return data[input & 4095u32];
}
global u32 stack_caller(in u32 input) {
    volatile u8 data[512]; data[input & 511u32] = (u8)input;
    return stack_callee(input) + data[input & 511u32];
}
]=])
compile(stack "${OUTPUT}-stack.x" -O2)
if(assembly MATCHES "_stack_callee:\r?\n")
    message(FATAL_ERROR "stack growth ignored the original caller's frame")
endif()

# Direct references across source units still form one compilation group.
file(WRITE "${OUTPUT}-group-caller.x" [=[
u32 across_units(in u32 value);
global u32 group_entry(in u32 value) { return across_units(value); }
]=])
file(WRITE "${OUTPUT}-group-callee.x" [=[
u32 across_units(in u32 value) { return value * 13u32 + 7u32; }
]=])
compile(group "${OUTPUT}-group-caller.x" -O2 "${OUTPUT}-group-callee.x")
if(assembly MATCHES "_across_units:\r?\n")
    message(FATAL_ERROR "a group-private callee in another unit was kept")
endif()

# A patch's initial relocation keeps the target's canonical body. The MIPS
# patch materializer currently does not support symbolic initial values.
if("${TARGET}" STREQUAL "x86_64")
    file(WRITE "${OUTPUT}-roots.x" [=[
static u32 patch_address_target(in u32 value) { return value * 3u32; }
global uptr roots_entry(in u32 value) {
    return $::patch((uptr)&patch_address_target) + (uptr)patch_address_target(value);
}
]=])
    compile(roots "${OUTPUT}-roots.x" -O2)
    if(NOT assembly MATCHES "patch_address_target:\r?\n")
        message(FATAL_ERROR "removed canonical patch reference")
    endif()
endif()

file(WRITE "${OUTPUT}-model.xm" [=[
optimization "single-call" {
    inherits = "O0";
    f.inline-functions-called-once = true;
}
]=])
compile(model "${SOURCE}" --model=${OUTPUT}-model.xm -O=single-call)
foreach(symbol IN LISTS removed)
    if(assembly MATCHES "${symbol}:\r?\n")
        message(FATAL_ERROR "custom model did not enable called-once inlining")
    endif()
endforeach()

if("${TARGET}" STREQUAL "x86_64")
    file(WRITE "${OUTPUT}-manual.x" [=[
static u32 manual_once(in u32 value) -> "eax" { return value + 1u32; }
global u32 manual_entry(in u32 value) { return manual_once(value); }
]=])
    compile(manual "${OUTPUT}-manual.x" -O2)
    if(assembly MATCHES "_manual_once:\r?\n")
        message(FATAL_ERROR "manual result placement blocked called-once inlining")
    endif()
endif()

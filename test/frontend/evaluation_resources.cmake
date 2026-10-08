# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX OUTPUT MODE MODEL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags)
set(helper_attributes "[[noinline]]")
set(entry_attributes)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
    set(helper_attributes "[[noinline, abi(\"odd_abi\")]]")
elseif(MODE STREQUAL mips OR MODE STREQUAL mipsel)
    list(APPEND flags -target "${MODE}-unknown-elf" -mprofile=r3000-o32)
elseif(MODE STREQUAL mips64 OR MODE STREQUAL mips64el)
    list(APPEND flags -target "${MODE}-unknown-elf" -mabi=n64)
elseif(NOT MODE STREQUAL native)
    message(FATAL_ERROR "unknown profile ${MODE}")
endif()
if(MODE STREQUAL native OR MODE STREQUAL custom)
    # The C++ harness has an explicit platform ABI. Custom helpers retain their
    # explicit model ABI, including nonstandard result transport.
    if(WIN32)
        set(entry_attributes "[[abi(\"ms_abi\")]]")
    else()
        set(entry_attributes "[[abi(\"sysv_abi\")]]")
    endif()
endif()

function(compile_case source output expected)
    execute_process(COMMAND "${CC}" ${flags} -S ${ARGN} "${source}" -o "${output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(expected STREQUAL pass)
        if(NOT status EQUAL 0 OR NOT err STREQUAL "")
            message(FATAL_ERROR "${MODE}/${output}: unexpected diagnostic or rejection\n${out}\n${err}")
        endif()
    else()
        string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" errors "${err}")
        list(LENGTH errors count)
        if(NOT status EQUAL 1 OR NOT count EQUAL 1 OR NOT err MATCHES "${expected}")
            message(FATAL_ERROR "${MODE}/${output}: missing single '${expected}' failure\n${out}\n${err}")
        endif()
    endif()
endfunction()

function(check_resource name helpers expression callee expected)
    set(limit ${ARGN})
    set(source "${OUTPUT}/${name}.x")
    file(WRITE "${source}" "${helpers}\n${entry_attributes} global u32 resource_entry() { return (${expression}) == 8u32 ? 0u32 : 1u32; }\n")
    file(WRITE "${OUTPUT}/${name}-forced.x" "${helpers}\n${entry_attributes} global u32 resource_entry() { return $::eval(${expression}); }\n")
    file(WRITE "${OUTPUT}/${name}-static.x" "${helpers}\nglobal u32 resource_required = ${expression};\n")
    foreach(level O0 O2)
        foreach(folding normal noeval)
            set(optional)
            if(folding STREQUAL noeval)
                set(optional -fno-eval-calls)
            endif()
            set(output "${OUTPUT}/${name}-${level}-${folding}")
            compile_case("${source}" "${output}.s" pass -${level} -fno-inline ${optional} ${limit})
            if(level STREQUAL O0)
                file(READ "${output}.s" assembly)
                if(NOT assembly MATCHES "(call|jal)[^\r\n]*${callee}")
                    message(FATAL_ERROR "${MODE}/${name}/${folding}: exhausted optional evaluation lost its runtime call")
                endif()
            endif()
            foreach(required forced static)
                compile_case("${OUTPUT}/${name}-${required}.x" "${output}-${required}.s"
                    "${expected}" -${level} -fno-inline ${optional} ${limit})
            endforeach()
            if(MODE STREQUAL native OR MODE STREQUAL custom)
                execute_process(COMMAND "${CMAKE_COMMAND}" "-DCC=${CC}" "-DHOST_CXX=${HOST_CXX}"
                    "-DSOURCE=${source}" "-DOUTPUT=${output}-runtime"
                    "-DRUNNER=${CMAKE_CURRENT_LIST_DIR}/../support/native_entry.cpp"
                    -DENTRY=resource_entry -DEXPECTED=0
                    "-DCC_FLAGS=${flags};-${level};-fno-inline;${optional};${limit}"
                    -P "${CMAKE_CURRENT_LIST_DIR}/../support/run_native.cmake"
                    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
                if(NOT status EQUAL 0)
                    message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: runtime fallback failed\n${out}\n${err}")
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()

set(add "${helper_attributes} global u32 resource_add(in u32 value) { return value + 1u32; }")
check_resource(steps "${add}" "resource_add(7u32)" resource_add
    "instruction budget exceeded 1" -feval-step-limit=1)
set(storage "struct ResourceStorage { u32 value; }; ${helper_attributes} global u32 resource_storage(in u32 value) { struct ResourceStorage object = { value }; return object.value + 1u32; }")
check_resource(memory "${storage}" "resource_storage(7u32)" resource_storage
    "meta memory budget exceeded 1 bytes" -feval-memory-limit=1)
check_resource(bytes "${storage}" "resource_storage(7u32)" resource_storage
    "translation-time object exceeds target layout or byte budget" -feval-byte-limit=1)
set(recursion "${helper_attributes} global u32 resource_recursive(in u32 value) { if (value == 0u32) return 5u32; return resource_recursive(value - 1u32) + 1u32; }")
check_resource(depth "${recursion}" "resource_recursive(3u32)" resource_recursive
    "recursion depth exceeded 1" -feval-depth-limit=1)

# Automatic bounds are required proofs even when optional call folding is off.
file(WRITE "${OUTPUT}/automatic.x" "static u32 bound() { return 4u32; } static void local() { u8 value[bound()]; } global u32 entry() { return 0u32; }\n")
foreach(level O0 O2)
    foreach(folding normal noeval)
        set(optional)
        if(folding STREQUAL noeval)
            set(optional -fno-eval-calls)
        endif()
        compile_case("${OUTPUT}/automatic.x" "${OUTPUT}/automatic-${level}-${folding}.s"
            "instruction budget exceeded 1" -${level} ${optional} -feval-step-limit=1)
    endforeach()
endforeach()

if(MODE STREQUAL custom)
    # All three transports must survive a failed optional attempt. This checks
    # actual executable calls, not just source callable-type identities.
    set(helpers [=[
struct ResourcePair { u32 first; u32 second; };
[[noinline, abi("odd_abi")]] global u32 resource_register(in u32 value) { return value + 1u32; }
[[noinline, abi("stack_result_abi")]] global u32 resource_stack(in u32 value) { return value + 2u32; }
[[noinline, abi("memory_result_abi")]] global struct ResourcePair resource_memory(in u32 value) { struct ResourcePair result = { value, value + 3u32 }; return result; }
]=])
    file(WRITE "${OUTPUT}/transports.x" "${helpers}\n${entry_attributes} global u32 resource_entry() { u32 registered = resource_register(7u32); u32 stacked = resource_stack(7u32); struct ResourcePair memory = resource_memory(7u32); return registered == 8u32 && stacked == 9u32 && memory.first == 7u32 && memory.second == 10u32 ? 0u32 : 1u32; }\n")
    foreach(level O0 O2)
        foreach(folding normal noeval)
            set(optional)
            if(folding STREQUAL noeval)
                set(optional -fno-eval-calls)
            endif()
            set(output "${OUTPUT}/transports-${level}-${folding}")
            compile_case("${OUTPUT}/transports.x" "${output}.s" pass
                -${level} -fno-inline ${optional} -feval-step-limit=1)
            if(level STREQUAL O0)
                file(READ "${output}.s" assembly)
                foreach(callee resource_register resource_stack resource_memory)
                    if(NOT assembly MATCHES "call[^\r\n]*${callee}")
                        message(FATAL_ERROR "${MODE}/${level}/${folding}: custom ${callee} transport was erased")
                    endif()
                endforeach()
            endif()
            execute_process(COMMAND "${CMAKE_COMMAND}" "-DCC=${CC}" "-DHOST_CXX=${HOST_CXX}"
                "-DSOURCE=${OUTPUT}/transports.x" "-DOUTPUT=${output}-runtime"
                "-DRUNNER=${CMAKE_CURRENT_LIST_DIR}/../support/native_entry.cpp"
                -DENTRY=resource_entry -DEXPECTED=0
                "-DCC_FLAGS=${flags};-${level};-fno-inline;${optional};-feval-step-limit=1"
                -P "${CMAKE_CURRENT_LIST_DIR}/../support/run_native.cmake"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${MODE}/${level}/${folding}: custom result runtime fallback failed\n${out}\n${err}")
            endif()
        endforeach()
    endforeach()
endif()

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
elseif(MODE STREQUAL mips)
    list(APPEND flags -target mips-unknown-linux-gnu -mprofile=r3000-o32)
elseif(MODE STREQUAL mipsel)
    list(APPEND flags -target mipsel-unknown-linux-gnu -mprofile=r3000-o32)
elseif(MODE STREQUAL mips64)
    list(APPEND flags -target mips64-unknown-linux-gnu -mabi=n64)
elseif(MODE STREQUAL mips64el)
    list(APPEND flags -target mips64el-unknown-linux-gnu -mabi=n64)
elseif(NOT MODE STREQUAL native)
    message(FATAL_ERROR "unknown mode ${MODE}")
endif()

# Record layout may depend on an earlier helper-local alias-expanded object.
# Exercise semantic preparation and its required-value bridge together, while
# keeping these cases independent of runtime ABI transport and host layout.
set(source "[[eval_only]] static uptr local_shape<T>() { typedef T A0;\n")
foreach(index RANGE 0 47)
    math(EXPR next "${index} + 1")
    string(APPEND source "typedef A${index} A${next}[1];\n")
endforeach()
string(APPEND source "A48 value = {};\nstruct Local { u8 bytes[sizeof(value)]; };\nstruct Local object = {};\nreturn sizeof(object);\n}\n")
set(local_helper "${source}")
string(APPEND source "$::static_assert($::eval(local_shape<uptr>()) == sizeof(uptr), \"local model extent\");\n$::static_assert($::eval(local_shape<u8>()) == sizeof(u8), \"local byte extent\");\nglobal u32 entry() { return 0u32; }\n")
file(WRITE "${OUTPUT}/local_shape.x" "${source}")
foreach(scalar uptr u8)
    file(WRITE "${OUTPUT}/local_shape_${scalar}.x" "${local_helper}$::static_assert($::eval(local_shape<${scalar}>()) == sizeof(${scalar}), \"local extent\");\nglobal u32 entry() { return 0u32; }\n")
endforeach()
foreach(level O0 O2)
    foreach(resource memory steps)
        if(resource STREQUAL memory)
            set(limit -feval-memory-limit=64 -feval-step-limit=64000000)
            set(expected "translation-time meta memory budget exceeded 64 bytes")
        else()
            set(limit -feval-step-limit=25)
            set(expected "translation-time instruction budget exceeded 25")
        endif()
        foreach(scalar uptr u8)
            execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags} ${limit}
                "${OUTPUT}/local_shape_${scalar}.x" -o "${OUTPUT}/local-shape-${scalar}-${resource}-${level}.s"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
            string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" errors "${err}")
            list(LENGTH errors count)
            if(NOT status EQUAL 1 OR NOT count EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "while evaluating call to 'local_shape")
                message(FATAL_ERROR "${MODE}/${level}/${resource}/${scalar}: local preparation cascaded, hung, or lost provenance\n${out}\n${err}")
            endif()
        endforeach()
    endforeach()
    execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
        "${OUTPUT}/local_shape.x" -o "${OUTPUT}/local-shape-finite-${level}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${MODE}/${level}: finite local preparation failed\n${out}\n${err}")
    endif()
endforeach()

function(source_for count name)
    set(source "static uptr object_size<T>() { T object = {}; return sizeof(object); }\n")
    math(EXPR last "${count} - 1")
    foreach(index RANGE 0 ${last})
        math(EXPR next "${index} + 1")
        string(APPEND source "struct R${index} [[aligned(object_size<struct R${next}>())]] { u8 value; };\n")
    endforeach()
    string(APPEND source "struct R${count} { u8 value; };\n$::static_assert(sizeof(struct R0) == sizeof(u8), \"required layout\");\nglobal u32 entry() { return (u32)sizeof(struct R0); }\n")
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
endfunction()

# Each helper constructs typed storage: these are HIR/required-evaluation
# callbacks, not a parser matching limit. Stop the entire affected traversal
# after its first new failure, without replacing its precise call ancestry.
source_for(12 exhausted)
source_for(3 finite)
# Sharing the active record view eliminates accidental repeated allocations.
# Keep genuine small-budget failures and require the former slow path to pass
# at its old 64-KiB budget, without increasing any compiler limit.
source_for(12 shared)
foreach(level O0 O2)
    foreach(resource memory steps)
        if(resource STREQUAL memory)
            set(limit -feval-memory-limit=256 -feval-step-limit=64000000)
            set(expected "translation-time meta memory budget exceeded 256 bytes")
        else()
            set(limit -feval-step-limit=25)
            set(expected "translation-time instruction budget exceeded 25")
        endif()
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags} ${limit}
            "${OUTPUT}/exhausted.x" -o "${OUTPUT}/exhausted-${resource}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" errors "${err}")
        list(LENGTH errors count)
        if(NOT status EQUAL 1 OR NOT count EQUAL 1 OR NOT err MATCHES "${expected}" OR
           NOT err MATCHES "exhausted.x:1:[0-9]+: error:" OR
           NOT err MATCHES "while evaluating call to 'object_size")
            message(FATAL_ERROR "${MODE}/${level}/${resource}: layout exhaustion cascaded, hung, or lost provenance\n${out}\n${err}")
        endif()
    endforeach()
    execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
        "${OUTPUT}/finite.x" -o "${OUTPUT}/finite-${level}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${MODE}/${level}: finite object/layout control failed\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags} -feval-memory-limit=65536
        "${OUTPUT}/shared.x" -o "${OUTPUT}/shared-${level}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${MODE}/${level}: shared object/layout view control failed\n${out}\n${err}")
    endif()
endforeach()

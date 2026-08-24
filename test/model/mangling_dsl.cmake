# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_mangler name)
    execute_process(
        COMMAND "${CC}" "--model=${MODEL}" "-mmangling=${name}"
                -S "${SOURCE}" -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "mangler ${name} failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}.${name}.s" assembly)
    set("${name}_assembly" "${assembly}" PARENT_SCOPE)
endfunction()

compile_mangler(recursive-test)
compile_mangler(itanium)
compile_mangler(msvc)

if(NOT recursive-test_assembly MATCHES
       "R5codec6repeatPKiS0_iS1_")
    message(FATAL_ERROR
        "recursive helper/substitution mangler result is wrong\n${recursive-test_assembly}")
endif()
if(NOT itanium_assembly MATCHES
       "_ZN5codec6repeatEPKiS_ii")
    message(FATAL_ERROR
        "shipped Itanium DSL mangler result is wrong\n${itanium_assembly}")
endif()
if(NOT msvc_assembly MATCHES
       "[?]repeat@codec@@YAHPEA[$][$]CBH0H1@Z")
    message(FATAL_ERROR
        "shipped MSVC DSL mangler result is wrong\n${msvc_assembly}")
endif()

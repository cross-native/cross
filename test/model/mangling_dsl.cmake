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

# A generic instance's link name does not depend on the names of the type
# parameters: the mangling sees the instance's own signature.
foreach(parameter T Elem)
    set(source "${OUTPUT}.generic-${parameter}.x")
    set(P "${parameter}")
    file(WRITE "${source}" "global ${P} larger<${P}>(${P} a, ${P} b) {
    return a > b ? a : b;
}
global ${P} apply<${P}>(${P} (*f)(${P}), ${P} x) {
    return f(x);
}
static u32 twice(u32 x) {
    return x * 2;
}
global u64 pick(u64 x, u32 y) {
    return larger(x, 10u64) + apply(twice, y);
}
")
    foreach(name itanium signature-test)
        execute_process(
            COMMAND "${CC}" "--model=${MODEL}" "-mmangling=${name}"
                    -S "${source}" -o "${source}.${name}.s"
            RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout
            ERROR_VARIABLE stderr
        )
        if(NOT status EQUAL 0)
            message(FATAL_ERROR
                "generic ${parameter} with ${name} failed\n${stdout}\n${stderr}")
        endif()
        file(READ "${source}.${name}.s" assembly)
        if(name STREQUAL itanium)
            set(expected "_Z6largerIyEyy")
        else()
            set(expected "larger__u64__inu64_inu64"
                "apply__u32__inPF5_cross3_u324_auto6_caller0__1_i3_u324_autoE_inu32")
        endif()
        foreach(symbol IN LISTS expected)
            if(NOT assembly MATCHES "${symbol}")
                message(FATAL_ERROR
                    "${symbol} depends on the type-parameter name ${parameter}\n${assembly}")
            endif()
        endforeach()
    endforeach()
endforeach()

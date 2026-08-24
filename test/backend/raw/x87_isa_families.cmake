# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu)
    string(REPLACE "-" "_" suffix "${target}")
    execute_process(
        COMMAND "${CC}" -S -march=haswell -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${suffix}.s"
        RESULT_VARIABLE assembly_status
        OUTPUT_VARIABLE assembly_stdout
        ERROR_VARIABLE assembly_stderr
    )
    execute_process(
        COMMAND "${CC}" -c -march=haswell -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${suffix}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr
    )
    if(NOT assembly_status EQUAL 0 OR NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "raw x87 forms failed for ${target}\n${assembly_stdout}\n"
            "${assembly_stderr}\n${object_stdout}\n${object_stderr}")
    endif()
    file(READ "${OUTPUT}-${suffix}.s" assembly)
    foreach(pattern
            "flds[\t ]+[(]%r8[)]"
            "fsts[\t ]+[(]%r8[)]"
            "fldl[\t ]+[(]%r9[)]"
            "fadd[\t ]+%st[(]1[)], %st"
            "fxch[\t ]+%st[(]1[)]"
            "fucomi[\t ]+%st[(]1[)], %st"
            "faddp[\t ]+%st, %st[(]1[)]"
            "fstpt[\t ]+[(]%r10[)]"
            "fildl[\t ]+[(]%r11[)]"
            "fistpl[\t ]+[(]%r11[)]"
            "fldcw[\t ]+[(]%rcx[)]"
            "fnstcw[\t ]+[(]%rcx[)]"
            "fnstsw[\t ]+%ax"
            "fld1([\t \n]|$)"
            "fldpi([\t \n]|$)"
            "fyl2x([\t \n]|$)"
            "fptan([\t \n]|$)"
            "fsincos([\t \n]|$)"
            "fxtract([\t \n]|$)"
            "fmulp[\t ]+%st, %st[(]1[)]"
            "fnclex([\t \n]|$)"
            "fclex([\t \n]|$)"
            "fwait([\t \n]|$)"
            "fninit([\t \n]|$)"
            "finit([\t \n]|$)"
            "emms([\t \n]|$)")
        if(NOT assembly MATCHES "${pattern}")
            message(FATAL_ERROR
                "raw x87 output for ${target} is missing ${pattern}\n${assembly}")
        endif()
    endforeach()
endforeach()

execute_process(
    COMMAND "${CC}" -S "${ERROR_SOURCE}" -o "${OUTPUT}-errors.s"
    RESULT_VARIABLE error_status
    OUTPUT_VARIABLE error_stdout
    ERROR_VARIABLE error_stderr
)
if(error_status EQUAL 0)
    message(FATAL_ERROR "invalid raw x87 state compiled\n${error_stdout}")
endif()
foreach(pattern
        "raw x87 instruction reads st0 above the current dense stack depth 0"
        "raw x87 stack depth is 1 at [$]::_ret; interface requires 0"
        "inconsistent raw x87 stack depth at control-flow join")
    if(NOT error_stderr MATCHES "${pattern}")
        message(FATAL_ERROR
            "raw x87 diagnostic is missing ${pattern}\n${error_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_stderr
)
if(NOT registry_status EQUAL 0)
    message(FATAL_ERROR "--print-instructions failed\n${registry_stderr}")
endif()
foreach(entry _fld _fild _fst _fstp _fistp _fadd _faddp _fucomi
              _fabs _fsqrt _fyl2x _fptan _fsincos _fxtract _fnstsw
              _fnclex _fclex _fwait _fninit _finit _emms)
    string(FIND "${registry}" "$::${entry} instruction" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "instruction registry is missing $::${entry}")
    endif()
endforeach()

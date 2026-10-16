# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid raw_inline cases compiled successfully")
endif()
foreach(pattern
        "requires another matching register in the naked caller's clobber contract"
        "raw_inline local requires automatic scalar storage"
        "ordinary or indirect calls are not permitted in a naked function"
        "recursive raw_inline call cannot be completely eliminated"
        "floating value requires a matching f32/f64 SIMD register"
        "raw_inline_errors[.]x:70:12: error: x86-64 naked functions implement a [$]::patch value only as the complete operand of a patchable instruction, such as [$]::_movabs[^\n]*\n[^\n]*\n[^\n]*\n[^\n]*raw_inline_errors[.]x:75:14: note: inlined here"
        "raw_inline_errors[.]x:81:14: error: built-in '[$]::popcount' is not implemented in an x86-64 naked function")
    if(NOT stderr MATCHES "${pattern}")
        message(FATAL_ERROR
            "raw_inline diagnostic is missing '${pattern}'\n${stdout}\n${stderr}")
    endif()
endforeach()

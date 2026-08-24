# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" --print-registers
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "--print-registers failed (${status})\n${error}")
endif()
foreach(pattern
        "eax  storage: rax  bits: 32  class: integer"
        "xmm0  storage: zmm0  bits: 128  class: simd  feature: base"
        "ymm0  storage: zmm0  bits: 256  class: simd  feature: avx"
        "zmm31  storage: zmm31  bits: 512  class: simd  feature: avx512f"
        "st0  storage: st0  bits: 80  class: x87  feature: base"
        "st7  storage: st7  bits: 80  class: x87  feature: base")
    if(NOT output MATCHES "${pattern}")
        message(FATAL_ERROR "register registry is missing: ${pattern}\n${output}")
    endif()
endforeach()

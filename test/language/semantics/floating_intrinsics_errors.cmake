# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(reject label expected)
    execute_process(
        COMMAND "${CC}" -S ${ARGN} "${SOURCE}" -o "${OUTPUT}-${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${expected}" OR
       NOT stderr MATCHES "[.]x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${label} did not report '${expected}'\n${stdout}\n${stderr}")
    endif()
endfunction()

reject(integer "[$]::sqrt requires floating operands"
       -target x86_64-unknown-linux-gnu -DINTEGER)
reject(mixed "[$]::fmin requires operands of one floating type"
       -target x86_64-unknown-linux-gnu -DMIXED)
reject(arity "[$]::copysign requires two arguments"
       -target x86_64-unknown-linux-gnu -DARITY)
reject(evaluated "[$]::fmax requires operands of one floating type"
       -target x86_64-unknown-linux-gnu -DEVALUATED)
reject(binary128 "[$]::fmax has no inline x86-64 form for f128"
       -target x86_64-unknown-linux-gnu -DBINARY128)
reject(mips1 "[$]::sqrt requires feature 'mips2'"
       -target mips-unknown-elf -march=r3000)

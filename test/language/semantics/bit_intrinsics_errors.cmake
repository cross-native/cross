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

foreach(target x86_64-unknown-linux-gnu mips-unknown-elf)
    reject(floating-${target} "[$]::clz requires integer operands" -target ${target} -DFLOATING)
    reject(boolean-${target} "[$]::popcount requires integer operands" -target ${target} -DBOOLEAN)
    reject(pointer-${target} "[$]::ctz requires integer operands" -target ${target} -DPOINTER)
    reject(count-${target} "[$]::rotr requires integer operands" -target ${target} -DCOUNT)
    reject(rotate-arity-${target} "[$]::rotl requires two arguments" -target ${target} -DROTATE_ARITY)
    reject(swap-arity-${target} "[$]::bswap requires one argument" -target ${target} -DSWAP_ARITY)
    reject(evaluated-${target} "[$]::ctz requires integer operands" -target ${target} -DEVALUATED)
    reject(untaken-${target} "[$]::popcount requires integer operands" -target ${target} -DUNTAKEN)
endforeach()

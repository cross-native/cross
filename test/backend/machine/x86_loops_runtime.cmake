# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
execute_process(COMMAND "${CC}" -c -target x86_64-unknown-linux-gnu
    -mabi=sysv_abi -fno-eval-calls ${CC_FLAGS} "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "compilation: ${error}")
endif()
execute_process(COMMAND "${HOST_CXX}" -std=c++20 -O2 "${RUNNER}"
    "${OUTPUT}.o" -o "${OUTPUT}.exe" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "link: ${error}")
endif()
execute_process(COMMAND "${OUTPUT}.exe" RESULT_VARIABLE result ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "runtime (${result}): ${error}")
endif()

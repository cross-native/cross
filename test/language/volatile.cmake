# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE result
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cc failed (${result})\n${stderr}")
endif()
file(READ "${OUTPUT}" ir)
if(NOT ir MATCHES "load volatile i32")
    message(FATAL_ERROR "volatile load contract was lost")
endif()
if(NOT ir MATCHES "store volatile i32")
    message(FATAL_ERROR "volatile store contract was lost")
endif()


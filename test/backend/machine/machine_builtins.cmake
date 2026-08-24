# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cc failed (${status})\n${stdout}\n${stderr}")
endif()

file(READ "${OUTPUT}" ir)
if(ir MATCHES "asm sideeffect")
    message(FATAL_ERROR "machine built-ins became opaque side-effecting inline assembly")
endif()
if(NOT ir MATCHES "add i64")
    message(FATAL_ERROR "$::_add was not represented as an optimizable typed operation")
endif()
if(NOT ir MATCHES "icmp eq i64")
    message(FATAL_ERROR "$::_cmp was not represented as an optimizable typed operation")
endif()

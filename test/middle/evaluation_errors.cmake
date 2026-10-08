# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid staging expressions unexpectedly compiled")
endif()

foreach(expected
        "call to runtime-only function 'runtime_seed' cannot be evaluated during translation"
        "while evaluating call to 'enters_runtime_only'"
        "$::runtime is invalid where a translation-time value is required"
        "eval-only function 'compile_seed' cannot be called inside $::runtime"
        "$::eval cannot appear inside a $::runtime expression")
    string(FIND "${stderr}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing staging diagnostic '${expected}'\n${stdout}\n${stderr}")
    endif()
endforeach()

# Only evaluation is restricted: an unselected $::runtime, and $::eval inside
# an unevaluated operand of $::runtime, are valid.
file(WRITE "${OUTPUT}.unselected.x" [=[
static u32 runtime_value() { return 7u32; }
[[eval_only]] static u32 eval_value() { return 8u32; }
global u32 unselected = 0u32 ? $::runtime(runtime_value()) : 3u32;
global u32 measured(in u32 x) { return $::runtime((u32)sizeof($::eval(eval_value())) + x); }
$::static_assert(1u32 || $::runtime(runtime_value()), "unselected runtime operand");
]=])
execute_process(
    COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu "${OUTPUT}.unselected.x"
            -o "${OUTPUT}.unselected.s"
    RESULT_VARIABLE status
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "unselected staging expressions were rejected\n${stderr}")
endif()
file(READ "${OUTPUT}.unselected.s" assembly)
if(NOT assembly MATCHES "unselected:\n[ \t]*\\.long 3\n")
    message(FATAL_ERROR "unselected initializer did not evaluate to 3\n${assembly}")
endif()

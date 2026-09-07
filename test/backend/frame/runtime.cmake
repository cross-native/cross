# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT HOST_CXX)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(COMMAND "${HOST_CXX}" -O2 -c "${CMAKE_CURRENT_LIST_DIR}/driver.cpp"
    -o "${OUTPUT}-driver.o" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "frame runtime driver compilation failed")
endif()
execute_process(COMMAND "${HOST_CXX}" -c "${CMAKE_CURRENT_LIST_DIR}/canary.s"
    -o "${OUTPUT}-canary.o" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "frame ABI canary assembly failed")
endif()

# 'none' selects the verified fixed-frame program. 'platform' keeps the
# existing metadata-capable lowering, even when table emission is disabled.
foreach(level O0 O2 O3 Oz)
  foreach(fp omit-frame-pointer no-omit-frame-pointer)
    foreach(unwind none platform)
        set(run_output "${OUTPUT}-${level}-${fp}-${unwind}")
        execute_process(COMMAND "${CC}" -${level} -f${fp} -funwind-model=${unwind}
            -fno-unwind-tables -fno-asynchronous-unwind-tables
            -c "${CMAKE_CURRENT_LIST_DIR}/runtime.x" -o "${run_output}.o"
            RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "frame compilation failed: ${level}, ${fp}, ${unwind}")
        endif()
        execute_process(COMMAND "${HOST_CXX}" "${OUTPUT}-driver.o"
            "${OUTPUT}-canary.o" "${run_output}.o" -o "${run_output}.exe"
            RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "frame runtime linking failed")
        endif()
        execute_process(COMMAND "${run_output}.exe" RESULT_VARIABLE status TIMEOUT 10)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "frame runtime failed: ${level}, ${fp}, ${unwind} (${status})")
        endif()
    endforeach()
  endforeach()
endforeach()

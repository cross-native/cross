# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs the x86-64 shrink-wrapping kernels at LEVEL with shrink-wrapping
# enabled and disabled, with and without a frame pointer.
foreach(required CC OUTPUT HOST_CXX LEVEL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

execute_process(COMMAND "${HOST_CXX}" -O1 -std=c++20 -c
    "${CMAKE_CURRENT_LIST_DIR}/driver.cpp" -o "${OUTPUT}-driver.o"
    RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "shrink-wrap driver compilation failed")
endif()
execute_process(COMMAND "${HOST_CXX}" -c "${CMAKE_CURRENT_LIST_DIR}/canary.s"
    -o "${OUTPUT}-canary.o" RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "shrink-wrap canary assembly failed")
endif()

foreach(mode shrink-wrap no-shrink-wrap)
  foreach(fp omit-frame-pointer no-omit-frame-pointer)
    set(run "${OUTPUT}-${mode}-${fp}")
    execute_process(COMMAND "${CC}" -${LEVEL} -f${mode} -f${fp}
        -c "${CMAKE_CURRENT_LIST_DIR}/shrink_wrap.x" -o "${run}.o"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "shrink-wrap compilation failed (-${LEVEL} -f${mode} -f${fp})\n${errors}")
    endif()
    execute_process(COMMAND "${HOST_CXX}" "${OUTPUT}-driver.o"
        "${OUTPUT}-canary.o" "${run}.o" -o "${run}.exe" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "shrink-wrap runtime linking failed")
    endif()
    execute_process(COMMAND "${run}.exe" RESULT_VARIABLE status
        ERROR_VARIABLE errors TIMEOUT 20)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "shrink-wrap runtime failed (-${LEVEL} -f${mode} -f${fp}, ${status})\n${errors}")
    endif()
  endforeach()
endforeach()

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(level O0 O2 O3 Oz)
  foreach(private private-abi no-private-abi)
    set(out "${OUTPUT}-${level}-${private}")
    execute_process(COMMAND "${CC}" -${level} -f${private} -mabi=ms_abi
        -c "${CMAKE_CURRENT_LIST_DIR}/runtime.x" -o "${out}.o" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
      message(FATAL_ERROR "indirect call compilation failed: ${level}/${private}")
    endif()
    execute_process(COMMAND "${CC}" -${level} -f${private} -mabi=ms_abi
        -c "${CMAKE_CURRENT_LIST_DIR}/floating.x" -o "${out}-floating.o" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
      message(FATAL_ERROR "floating callback compilation failed")
    endif()
    execute_process(COMMAND "${HOST_CXX}" -O2
        "${CMAKE_CURRENT_LIST_DIR}/driver.cpp" "${out}.o" "${out}-floating.o" -o "${out}.exe"
        RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
      message(FATAL_ERROR "indirect call driver link failed")
    endif()
    execute_process(COMMAND "${out}.exe" RESULT_VARIABLE status TIMEOUT 10)
    if(NOT status EQUAL 0)
      message(FATAL_ERROR "indirect call execution failed: ${level}/${private}")
    endif()
  endforeach()
endforeach()

find_program(LLVM_AS NAMES llvm-as)
if(LLVM_AS)
    execute_process(COMMAND "${CC}" -O2 -mabi=ms_abi -emit-llvm
        "${CMAKE_CURRENT_LIST_DIR}/runtime.x" -o "${OUTPUT}.ll" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "indirect call LLVM serialization failed")
    endif()
    execute_process(COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "indirect call LLVM IR validation failed")
    endif()
else()
    message(STATUS "skipping LLVM IR validation: llvm-as unavailable")
endif()

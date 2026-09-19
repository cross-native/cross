# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu
               mips-unknown-elf mipsel-unknown-elf
               mips64-unknown-elf mips64el-unknown-elf)
    execute_process(
        COMMAND "${CC}" -S -target "${target}"
                "${SOURCE_DIR}/address_spaces.x"
                -o "${OUTPUT}-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${target} rejected generic space 0\n${stdout}\n${stderr}")
    endif()
    execute_process(
        COMMAND "${CC}" -S -target "${target}"
                "${SOURCE_DIR}/address_space_function_pointer.x"
                -o "${OUTPUT}-callback-${target}.s"
        RESULT_VARIABLE callback_status OUTPUT_VARIABLE callback_stdout
        ERROR_VARIABLE callback_stderr)
    if(NOT callback_status EQUAL 0)
        message(FATAL_ERROR
            "${target} rejected generic-space function pointer\n${callback_stdout}\n${callback_stderr}")
    endif()
endforeach()

foreach(emit llvm gimple)
    execute_process(
        COMMAND "${CC}" "-emit-${emit}"
                "${SOURCE_DIR}/address_spaces.x"
                -o "${OUTPUT}.${emit}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${emit} rejected generic space 0\n${stdout}\n${stderr}")
    endif()
endforeach()

foreach(target x86_64-unknown-linux-gnu mips64el-unknown-elf)
    execute_process(
        COMMAND "${CC}" -S -target "${target}"
                "${SOURCE_DIR}/address_space_unsupported.x"
                -o "${OUTPUT}-unsupported-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR
       NOT stderr MATCHES "address space 1 is not registered" OR
       NOT stderr MATCHES "address space 2 is not registered" OR
       NOT stderr MATCHES "address space 3 is not registered")
        message(FATAL_ERROR
            "${target} failed to diagnose both unsupported spaces\n${stdout}\n${stderr}")
    endif()
endforeach()

foreach(case bad_argument duplicate grouped_duplicate nonpointer)
    execute_process(
        COMMAND "${CC}" -S
                "${SOURCE_DIR}/address_space_${case}.x"
                -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(case STREQUAL "bad_argument")
        set(expected "address_space requires a nonnegative target registry number")
    elseif(case STREQUAL "duplicate" OR
           case STREQUAL "grouped_duplicate")
        set(expected "duplicate address_space type qualifier")
    else()
        set(expected "address_space requires a pointer type")
    endif()
    if(status EQUAL 0 OR NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR
            "${case} lacked its source diagnostic\n${stdout}\n${stderr}")
    endif()
endforeach()

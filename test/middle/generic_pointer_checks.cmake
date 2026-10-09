# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE DRIVER ERRORS MODEL CUSTOM_MODEL OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
if(WIN32)
    set(abi ms_abi)
else()
    set(abi sysv_abi)
endif()

foreach(level O0 O2 O3 Oz)
    foreach(mode ordinary custom profile private-mangler)
        set(flags "-mabi=${abi}")
        if(mode STREQUAL custom OR mode STREQUAL profile)
            list(APPEND flags "--model=${CUSTOM_MODEL}" -DCUSTOM_POINTER_ABI)
        endif()
        if(mode STREQUAL private-mangler)
            list(APPEND flags "--model=${MODEL}" -mmangling=erase-generic-arguments
                              -DPRIVATE_POINTER_GENERIC)
        endif()
        if(mode STREQUAL profile)
            # Profile selects both ABI and mangler. Only the C++ entry boundary
            # and the callback deliberately opt back into the host ABI.
            set(flags "--model=${CUSTOM_MODEL}" "--model=${MODEL}"
                      -mprofile=pointer-profile -DCUSTOM_POINTER_ABI)
        endif()
        set(stem "${OUTPUT}-${level}-${mode}")
        execute_process(COMMAND "${CC}" ${flags} -${level} -fno-eval-calls
            "-DHOST_ABI=\"${abi}\"" -c "${SOURCE}" -o "${stem}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "pointer generic compile ${level}/${mode}\n${stdout}\n${stderr}")
        endif()
        execute_process(COMMAND "${HOST_CXX}" "${DRIVER}" "${stem}.o" -o "${stem}.exe"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "pointer generic link ${level}/${mode}\n${stdout}\n${stderr}")
        endif()
        execute_process(COMMAND "${stem}.exe" RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "pointer generic execution ${level}/${mode}: ${status}")
        endif()
    endforeach()
endforeach()

get_filename_component(test_dir "${SOURCE}" DIRECTORY)
execute_process(COMMAND "${CC}" "--model=${CUSTOM_MODEL}" "-mabi=${abi}"
    -DCUSTOM_POINTER_ABI -DCALLBACK_ABI_ERROR "-DHOST_ABI=\"${abi}\""
    -c "${SOURCE}" -o "${OUTPUT}-callback-abi-error.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 1 OR NOT stderr MATCHES
   "a function-pointer value cannot change its callable ABI in return" OR
   NOT stderr MATCHES "generic_pointer_value.x:[0-9]+:[0-9]+: error:" OR
   NOT stderr MATCHES "while evaluating call to 'mismatched_callback'")
    message(FATAL_ERROR "typed function pointer lost its ABI during evaluation\n${stdout}\n${stderr}")
endif()

foreach(order forward reverse)
    set(units "${test_dir}/generic_pointer_units_a.x" "${test_dir}/generic_pointer_units_b.x")
    if(order STREQUAL reverse)
        list(REVERSE units)
    endif()
    foreach(level O0 O3)
        set(stem "${OUTPUT}-units-${order}-${level}")
        execute_process(COMMAND "${CC}" "--model=${CUSTOM_MODEL}" "--model=${MODEL}"
            -mprofile=pointer-profile "-DHOST_ABI=\"${abi}\"" -${level} -fno-eval-calls
            -c ${units} -o "${stem}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "pointer source-unit compile ${order}/${level}\n${stdout}\n${stderr}")
        endif()
        execute_process(COMMAND "${HOST_CXX}" "${test_dir}/generic_pointer_units_driver.cpp"
            "${stem}.o" -o "${stem}.exe"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "pointer source-unit link ${order}/${level}\n${stdout}\n${stderr}")
        endif()
        execute_process(COMMAND "${stem}.exe" RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "pointer source-unit execution ${order}/${level}: ${status}")
        endif()
    endforeach()
endforeach()

foreach(case
        "mips-unknown-elf,vr4300,o32"
        "mipsel-unknown-elf,vr4300,o32"
        "mips64-unknown-elf,mips64,n64"
        "mips64el-unknown-elf,mips64,n64")
    string(REPLACE "," ";" fields "${case}")
    list(GET fields 0 triple)
    list(GET fields 1 cpu)
    list(GET fields 2 target_abi)
    foreach(level O0 O3)
        execute_process(COMMAND "${CC}" -target "${triple}" "-march=${cpu}"
            "-mabi=${target_abi}" -${level} -c "${SOURCE}" -o "${OUTPUT}-${triple}-${level}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "pointer generic ${triple}/${level}\n${stdout}\n${stderr}")
        endif()
    endforeach()
endforeach()

foreach(case
        "LOCAL|cannot depend on an automatic local or parameter"
        "PARAMETER|cannot depend on an automatic local or parameter"
        "QUALIFIERS|incompatible pointed-to type or qualifiers"
        "MEMBER_QUALIFIERS|incompatible pointed-to type or qualifiers"
        "ARRAY_QUALIFIERS|incompatible pointed-to type or qualifiers"
        "NESTED|incompatible pointed-to type or qualifiers"
        "CAST_QUALIFIERS|incompatible pointed-to type or qualifiers"
        "STRING_QUALIFIERS|incompatible pointed-to type or qualifiers"
        "WRONG_TYPE|incompatible pointed-to type or qualifiers"
        "UNKNOWN|unresolved name in generic pointer argument"
        "NONZERO|requires an explicit pointer cast"
        "TLS|thread-local address is not a generic pointer constant"
        "PAST|outside its object or one-past bound"
        "BEFORE|outside its object or one-past bound"
        "OVERFLOW|not a supported address constant"
        "ABSOLUTE_OVERFLOW|arithmetic overflows the selected target width"
        "ABSOLUTE_UNDERFLOW|arithmetic overflows the selected target width"
        "VOID_ARITHMETIC|requires a complete object type and representable offset"
        "RUNTIME_CALL|call to runtime-only function"
        "STATIC_READ|runtime/static storage cannot be read"
        "ESCAPE|object pointer cannot escape into a generic argument"
        "UNINITIALIZED|read of uninitialized value"
        "INDIRECT_CALL|indirect calls are not permitted"
        "CALL_QUALIFIERS|incompatible pointed-to type or qualifiers"
        "ROUNDTRIP_QUALIFIERS|incompatible pointed-to type or qualifiers"
        "EVAL_ADDRESS|requires a function with a runtime address"
        "RUNTIME|runtime expression is not permitted in a generic pointer argument")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 selector)
    list(GET fields 1 diagnostic)
    execute_process(COMMAND "${CC}" "-D${selector}" -c "${ERRORS}" -o "${OUTPUT}-error.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 10)
    if(status EQUAL 0 OR NOT stderr MATCHES "${diagnostic}")
        message(FATAL_ERROR "pointer generic diagnostic ${selector}\n${stdout}\n${stderr}")
    endif()
endforeach()

execute_process(COMMAND "${CC}" -target mips-unknown-elf -march=vr4300 -mabi=o32
    -DWIDTH -c "${ERRORS}" -o "${OUTPUT}-width.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES "not representable in the selected target width")
    message(FATAL_ERROR "pointer generic target width was not checked\n${stdout}\n${stderr}")
endif()

execute_process(COMMAND "${CC}" -target mipsel-unknown-elf -march=vr4300 -mabi=o32
    -c "${test_dir}/generic_pointer_width.x" -o "${OUTPUT}-arithmetic-width.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES "arithmetic overflows the selected target width")
    message(FATAL_ERROR "pointer arithmetic ignored the selected address width\n${stdout}\n${stderr}")
endif()
execute_process(COMMAND "${CC}" -target mips64el-unknown-elf -march=mips64 -mabi=n64
    -c "${test_dir}/generic_pointer_width.x" -o "${OUTPUT}-arithmetic-wide.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "64-bit pointer arithmetic rejected a representable result\n${stdout}\n${stderr}")
endif()

execute_process(COMMAND "${CC}" "--model=${MODEL}" -mmangling=erase-generic-arguments
    -c "${SOURCE}" -o "${OUTPUT}-collision.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES "link name|link symbol")
    message(FATAL_ERROR "custom mangler silently merged distinct generic instances\n${stdout}\n${stderr}")
endif()

if(LLVM_TEXT)
    execute_process(COMMAND "${CC}" "-mabi=${abi}" -emit-llvm -O0 "${SOURCE}"
        -o "${OUTPUT}.ll" RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "pointer generic LLVM serialization\n${stdout}\n${stderr}")
    endif()
    find_program(LLVM_AS NAMES llvm-as)
    if(LLVM_AS)
        execute_process(COMMAND "${LLVM_AS}" "${OUTPUT}.ll" -o "${OUTPUT}.bc"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "pointer generic LLVM verification\n${stdout}\n${stderr}")
        endif()
    else()
        message(STATUS "skipping optional pointer generic LLVM verification: llvm-as unavailable")
    endif()
endif()

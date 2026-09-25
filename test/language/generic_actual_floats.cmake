# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

foreach(target default mips-unknown-elf mipsel-unknown-elf
        mips64-unknown-elf mips64el-unknown-elf custom)
    set(flags -S -O0 -fno-eval-calls)
    if(target MATCHES "^mips")
        list(APPEND flags -target "${target}")
        if(target MATCHES "^mips64")
            list(APPEND flags -mabi=n64)
        else()
            list(APPEND flags -mabi=o32)
        endif()
    elseif(target STREQUAL custom)
        list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
    endif()
    execute_process(COMMAND "${CC}" ${flags} "${SOURCE}"
        -o "${OUTPUT}-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${target} floating generic inference failed\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}-${target}.s" assembly)
    foreach(name inferred_f32 inferred_f64 inferred_f80 inferred_f128 inferred_fptr
            promoted_unary promoted_binary promoted_shift mixed_integer
            mixed_float mixed_conditional pointer_width_integer)
        if(NOT assembly MATCHES "${name}:\n[^\n]*[.]long 1")
            message(FATAL_ERROR "${target} did not materialize ${name}=1\n${assembly}")
        endif()
    endforeach()
endforeach()

foreach(case floating promoted)
    if(case STREQUAL floating)
        set(arguments "1.5f80, (f128)1.5f64")
    else()
        set(arguments "1i8 + 2u8, (u8)3u8")
    endif()
    set(conflict "${OUTPUT}-${case}-conflict.x")
    file(WRITE "${conflict}"
        "[[eval_only]] static u32 match_float<T>(in T first, in T second) { return 1u32; }\n"
        "global u32 bad = match_float(${arguments});\n")
    execute_process(COMMAND "${CC}" -S "${conflict}"
        -o "${OUTPUT}-${case}-conflict.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "conflicting deductions for generic type parameter")
        message(FATAL_ERROR "incompatible ${case} generic actuals were accepted\n${out}\n${err}")
    endif()
endforeach()

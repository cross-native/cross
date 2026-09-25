# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE DRIVER GENERATOR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
if(WIN32)
    set(ABI ms_abi)
else()
    set(ABI sysv_abi)
endif()

set(directory "${OUTPUT}.assets")
file(MAKE_DIRECTORY "${directory}")
configure_file("${SOURCE}" "${directory}/embed_values.x" COPYONLY)
execute_process(COMMAND "${HOST_CXX}" "${GENERATOR}" -o "${OUTPUT}-generator.exe"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "embedded asset generator build failed\n${out}\n${err}")
endif()
execute_process(COMMAND "${OUTPUT}-generator.exe" "${directory}/payload.bin"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "embedded asset generator failed\n${out}\n${err}")
endif()

foreach(level O0 O2)
    foreach(mode normal noeval)
        set(stem "${OUTPUT}-${level}-${mode}")
        set(flags -mabi=${ABI} -${level})
        if(mode STREQUAL noeval)
            list(APPEND flags -fno-eval-calls)
        endif()
        execute_process(COMMAND "${CC}" ${flags} -c
            "${directory}/embed_values.x" -o "${stem}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "embedded value compile failed (${level}/${mode})\n${out}\n${err}")
        endif()
        execute_process(COMMAND "${HOST_CXX}" "${DRIVER}" "${stem}.o"
            -o "${stem}.exe"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "embedded value link failed (${level}/${mode})\n${out}\n${err}")
        endif()
        execute_process(COMMAND "${stem}.exe" RESULT_VARIABLE status)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "embedded value runtime mismatch (${level}/${mode}): ${status}")
        endif()
    endforeach()
endforeach()

set(limit_source "${directory}/limits.x")
file(WRITE "${limit_source}"
    "static uptr count(in $::meta::bytes value) { return $::meta::len(value); }\n"
    "[[eval_only]] static uptr outer() { return count($::embed(\"payload.bin\")); }\n"
    "global const u8 data[] = $::embed(\"payload.bin\");\n"
    "global uptr size = outer();\n")
foreach(case byte memory steps depth)
    if(case STREQUAL byte)
        set(option eval-byte-limit)
        set(low 4)
        set(high 5)
        set(expected "embedded asset exceeds the target uptr or 4-byte limit")
    elseif(case STREQUAL memory)
        set(option eval-memory-limit)
        set(low 9)
        set(high 10)
        set(expected "meta memory budget exceeded 9 bytes")
    elseif(case STREQUAL steps)
        set(option eval-step-limit)
        set(low 1)
        set(high 100)
        set(expected "instruction budget exceeded 1")
    else()
        set(option eval-depth-limit)
        set(low 1)
        set(high 2)
        set(expected "recursion depth exceeded 1")
    endif()
    execute_process(COMMAND "${CC}" -S "-f${option}=${low}"
        "${limit_source}" -o "${OUTPUT}-${case}-low.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${case} limit was not enforced\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${CC}" -S "-f${option}=${high}"
        "${limit_source}" -o "${OUTPUT}-${case}-high.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${case} limit override did not compile\n${out}\n${err}")
    endif()
endforeach()

foreach(case unassigned_prefix double_freeze over_capacity nonstatic_buffer meta_memory)
    if(case STREQUAL unassigned_prefix)
        string(CONCAT body "$::meta::buffer value = $::meta::alloc(1u32);\n"
                 "$::meta::bytes result = $::meta::freeze(value, 1u32);\n"
                 "return $::meta::len(result);")
        set(expected "freeze requires every prefix byte to be assigned")
    elseif(case STREQUAL double_freeze)
        string(CONCAT body "$::meta::buffer value = $::meta::alloc(0u32);\n"
                 "$::meta::buffer alias = value;\n"
                 "$::meta::bytes first = $::meta::freeze(value, 0u32);\n"
                 "$::meta::bytes second = $::meta::freeze(alias, 0u32);\n"
                 "return $::meta::len(first) + $::meta::len(second);")
        set(expected "buffer handle was used after freeze")
    elseif(case STREQUAL over_capacity)
        string(CONCAT body "$::meta::buffer value = $::meta::alloc(0u32);\n"
                 "$::meta::bytes result = $::meta::freeze(value, 1u32);\n"
                 "return $::meta::len(result);")
        set(expected "freeze length exceeds buffer capacity")
    elseif(case STREQUAL meta_memory)
        string(CONCAT body
            "$::meta::buffer first = $::meta::alloc(16777216u32);\n"
            "$::meta::buffer second = $::meta::alloc(16777216u32);\n"
            "$::meta::buffer third = $::meta::alloc(1u32);\n"
            "return $::meta::cap(third);")
        set(expected "meta memory budget exceeded 67108864 bytes")
    else()
        set(body "")
        set(expected "meta byte type in its signature must be static")
    endif()
    set(input "${directory}/${case}.x")
    if(case STREQUAL nonstatic_buffer)
        file(WRITE "${input}"
            "global uptr bad(in $::meta::buffer value) { return $::meta::cap(value); }\n")
    else()
        file(WRITE "${input}"
            "[[eval_only]] static uptr bad() { ${body} }\n"
            "global uptr result = bad();\n")
    endif()
    execute_process(COMMAND "${CC}" -S "${input}"
        -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endforeach()

foreach(case unassigned_read frozen_pointer const_write view_overread integer_cast)
    if(case STREQUAL unassigned_read)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(1u32);\n"
            "u8 *pointer = $::meta::data(value);\n"
            "return pointer[0u32];")
        set(expected "read of unassigned buffer byte")
    elseif(case STREQUAL frozen_pointer)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(1u32);\n"
            "u8 *pointer = $::meta::data(value);\n"
            "pointer[0u32] = 7u32;\n"
            "$::meta::bytes frozen = $::meta::freeze(value, 1u32);\n"
            "return pointer[0u32] + $::meta::len(frozen);")
        set(expected "buffer data pointer was used after freeze")
    elseif(case STREQUAL const_write)
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "const u8 *pointer = $::meta::data(value);\n"
            "pointer[0u32] = 7u32;\n"
            "return 0u32;")
        set(expected "meta pointer write requires mutable u8 storage")
    elseif(case STREQUAL view_overread)
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "const u8 *pointer = $::meta::data($::meta::slice(value, 1u32, 2u32));\n"
            "return pointer[2u32];")
        set(expected "meta pointer read is outside its view")
    else()
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "uptr address = (uptr)$::meta::data(value);\n"
            "return address;")
        set(expected "meta data pointers cannot convert to integer")
    endif()
    set(input "${directory}/${case}.x")
    file(WRITE "${input}"
        "[[eval_only]] static uptr bad() { ${body} }\n"
        "global uptr result = bad();\n")
    execute_process(COMMAND "${CC}" -S "${input}"
        -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endforeach()

# Byte materialization is independent of scalar byte order and target uptr
# width. The scalar length still follows the selected target's layout.
foreach(target mips-unknown-elf mipsel-unknown-elf mips64-unknown-elf)
    set(flags -target "${target}")
    if(target STREQUAL mips64-unknown-elf)
        list(APPEND flags -mabi=n64)
        set(length_directive "[.]quad 5")
    else()
        list(APPEND flags -mabi=o32)
        set(length_directive "[.]long 5")
    endif()
    execute_process(COMMAND "${CC}" -S ${flags}
        "${directory}/embed_values.x" -o "${OUTPUT}-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${target} embedded value compile failed\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}-${target}.s" assembly)
    if(NOT assembly MATCHES "original:\n[^\n]*[.]byte 65,0,128,255,33" OR
       NOT assembly MATCHES "copied:\n[^\n]*[.]byte 65,0,128,255,33" OR
       NOT assembly MATCHES "rotated:\n[^\n]*[.]byte 0,128,255,33,65" OR
       NOT assembly MATCHES "asset_size:\n[^\n]*${length_directive}" OR
       NOT assembly MATCHES "static_size:\n[^\n]*${length_directive}")
        message(FATAL_ERROR "${target} emitted incorrect byte data or target-sized length\n${assembly}")
    endif()
endforeach()

execute_process(COMMAND "${CC}" -E "${directory}/embed_values.x"
    -o "${directory}/replay.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "embedded value preprocessing failed\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -S "${directory}/replay.i"
    -o "${OUTPUT}-replay.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "independent preprocessed embed evaluation failed\n${out}\n${err}")
endif()

foreach(case bad_bound empty out_of_bounds pointer buffer)
    if(case STREQUAL bad_bound)
        set(source "static const u8 value[3] = $::embed(\"payload.bin\");\n")
        set(expected "explicit byte-array bound")
    elseif(case STREQUAL empty)
        file(WRITE "${directory}/empty.bin" "")
        set(source "static const u8 value[] = $::embed(\"empty.bin\");\n")
        set(expected "invalid target-sized bound")
    elseif(case STREQUAL out_of_bounds)
        set(source "static const u8 value[] = $::meta::slice($::embed(\"payload.bin\"), 5u32, 1u32);\n")
        set(expected "outside the byte sequence")
    elseif(case STREQUAL pointer)
        set(source "static const u8 *value = $::embed(\"payload.bin\");\n")
        set(expected "meta byte values cannot be used as a runtime scalar")
    else()
        set(source "global $::meta::buffer invalid;\n")
        set(expected "meta values cannot have runtime object storage")
    endif()
    set(input "${directory}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S "${input}" -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endforeach()

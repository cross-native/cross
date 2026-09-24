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
        set(expected "bytes")
    else()
        set(source "static $::meta::buffer invalid(in $::meta::buffer input) { return input; }\n")
        set(expected "buffer evaluation is not implemented")
    endif()
    set(input "${directory}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S "${input}" -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endforeach()

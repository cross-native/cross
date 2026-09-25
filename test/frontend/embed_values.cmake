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

foreach(case unassigned_read frozen_pointer const_write view_overread integer_cast
        non_byte_cast misaligned wide_overread effective_type
        float_effective_type invalid_bool invalid_f80 unassigned_wide
        different_pointer_views array_row_overread array_row_write_overflow
        array_row_misaligned array_row_unassigned array_row_effective_type
        array_row_views array_const_loss array_frozen record_misaligned
        record_out_of_view record_packed_member record_unassigned
        record_effective_type record_const_loss record_bit_field
        record_array_overread)
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
        set(expected "meta pointer write requires mutable scalar storage")
    elseif(case STREQUAL view_overread)
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "const u8 *pointer = $::meta::data($::meta::slice(value, 1u32, 2u32));\n"
            "return pointer[2u32];")
        set(expected "meta pointer read is outside its view")
    elseif(case STREQUAL misaligned)
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "const u32 *pointer = (const u32 *)$::meta::data($::meta::slice(value, 1u32, 4u32));\n"
            "return pointer[0u32];")
        set(expected "misaligned meta pointer access")
    elseif(case STREQUAL wide_overread)
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "const u32 *pointer = (const u32 *)$::meta::data($::meta::slice(value, 0u32, 3u32));\n"
            "return pointer[0u32];")
        set(expected "meta pointer read is outside its view")
    elseif(case STREQUAL effective_type)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(4u32);\n"
            "u32 *word = (u32 *)$::meta::data(value);\n"
            "word[0u32] = 7u32;\n"
            "const u16 *half = (const u16 *)word;\n"
            "return half[0u32];")
        set(expected "meta pointer read violates effective type")
    elseif(case STREQUAL float_effective_type)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(4u32);\n"
            "f32 *real = (f32 *)$::meta::data(value);\n"
            "real[0u32] = 1.5f32;\n"
            "const u32 *bits = (const u32 *)real;\n"
            "return bits[0u32];")
        set(expected "meta pointer read violates effective type")
    elseif(case STREQUAL invalid_bool)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(1u32);\n"
            "u8 *bytes = $::meta::data(value);\n"
            "bytes[0u32] = 2u32;\n"
            "const bool *flag = (const bool *)bytes;\n"
            "return flag[0u32];")
        set(expected "invalid bool representation")
    elseif(case STREQUAL invalid_f80)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(16u32);\n"
            "u8 *bytes = $::meta::data(value);\n"
            "uptr index = 0u32;\n"
            "while (index < 16u32) { bytes[index] = 0u32; ++index; }\n"
            "bytes[8u32] = 1u32;\n"
            "const f80 *real = (const f80 *)bytes;\n"
            "return real[0u32] != 0.0f80;")
        set(expected "invalid f80 representation")
    elseif(case STREQUAL different_pointer_views)
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "const u8 *first = $::meta::data($::meta::slice(value, 0u32, 2u32));\n"
            "const u8 *second = $::meta::data($::meta::slice(value, 2u32, 2u32));\n"
            "return first < second;")
        set(expected "ordering or subtraction requires one compatible view")
    elseif(case STREQUAL unassigned_wide)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(4u32);\n"
            "u8 *bytes = $::meta::data(value);\n"
            "bytes[0u32] = 7u32;\n"
            "const u32 *word = (const u32 *)bytes;\n"
            "return word[0u32];")
        set(expected "read of unassigned buffer byte")
    elseif(case STREQUAL array_row_overread)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(8u32);\n"
            "word_row rows = (word_row)$::meta::data(value);\n"
            "rows[0u32][0u32] = 7u16;\n"
            "return rows[0u32][2u32];")
        set(expected "meta pointer read is outside its view")
    elseif(case STREQUAL array_row_write_overflow)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(8u32);\n"
            "word_row rows = (word_row)$::meta::data(value);\n"
            "rows[0u32][2u32] = 7u16;\n"
            "return 0u32;")
        set(expected "meta pointer write is outside its view")
    elseif(case STREQUAL array_row_misaligned)
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "const_word_row rows = (const_word_row)$::meta::data($::meta::slice(value, 1u32, 4u32));\n"
            "return rows[0u32][0u32];")
        set(expected "misaligned meta pointer access")
    elseif(case STREQUAL array_row_unassigned)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(4u32);\n"
            "word_row rows = (word_row)$::meta::data(value);\n"
            "return rows[0u32][1u32];")
        set(expected "read of unassigned buffer byte")
    elseif(case STREQUAL array_row_effective_type)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(4u32);\n"
            "word_row rows = (word_row)$::meta::data(value);\n"
            "rows[0u32][0u32] = 7u16;\n"
            "rows[0u32][1u32] = 8u16;\n"
            "return ((u32 *)rows)[0u32];")
        set(expected "meta pointer read violates effective type")
    elseif(case STREQUAL array_row_views)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(8u32);\n"
            "word_row rows = (word_row)$::meta::data(value);\n"
            "u16 *first = rows[0u32];\n"
            "u16 *second = rows[1u32];\n"
            "return first < second;")
        set(expected "ordering or subtraction requires one compatible view")
    elseif(case STREQUAL array_const_loss)
        string(CONCAT body
            "const_word_row rows = (const_word_row)$::meta::data($::embed(\"payload.bin\"));\n"
            "word_row writable = (word_row)rows;\n"
            "return 0u32;")
        set(expected "conversion without qualifier loss")
    elseif(case STREQUAL array_frozen)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(4u32);\n"
            "word_row rows = (word_row)$::meta::data(value);\n"
            "rows[0u32][0u32] = 1u16;\n"
            "rows[0u32][1u32] = 2u16;\n"
            "$::meta::bytes frozen = $::meta::freeze(value, 4u32);\n"
            "return rows[0u32][0u32];")
        set(expected "buffer data pointer was used after freeze")
    elseif(case STREQUAL record_misaligned)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(sizeof(struct eval_pair) + 1u32);\n"
            "struct eval_pair *record = (struct eval_pair *)($::meta::data(value) + 1u32);\n"
            "return record->value;")
        set(expected "misaligned meta pointer access for target record type")
    elseif(case STREQUAL record_out_of_view)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(sizeof(struct eval_pair) - 1u32);\n"
            "struct eval_pair *record = (struct eval_pair *)$::meta::data(value);\n"
            "return record->value;")
        set(expected "meta pointer read is outside its view")
    elseif(case STREQUAL record_packed_member)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(sizeof(struct eval_packed));\n"
            "struct eval_packed *record = (struct eval_packed *)$::meta::data(value);\n"
            "record->value = 7uptr;\n"
            "return 0u32;")
        set(expected "packed meta record member access is not implemented")
    elseif(case STREQUAL record_unassigned)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(sizeof(struct eval_pair));\n"
            "struct eval_pair *record = (struct eval_pair *)$::meta::data(value);\n"
            "return record->value;")
        set(expected "read of unassigned buffer byte")
    elseif(case STREQUAL record_effective_type)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(sizeof(struct eval_pair));\n"
            "struct eval_pair *record = (struct eval_pair *)$::meta::data(value);\n"
            "record->value = 7uptr;\n"
            "const u16 *wrong = (const u16 *)($::meta::data(value) + $::target::pointer_bytes);\n"
            "return wrong[0u32];")
        set(expected "meta pointer read violates effective type")
    elseif(case STREQUAL record_const_loss)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(sizeof(struct eval_pair));\n"
            "const struct eval_pair *readonly = (const struct eval_pair *)$::meta::data(value);\n"
            "struct eval_pair *writable = (struct eval_pair *)readonly;\n"
            "return 0u32;")
        set(expected "conversion without qualifier loss")
    elseif(case STREQUAL record_bit_field)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(sizeof(struct eval_bits));\n"
            "struct eval_bits *record = (struct eval_bits *)$::meta::data(value);\n"
            "return record->value;")
        set(expected "meta record member requires a non-bit-field")
    elseif(case STREQUAL record_array_overread)
        string(CONCAT body
            "$::meta::buffer value = $::meta::alloc(sizeof(struct eval_collection));\n"
            "struct eval_collection *record = (struct eval_collection *)$::meta::data(value);\n"
            "return record->values[2u32];")
        set(expected "meta pointer read is outside its view")
    elseif(case STREQUAL integer_cast)
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "uptr address = (uptr)$::meta::data(value);\n"
            "return address;")
        set(expected "meta data pointers cannot convert to integer")
    else()
        string(CONCAT body
            "$::meta::bytes value = $::embed(\"payload.bin\");\n"
            "const f32 **pointer = (const f32 **)$::meta::data(value);\n"
            "return 0u32;")
        set(expected "compatible scalar, fixed-array, record, or void pointer conversion")
    endif()
    set(input "${directory}/${case}.x")
    file(WRITE "${input}"
        "typedef u16 (*word_row)[2];\n"
        "typedef const u16 (*const_word_row)[2];\n"
        "struct eval_pair { u8 tag; uptr value; };\n"
        "struct eval_packed [[packed]] { u8 tag; uptr value; };\n"
        "struct eval_bits { u32 value : 3; };\n"
        "struct eval_collection { u8 tag; u16 values[2]; };\n"
        "[[eval_only]] static uptr bad() { ${body} }\n"
        "global uptr result = bad();\n")
    execute_process(COMMAND "${CC}" -S "${input}"
        -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endforeach()

set(void_escape "${directory}/void_escape.x")
file(WRITE "${void_escape}"
    "global const void *escaped = (const void *)$::meta::data($::embed(\"payload.bin\"));\n")
execute_process(COMMAND "${CC}" -S "${void_escape}"
    -o "${OUTPUT}-void-escape.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "translation-time pointer|runtime scalar|not a scalar translation-time value")
    message(FATAL_ERROR "opaque meta pointer escaped to runtime storage\n${out}\n${err}")
endif()

# Byte materialization is independent of scalar byte order and target uptr
# width. The scalar length still follows the selected target's layout.
foreach(target mips-unknown-elf mipsel-unknown-elf
        mips64-unknown-elf mips64el-unknown-elf)
    set(flags -target "${target}")
    if(target MATCHES "^mips64")
        list(APPEND flags -mabi=n64)
        set(length_directive "[.]quad 5")
        set(success_directive "[.]quad 1")
        set(distance_directive "[.]quad 2")
    else()
        list(APPEND flags -mabi=o32)
        set(length_directive "[.]long 5")
        set(success_directive "[.]long 1")
        set(distance_directive "[.]long 2")
    endif()
    execute_process(COMMAND "${CC}" -S ${flags}
        "${directory}/embed_values.x" -o "${OUTPUT}-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${target} embedded value compile failed\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}-${target}.s" assembly)
    if(target STREQUAL mipsel-unknown-elf OR target STREQUAL mips64el-unknown-elf)
        set(word_bytes "120,86,52,18,240,222,188,154")
        set(row_bytes "34,17,68,51,102,85,136,119")
        set(source_word "4286578753")
        set(float_bytes "0,0,192,63")
        set(double_bytes "0,0,0,0,0,0,0,128")
        set(quad_bytes "0,0,0,0,0,0,0,0,0,0,0,0,0,128,255,63")
        set(extended_bytes "0,0,0,0,0,0,0,192,255,63,0,0,0,0,0,0")
    else()
        set(word_bytes "18,52,86,120,154,188,222,240")
        set(row_bytes "17,34,51,68,85,102,119,136")
        set(source_word "1090552063")
        set(float_bytes "63,192,0,0")
        set(double_bytes "128,0,0,0,0,0,0,0")
        set(quad_bytes "63,255,128,0,0,0,0,0,0,0,0,0,0,0,0,0")
        set(extended_bytes "0,0,0,0,0,0,63,255,192,0,0,0,0,0,0,0")
    endif()
    if(target STREQUAL mips-unknown-elf)
        set(pointer_scalars "18,52,86,120,63,192,0,0")
    elseif(target STREQUAL mipsel-unknown-elf)
        set(pointer_scalars "120,86,52,18,0,0,192,63")
    elseif(target STREQUAL mips64-unknown-elf)
        set(pointer_scalars "0,0,0,0,18,52,86,120,63,248,0,0,0,0,0,0")
    else()
        set(pointer_scalars "120,86,52,18,0,0,0,0,0,0,0,0,0,0,248,63")
    endif()
    if(NOT assembly MATCHES "original:\n[^\n]*[.]byte 65,0,128,255,33" OR
       NOT assembly MATCHES "copied:\n[^\n]*[.]byte 65,0,128,255,33" OR
       NOT assembly MATCHES "rotated:\n[^\n]*[.]byte 0,128,255,33,65" OR
       NOT assembly MATCHES "asset_size:\n[^\n]*${length_directive}" OR
       NOT assembly MATCHES "static_size:\n[^\n]*${length_directive}" OR
       NOT assembly MATCHES "words:\n[^\n]*[.]byte ${word_bytes}" OR
       NOT assembly MATCHES "rows:\n[^\n]*[.]byte ${row_bytes}" OR
       NOT assembly MATCHES "array_view_checked:\n[^\n]*${success_directive}" OR
       NOT assembly MATCHES "target_sized_array_checked:\n[^\n]*${success_directive}" OR
       NOT assembly MATCHES "target_record_layout_checked:\n[^\n]*${success_directive}" OR
       NOT assembly MATCHES "record_member_checked:\n[^\n]*${success_directive}" OR
       NOT assembly MATCHES "record_array_member_checked:\n[^\n]*${success_directive}" OR
       NOT assembly MATCHES "float_bytes:\n[^\n]*[.]byte ${float_bytes}" OR
       NOT assembly MATCHES "double_bytes:\n[^\n]*[.]byte ${double_bytes}" OR
       NOT assembly MATCHES "quad_bytes:\n[^\n]*[.]byte ${quad_bytes}" OR
       NOT assembly MATCHES "extended_bytes:\n[^\n]*[.]byte ${extended_bytes}" OR
       NOT assembly MATCHES "pointer_scalars:\n[^\n]*[.]byte ${pointer_scalars}" OR
       NOT assembly MATCHES "source_word:\n[^\n]*[.]long ${source_word}" OR
       NOT assembly MATCHES "opaque_word:\n[^\n]*[.]long ${source_word}" OR
       NOT assembly MATCHES "opaque_equal:\n[^\n]*${success_directive}" OR
       NOT assembly MATCHES "endian_read_ok:\n[^\n]*[.]long 1" OR
       NOT assembly MATCHES "endian_write_ok:\n[^\n]*[.]long 1" OR
       NOT assembly MATCHES "extended_padding_ok:\n[^\n]*${success_directive}" OR
       NOT assembly MATCHES "meta_pointer_distance:\n[^\n]*${distance_directive}" OR
       NOT assembly MATCHES "meta_pointer_order_ok:\n[^\n]*${success_directive}")
        message(FATAL_ERROR "${target} emitted incorrect embedded values or target-sized length\n${assembly}")
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

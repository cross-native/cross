# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# may_alias is a type attribute: it takes no arguments and does not apply to an
# object. Without it, translation-time evaluation enforces effective types; with
# it, the assertions in SOURCE hold for each byte order.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(reject case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu "${input}"
                            -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${case}\\.x:[0-9]+:[0-9]+: error: ${expected}")
        message(FATAL_ERROR "${case} was not diagnosed with '${expected}'\n${out}\n${err}")
    endif()
endfunction()

reject(arguments "may_alias does not take arguments" "typedef u32 bad [[may_alias(1)]];\n")
reject(global_object "attribute 'may_alias' is not valid on an object" "global u32 x [[may_alias]];\n")
reject(local_object "attribute 'may_alias' is not valid on a local object"
    "global u32 f() { u32 y [[may_alias]] = 1u32; return y; }\n")
reject(member "attribute 'may_alias' is not valid on a record member"
    "struct Pair { u32 first [[may_alias]]; };\n")
reject(plain_record_view "meta pointer read violates effective type" "
struct Pair { u32 first; u32 second; };
static u32 first(in u64 bits) { u64 copy = bits; return ((struct Pair *)(void *)&copy)->first; }
\$::static_assert(first(6u64) == 6u32, \"plain record view\");\n")
reject(plain_view "meta pointer read violates effective type" "
static u32 float_bits(in f32 value) { f32 copy = value; return *(u32 *)(void *)&copy; }
\$::static_assert(float_bits(1.0f32) == 0x3f800000u32, \"plain view\");\n")

foreach(target "mips-unknown-elf;-mabi=o32" "mipsel-unknown-elf;-mabi=o32"
        "mips64-unknown-elf;-mabi=n64" "mips64el-unknown-elf;-mabi=n64")
    list(GET target 0 triple)
    list(GET target 1 abi)
    execute_process(COMMAND "${CC}" -S -O2 -target "${triple}" "${abi}" "${SOURCE}"
                            -o "${OUTPUT}/${triple}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${triple} rejected may_alias accesses\n${out}\n${err}")
    endif()
endforeach()

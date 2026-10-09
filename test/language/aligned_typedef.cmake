# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Typedef alignment: translation-time layout, the emitted objects, and the
# diagnostics of one architecture.
foreach(required CC SOURCE OUTPUT ARCH)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

if(ARCH STREQUAL "x86_64")
    set(targets "x86_64-unknown-linux-gnu" "x86_64-w64-windows-gnu")
elseif(ARCH STREQUAL "mips")
    set(targets "mips-unknown-elf" "mipsel-unknown-elf"
                "mips64-unknown-elf -mabi=n64" "mips64el-unknown-elf -mabi=n64")
else()
    message(FATAL_ERROR "unknown ARCH '${ARCH}'")
endif()

function(check_object assembly label name power)
    if(NOT assembly MATCHES "\\.p2align ${power}\n(\\.[^\n]*\n)*${name}:\n")
        message(FATAL_ERROR "${label}: ${name} is not emitted with .p2align ${power}\n${assembly}")
    endif()
endfunction()

foreach(target IN LISTS targets)
    separate_arguments(target_flags NATIVE_COMMAND "-target ${target}")
    string(REGEX REPLACE "[^A-Za-z0-9]+" "-" name "${target}")
    foreach(level O0 O2)
        set(label "${name}-${level}")
        execute_process(COMMAND "${CC}" -S ${target_flags} -${level} "${SOURCE}"
                                -o "${OUTPUT}/${label}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${label} was rejected\n${out}\n${err}")
        endif()
        file(READ "${OUTPUT}/${label}.s" assembly)
        check_object("${assembly}" "${label}" initialized 4)
        check_object("${assembly}" "${label}" zeroed 6)
        check_object("${assembly}" "${label}" initialized_table 4)
        # A padded scalar stores its value bytes first, in target byte order.
        if(target MATCHES "^mips64-|^mips-")
            set(value "0,0,0,7,0,0,0,0,0,0,0,0,0,0,0,0")
            set(element "0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0")
        else()
            set(value "7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0")
            set(element "2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0")
        endif()
        if(NOT assembly MATCHES "\ninitialized:\n\t\\.byte ${value}\n" OR
           NOT assembly MATCHES "\ninitialized_table:\n\t\\.byte [0-9,]+\n\t\\.byte ${element}\n" OR
           NOT assembly MATCHES "\nzeroed:\n\t\\.zero 64\n" OR
           NOT assembly MATCHES "\ngeneric_aligned:\n\t\\.[a-z]+ 80\n" OR
           NOT assembly MATCHES "\ndeduced_result:\n\t\\.[a-z]+ 4\n")
            message(FATAL_ERROR "${label}: padded object bytes are wrong\n${assembly}")
        endif()
    endforeach()
endforeach()

function(compile case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(expected STREQUAL "")
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${case} was rejected\n${out}\n${err}")
        endif()
    elseif(status EQUAL 0 OR
           NOT err MATCHES "${case}\\.x:[0-9]+:[0-9]+: error: ${expected}")
        message(FATAL_ERROR "${case} was not diagnosed with '${expected}'\n${out}\n${err}")
    endif()
endfunction()

set(power "aligned argument must be a positive power-of-two integer constant")
set(wide "typedef u32 wide_u32 [[aligned(16)]];\n")
if(ARCH STREQUAL "x86_64")
    set(x86 -target x86_64-unknown-linux-gnu)
    compile(zero "${power}" "typedef u32 bad [[aligned(0)]];\n" ${x86})
    compile(three "${power}" "typedef u32 bad [[aligned(3)]];\n" ${x86})
    compile(expression "${power}"
        "typedef u32 bad [[aligned(sizeof(u32) + 1uptr)]];\nglobal bad object;\n" ${x86})
    compile(negative "${power}"
        "typedef u32 bad [[aligned(-16)]];\nglobal bad object;\n" ${x86})
    compile(missing "aligned on a typedef requires one integer argument"
        "typedef u32 bad [[aligned]];\n" ${x86})
    compile(extra "aligned on a typedef requires one integer argument"
        "typedef u32 bad [[aligned(4, 8)]];\n" ${x86})
    compile(void "aligned on a typedef requires an object type"
        "typedef void bad [[aligned(8)]];\n" ${x86})
    compile(callable "aligned on a typedef requires an object type"
        "typedef u32 bad(in u32 value) [[aligned(8)]];\n" ${x86})
    compile(parameter "attribute 'aligned' is not valid on a parameter"
        "global u32 f(in u32 value [[aligned(16)]]) { return value; }\n" ${x86})
    compile(bit_field "a bit-field base type cannot request alignment"
        "${wide}struct bits { wide_u32 value : 3; };\nglobal struct bits object;\n" ${x86})
    compile(redeclaration "typedef 'wide_u32' redeclared with a different type"
        "${wide}typedef u32 wide_u32;\n" ${x86})
    compile(object_redeclaration "incompatible redeclaration of object 'object'"
        "${wide}global wide_u32 object;\nglobal u32 object;\n" ${x86})
    compile(same_redeclaration ""
        "${wide}typedef u32 wide_u32 [[aligned(16)]];\nglobal wide_u32 object;\n" ${x86})
    # A vector typedef's own request applies to the vector; lanes are values.
    compile(vectors "" "${wide}typedef u32 v4 [[vector_size(16), aligned(64)]];
typedef wide_u32 lanes [[vector_size(16)]];
\$::static_assert(sizeof(v4) == 64uptr && \$::alignof(v4) == 64uptr, \"vector request\");
\$::static_assert(sizeof(lanes) == 16uptr && \$::alignof(lanes) == 16uptr, \"lane values\");\n" ${x86})
    if(LLVM_TEXT)
        compile(llvm "LLVM debug serialization does not encode typedef alignment"
            "${wide}global wide_u32 object;\n" ${x86} -emit-llvm)
    endif()
    if(GIMPLE_TEXT)
        compile(gimple "GIMPLE serialization does not encode typedef alignment"
            "${wide}global u32 f() { wide_u32 local = 1u32; return local; }\n" ${x86} -emit-gimple)
    endif()
else()
    compile(realigned_vla "MIPS cannot realign a frame that also has a variable-length allocation"
        "typedef u8 line_u8 [[aligned(64)]];
global u32 f(in u32 count) { line_u8 cell = 1u8; u8 bytes[count]; bytes[0] = cell; return bytes[0]; }\n"
        -target mips-unknown-elf -O0)
endif()

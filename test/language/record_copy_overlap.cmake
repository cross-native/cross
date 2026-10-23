# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A record assignment whose source partially overlaps its destination is
# undefined, so translation-time evaluation diagnoses it; the copies in SOURCE,
# whose overlap is exact or whose source is an object of its own, evaluate
# for each byte order.
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

set(records "
struct pair { u32 a; u32 b; };
struct trio { struct pair low; u32 c; };
struct shifted { u32 skip; struct pair high; };
union view { struct trio t; struct shifted s; };
")
reject(union_members "record assignment source partially overlaps its destination" "${records}
static u32 shift() {
    union view v = { .t = { { 1u32, 2u32 }, 3u32 } };
    v.s.high = v.t.low;
    return v.s.high.a;
}
\$::static_assert(shift() == 1u32, \"members that share four bytes\");\n")
reject(pointers "record assignment source partially overlaps its destination" "${records}
static u32 shift() {
    struct pair cells[2] = { { 1u32, 2u32 }, { 3u32, 4u32 } };
    u32 *words = (u32 *)(void *)cells;
    struct pair *high = (struct pair *)(void *)(words + 1);
    *high = cells[0];
    return cells[1].a;
}
\$::static_assert(shift() == 2u32, \"elements that share four bytes\");\n")

foreach(target "x86_64-unknown-linux-gnu" "mips-unknown-elf;-mabi=o32")
    list(GET target 0 triple)
    execute_process(COMMAND "${CC}" -S -target ${target} "${SOURCE}" -o "${OUTPUT}/${triple}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${triple} rejected defined record copies\n${out}\n${err}")
    endif()
endforeach()

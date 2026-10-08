# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A record quoted from a helper's block keeps its own identity at the
# expansion site: same-spelled tags there neither capture nor reach it.

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# Each expected diagnostic is "line:column: kind: message", matched literally.
function(reject case source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(
        COMMAND "${CC}" -target x86_64-unknown-linux-gnu -S "${OUTPUT}/${case}.x"
                -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1)
        message(FATAL_ERROR "${case}: expected a diagnostic (status ${status})\n${out}\n${err}")
    endif()
    foreach(expected IN LISTS ARGN)
        string(FIND "${err}" "${case}.x:${expected}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${case}: missing '${case}.x:${expected}'\n${err}")
        endif()
    endforeach()
endfunction()

set(helper "[[macro]] static $::meta::tokens pair_type(in $::meta::tokens input) {
    struct Pair { u32 first; };
    return $::quote { struct Pair };
}
")

reject(distinct_site_record "${helper}struct Pair { u32 first; };
global u32 copy() {
    struct Pair site;
    pair_type!() quoted;
    site.first = 1u32;
    quoted = site;
    return quoted.first;
}
"
    "10:12: error: a record value requires the same nominal record type in assignment")

reject(tag_not_exported "${helper}global uptr size() {
    pair_type!() quoted;
    return sizeof(quoted) + sizeof(struct Pair);
}
"
    "7:29: error: sizeof requires a complete object type with fixed size")

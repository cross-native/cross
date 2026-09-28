# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
function(reject case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endfunction()

set(surround [=[
[[syntax_expander]] static $::meta::tokens surround(in $::meta::syntax_match input) {
    return $::quote {
        { u32 $::unquote($::syntax::capture(input, "name")) = 99u32;
          $::unquote($::meta::tokens($::syntax::node(input, "body"))) }
    };
}
syntax Surround : statement { prefix "surround"; match name:ident "," body:stmt; expand surround; }
]=])
reject(const_relocated "cannot (write|assign|modify).*const|read.only"
    "${surround} global u32 entry() { syntax Surround; const u32 value = 7u32; surround value, value += 1u32; return value; }")
reject(duplicate_local "declared more than once"
    "global u32 entry() { u32 value = 1u32; u32 value = 2u32; return value; }")
reject(duplicate_parameter "duplicate parameter name"
    "global u32 entry(in u32 value, in u32 value) { return value; }")
reject(parameter_local_conflict "declared more than once"
    "global u32 entry(in u32 value) { u32 value = 2u32; return value; }")

set(extract [=[
[[syntax_expander]] static $::meta::tokens extract(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax compound = $::meta::child($::meta::child(body, 0uptr), 0uptr);
    return $::meta::tokens($::meta::child(compound, 2uptr));
}
syntax Extract : statement { prefix "extract"; match body:stmt; expand extract; }
]=])
# Removing a parsed declaration leaves its use unbound. It must not fall back
# to an unrelated same-spelled global that remains available in this program.
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    reject(removed_binder_${suffix} "captured local value .* is not visible"
        "${extract} global u32 value = 23u32; global u32 entry() { syntax Extract; extract { u32 value = 7u32; return value; } }"
        ${flags})
endforeach()

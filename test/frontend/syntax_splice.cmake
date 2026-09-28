# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

set(source [=[
[[syntax_expander]] static $::meta::tokens wrong(in $::meta::syntax_match input) {
    return $::quote { return $::unquote($::syntax::node(input, "body")); };
}
syntax Wrong : statement {
    prefix "wrong"; match body:stmt; expand wrong;
}
global u32 entry() {
    syntax Wrong;
    wrong return 1u32;
}
]=])
set(input "${OUTPUT}/category.x")
file(WRITE "${input}" "${source}")
execute_process(COMMAND "${CC}" -S "${input}" -o "${OUTPUT}/category.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 1 OR
   NOT err MATCHES "structured syntax splice requires an expression node" OR
   NOT err MATCHES "category.x:[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR "incompatible splice category was not diagnosed\n${out}\n${err}")
endif()

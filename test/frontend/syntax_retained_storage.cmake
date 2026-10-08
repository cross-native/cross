# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
if(NOT MODE MATCHES "^(native|custom|mips|mipsel|mips64|mips64el)$")
    message(FATAL_ERROR "unknown context-storage mode '${MODE}'")
endif()
file(MAKE_DIRECTORY "${OUTPUT}")
function(compile case source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(result "${result}" PARENT_SCOPE)
    set(err "${out}${err}" PARENT_SCOPE)
endfunction()

set(template [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    @body@
    return $::quote { 1u32 };
}
syntax Inspect : expression { prefix "inspect"; match body:paren; expand expand; }
syntax Inspect;
global u32 entry() { return inspect (); }
]=])

function(reject case expected body)
    string(REPLACE "@body@" "${body}" source "${template}")
    compile(${case} "${source}" ${ARGN})
    if(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${case} context misuse was not diagnosed\n${err}")
    endif()
endfunction()

# Each opaque expression retains the prior node through a structured token.
# Storage must follow those edges even though parsing must not expand them.
set(retained_splices [=[
$::meta::context context = $::syntax::context(input);
$::meta::syntax node = $::meta::parse("expr", $::quote { 1u32 }, context);
for (uptr depth = 0uptr; depth < 12uptr; ++depth)
    node = $::meta::parse("expr", $::quote { 0u32 unknown_operator!() $::unquote(node) }, context);
if (!$::meta::is_kind(node, "deferred"))
    $::syntax::error($::syntax::span(input), "expected retained deferred input");
]=])
set(mode "${MODE}")
set(flags -O0 -fno-eval-calls)
if(mode STREQUAL custom)
    list(APPEND flags "--model=${CMAKE_CURRENT_LIST_DIR}/../model/custom.y" -mprofile=test-profile)
elseif(NOT mode STREQUAL native)
    list(APPEND flags -target "${mode}-unknown-linux-gnu")
endif()
reject(retained_splice_memory_${mode} "translation-time meta memory budget exceeded"
    "${retained_splices}" ${flags} -feval-memory-limit=65536)
reject(retained_splice_bytes_${mode} "public syntax parse output byte budget exceeded"
    "${retained_splices}" ${flags} -feval-memory-limit=1048576 -feval-byte-limit=16384)
string(REPLACE "@body@" "${retained_splices}" source "${template}")
compile(retained_splice_room_${mode} "${source}" ${flags} -feval-memory-limit=1048576)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${mode} retained splices failed with sufficient storage\n${err}")
endif()

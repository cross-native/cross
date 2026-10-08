# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${out}\n${err}")
    endif()
endfunction()

# Cell-layout checks are separately registered by target and optimization level.

set(edit_source [=[
static $::meta::syntax edit(in $::meta::syntax node, in $::meta::syntax replacement) {
    if ($::meta::is_production(node, "argument_list"))
        return $::meta::replace_child(node, 0uptr, replacement);
    for (uptr i = 0uptr; i < $::meta::child_count(node); ++i)
        node = $::meta::replace_child(node, i, edit($::meta::child(node, i), replacement));
    return node;
}
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::meta::syntax original = $::syntax::node(input, "value");
    $::meta::syntax replacement = $::meta::parse("expr", $::quote { REPLACEMENT }, $::syntax::context(input));
    $::meta::syntax changed = edit(original, replacement);
    return $::quote { $::unquote(original) + $::unquote(changed) };
}
syntax Edit : expression { prefix "edit"; match value:expr; expand expand; }
syntax Edit;
global u32 entry() { return edit $::patch(7u32); }
]=])
foreach(replacement 7u32 9u32 7u64)
    string(REPLACE REPLACEMENT "${replacement}" source "${edit_source}")
    set(input "${OUTPUT}/edit-${replacement}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S -O2 "${input}" -o "${input}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(replacement STREQUAL 7u32)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "equivalent copied patch was rejected\n${out}\n${err}")
        endif()
    elseif(NOT status EQUAL 1 OR NOT err MATCHES "copies of one .*patch expression disagree" OR
           NOT err MATCHES "first use of this patch expression is here" OR
           NOT err MATCHES ":[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "conflicting copied patch was not diagnosed\n${out}\n${err}")
    endif()
endforeach()

file(WRITE "${OUTPUT}/raw-copy.x" [=[
[[macro]] static $::meta::tokens twice(in $::meta::tokens input) {
    return $::quote { $::unquote(input) $::unquote(input) };
}
[[naked]] global u64 entry() -> "rax" {
    register u64 value "rax";
    twice!($::_movabs(value, $::patch(7u64));)
    $::_ret();
}
]=])
execute_process(COMMAND "${CC}" -S "${OUTPUT}/raw-copy.x" -o "${OUTPUT}/raw-copy.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 1 OR NOT err MATCHES "raw instruction cannot share one .*patch cell")
    message(FATAL_ERROR "raw copies silently duplicated an exact operand field\n${out}\n${err}")
endif()

file(WRITE "${OUTPUT}/relocatable-copy.x" [=[
global u32 object;
[[macro]] static $::meta::tokens twice(in $::meta::tokens input) {
    return $::quote { ($::unquote(input)) + ($::unquote(input)) };
}
global uptr entry() { return twice!($::patch((uptr)&object)); }
]=])
checked("copied symbolic initial" "${CC}" -c -O2 "${OUTPUT}/relocatable-copy.x"
    -o "${OUTPUT}/relocatable-copy.o")

# Optional LLVM execution has its own backend-labeled CTest case.

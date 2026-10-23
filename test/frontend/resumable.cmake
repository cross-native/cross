# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The resumable extension of doc/metaprogramming.md: resumable.xh is the
# manual's extension, scripts that keep their state in their record compile
# without a warning, and a local that a yield loses is reported with notes at
# the dispatch and at the yield.
foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
set(fixtures "${SOURCE_DIR}/test/frontend")

file(READ "${SOURCE_DIR}/doc/metaprogramming.md" manual)
string(REPLACE "\r\n" "\n" manual "${manual}")
string(FIND "${manual}" "### Example: resumable functions" section)
string(SUBSTRING "${manual}" ${section} -1 manual)
file(READ "${fixtures}/resumable.xh" copy)
foreach(text manual copy)
    string(FIND "${${text}}" "namespace script {" begin)
    string(SUBSTRING "${${text}}" ${begin} -1 ${text})
    string(FIND "${${text}}" "\n}\n" end)
    string(SUBSTRING "${${text}}" 0 ${end} ${text})
endforeach()
if(NOT manual STREQUAL copy)
    message(FATAL_ERROR "resumable.xh differs from the example in doc/metaprogramming.md")
endif()

execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu -Wall -Wextra
    "${fixtures}/resumable.x" -o "${OUTPUT}/resumable.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT "${out}${err}" STREQUAL "")
    message(FATAL_ERROR "resumable.x: expected no diagnostic\n${out}${err}")
endif()

file(WRITE "${OUTPUT}/lost.x" [=[
#include "resumable.xh"
syntax script::resumable;
struct walker { i32 x; label resume; };
resumable(self->resume) void walk(struct walker *self) {
    for (u32 steps = 0; steps < 3; ++steps) {
        self->x += 1;
        yield;
    }
    self->x = 0;
}
resumable(self->resume) void hop(struct walker *self) {
    i32 step = self->x;
    if (step > 2) yield;
    self->x = step + 1;
}
]=])
execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu -Wall -I "${fixtures}"
    "${OUTPUT}/lost.x" -o "${OUTPUT}/lost.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
string(REGEX MATCHALL "warning: " reports "${err}")
list(LENGTH reports count)
if(NOT status EQUAL 0 OR NOT count EQUAL 2)
    message(FATAL_ERROR "lost.x: expected two warnings\n${out}${err}")
endif()
foreach(expected
        "warning: 'steps' is used uninitialized \\[-Wuninitialized\\]"
        "lost.x:5:36: note: token supplied from here"
        "lost.x:5:14: note: 'steps' was declared here"
        "lost.x:4:1: note: this computed goto reaches label 'resume' with 'steps' unassigned"
        "lost.x:7:14: note: label 'resume' is here"
        "warning: 'step' may be used uninitialized \\[-Wmaybe-uninitialized\\]"
        "lost.x:12:9: note: 'step' was declared here"
        "lost.x:11:1: note: this computed goto reaches label 'resume' with 'step' unassigned"
        "lost.x:13:24: note: label 'resume' is here")
    if(NOT err MATCHES "${expected}")
        message(FATAL_ERROR "lost.x: missing '${expected}'\n${err}")
    endif()
endforeach()

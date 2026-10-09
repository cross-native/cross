# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs mips_caller.x and mips_callee.x, compiled separately under ABI for one
# CPU and byte order, at -O0, -O2 with and without shrink-wrapping and with a
# frame pointer, -Os, and with the callee at -O0 under an -O2 caller.
foreach(required CC ABI OUTPUT MIPS_CPUS MIPS_ENDIANS)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()
set(here "${CMAKE_CURRENT_LIST_DIR}")
if(ABI STREQUAL "cross64")
    set(WIDE 1)
else()
    set(WIDE 0)
endif()
set(base "${OUTPUT}")
set(STARTUP "${base}-start.s")
configure_file("${here}/mips_start.s.in" "${STARTUP}" @ONLY)
set(SOURCE "${here}/mips_caller.x")
set(CALLEE_SOURCE "${here}/mips_callee.x")
set(EXPECTED P)
set(MIPS_SINGLE_CASE ON)
# Each entry: name|flags[|extra callee flags], flags separated by commas.
foreach(build "O0|-O0" "O2|-O2" "O2-nosw|-O2,-fno-shrink-wrap"
              "O2-fp|-O2,-fno-omit-frame-pointer" "Os|-Os" "mixed|-O2|-O0")
    string(REPLACE "|" ";" parts "${build}")
    list(GET parts 0 name)
    list(GET parts 1 flags)
    set(CALLEE_FLAGS "")
    list(LENGTH parts count)
    if(count GREATER 2)
        list(GET parts 2 CALLEE_FLAGS)
    endif()
    string(REPLACE "," ";" flags "${flags}")
    set(CC_FLAGS -mabi=${ABI} ${flags})
    set(OUTPUT "${base}-${name}")
    include("${here}/../../../language/semantics/mips.cmake")
endforeach()

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Debug entries: the shipped entries, -g, -g=NAME, and -g0 selection through
# profiles, their inspection, and the load-time checks of every entry.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")
set(source "${OUTPUT}/source.x")
file(WRITE "${source}" "global u32 identity(u32 value) {
    return value;
}
")

function(run label)
    execute_process(COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# The shipped entries and the selection that --print-options reports.
run("print-models" --print-models)
expect(print-models "${stdout}"
    "\ndebug dwarf  format: dwarf  version: 5  frame_section: debug_frame  lines: on  frames: on  variables: on  types: on  source: shipped:common[.]xm:[0-9]+\n"
    "\ndebug dwarf-lines  format: dwarf  version: 5  frame_section: debug_frame  lines: on  frames: on  variables: off  types: off  source: shipped:common[.]xm:[0-9]+\n")

function(expect_selection label value origin)
    run(${label} -target x86_64-unknown-linux-gnu ${ARGN} --print-options=common)
    if(NOT stdout MATCHES "(^|\n)g  type: text  value: ${value}  origin: ${origin}  ")
        message(FATAL_ERROR "${label}: expected g = '${value}' (${origin})\n${stdout}")
    endif()
endfunction()
expect_selection(none "" default)
expect_selection(plain dwarf command-line -g)
expect_selection(named dwarf-lines command-line -g=dwarf-lines)
expect_selection(off "" command-line -g -g0)
expect_selection(last dwarf command-line -g0 -g)

# A profile names the entry -g selects; -g=NAME and -g0 still decide.
set(model "${OUTPUT}/profile.xm")
file(WRITE "${model}" [=[
debug "lines-only" {
    format = "dwarf";
    frames = false;
    variables = false;
    types = false;
}

profile "debug-profile" {
    target = "x86_64-unknown-linux-gnu";
    debug = "lines-only";
}
]=])
expect_selection(profile lines-only command-line
    "--model=${model}" -mprofile=debug-profile -g)
expect_selection(profile-named dwarf command-line
    "--model=${model}" -mprofile=debug-profile -g=dwarf)
expect_selection(profile-off "" default "--model=${model}" -mprofile=debug-profile)
run(profiles "--model=${model}" --print-profiles)
expect(profiles "${stdout}" "debug-profile  target: x86_64-unknown-linux-gnu  debug: lines-only\n")
run(models "--model=${model}" --print-models)
expect(models "${stdout}"
    "\ndebug lines-only  format: dwarf  version: 5  frame_section: debug_frame  lines: on  frames: off  variables: off  types: off  source: [^\n]*profile[.]xm:1\n")

# The selected entry decides the contents: no frames for lines-only.
run(profile-object "--model=${model}" -mprofile=debug-profile -g -S
    "${source}" -o "${OUTPUT}/profile.s")
file(READ "${OUTPUT}/profile.s" assembly)
expect(profile-object "${assembly}" "\n[.]loc 0 1 " "[.]section [.]debug_info,")
if(assembly MATCHES "[.]cfi_")
    message(FATAL_ERROR "lines-only described frames\n${assembly}")
endif()

function(expect_command_error label expected)
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu
            "${source}" -o "${OUTPUT}/${label}.out" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "cc: error: ${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}'\n${stderr}")
    endif()
endfunction()
expect_command_error(unknown-entry "unknown debug entry 'missing' from -g=missing"
    -S -g=missing)
expect_command_error(empty-entry "missing debug entry name after '-g='" -S -g=)
expect_command_error(llvm-text "-g is not implemented for -emit-llvm and -emit-gimple output"
    -g -emit-llvm)

# Load-time checks report the file and line of the entry or property.
function(expect_rejected label line expected text)
    set(rejected "${OUTPUT}/${label}.xm")
    file(WRITE "${rejected}" "${text}")
    execute_process(COMMAND "${CC}" "--model=${rejected}"
            -target x86_64-unknown-linux-gnu -S "${source}"
            -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR
       NOT stderr MATCHES "${label}[.]xm:${line}: error: ${expected}")
        message(FATAL_ERROR
            "${label} did not report '${expected}' at line ${line}\n${stderr}")
    endif()
endfunction()
expect_rejected(debug-format 2
    "unknown debug format 'stabs'; the implemented format is 'dwarf'" [=[
debug "d" {
    format = "stabs";
}
]=])
expect_rejected(debug-missing-format 1 "missing required model property 'format'" [=[
debug "d" {
    lines = true;
}
]=])
expect_rejected(debug-version 3 "the dwarf debug format implements version 5" [=[
debug "d" {
    format = "dwarf";
    version = 4;
}
]=])
expect_rejected(debug-frame-section 3
    "debug frame_section must be 'debug_frame' or 'eh_frame'" [=[
debug "d" {
    format = "dwarf";
    frame_section = "frames";
}
]=])
expect_rejected(debug-flag 3 "model property 'types' must be boolean" [=[
debug "d" {
    format = "dwarf";
    types = "yes";
}
]=])
expect_rejected(debug-property 3 "unknown model property 'macros'" [=[
debug "d" {
    format = "dwarf";
    macros = true;
}
]=])
expect_rejected(debug-block 3 "expected '=' after model property name" [=[
debug "d" {
    format = "dwarf";
    rule "r" { }
}
]=])
expect_rejected(debug-duplicate 1 "duplicate debug model 'dwarf'" [=[
debug "dwarf" {
    format = "dwarf";
}
]=])
expect_rejected(profile-debug 1 "profile 'p' names unknown debug entry 'missing'" [=[
profile "p" {
    debug = "missing";
}
]=])

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -fstack-usage writes GCC's .su lines beside the output: the position and
# link name of each emitted function, the stack it uses itself, and whether
# it also allocates at run time.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}/src")
configure_file("${SOURCE}" "${OUTPUT}/src/stack_usage.x" COPYONLY)
set(input "${OUTPUT}/src/stack_usage.x")
set(map "-ffile-prefix-map=${OUTPUT}/src=/src")

function(run label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr WORKING_DIRECTORY "${OUTPUT}")
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
endfunction()

function(expect_report path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "no stack usage report ${path}")
    endif()
    file(READ "${path}" report)
    foreach(pattern IN LISTS ARGN)
        if(NOT report MATCHES "${pattern}")
            message(FATAL_ERROR "${path} lacks '${pattern}'\n${report}")
        endif()
    endforeach()
    string(REGEX MATCHALL "[^\n]+\n" lines "${report}")
    list(LENGTH lines count)
    if(NOT count EQUAL 5)
        message(FATAL_ERROR "${path} has ${count} lines, not one per function\n${report}")
    endif()
endfunction()

# x86-64 counts the return address, as GCC does.
run("x86-64 -S" "${CC}" -S -O2 -fstack-usage -DX86_64 -target x86_64-unknown-linux-gnu
    "${map}" "${input}" -o "${OUTPUT}/x86.s")
expect_report("${OUTPUT}/x86.su"
    "^/src/stack_usage[.]x:4:21:leaf\t8\tstatic\n"
    "\n/src/stack_usage[.]x:12:8:framed\t1[0-9][0-9]\tstatic\n"
    "\n/src/stack_usage[.]x:18:8:dynamic\t[0-9]+\tdynamic\n"
    "\n/src/stack_usage[.]x:27:8:pushes\t16\tstatic\n")

# MIPS frames, in an object's report; a naked function has no stack.
foreach(config "o32;mips-unknown-elf;o32" "n64;mips64el-unknown-elf;n64")
    list(GET config 0 name)
    list(GET config 1 triple)
    list(GET config 2 abi)
    run("${name} -c" "${CC}" -c -O2 -fstack-usage -target ${triple} -mabi=${abi}
        "${map}" "${input}" -o "${OUTPUT}/${name}.o")
    expect_report("${OUTPUT}/${name}.su"
        "^/src/stack_usage[.]x:4:21:leaf\t0\tstatic\n"
        "\n/src/stack_usage[.]x:12:8:framed\t1[0-9][0-9]\tstatic\n"
        "\n/src/stack_usage[.]x:18:8:dynamic\t[0-9]+\tdynamic\n"
        "\n/src/stack_usage[.]x:37:8:bare\t0\tstatic\n")
endforeach()

# Without -o the report follows the output's default name; without
# -fstack-usage there is none.
run("default name" "${CC}" -S -fstack-usage -target mips-unknown-elf src/stack_usage.x)
expect_report("${OUTPUT}/stack_usage.su" "^src/stack_usage[.]x:4:21:leaf\t")
run("no report" "${CC}" -S -target mips-unknown-elf "${input}" -o "${OUTPUT}/plain.s")
if(EXISTS "${OUTPUT}/plain.su")
    message(FATAL_ERROR "a report without -fstack-usage")
endif()

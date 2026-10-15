# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Each case of initializer_overlap.x reports the overlap at the later entry,
# on either target.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(overlap "error: aggregate initializer entry overlaps the destination of an earlier entry")
set(duplicate "error: duplicate destination in aggregate initializer")
foreach(case "CONTAINER_AFTER_MEMBER|18:34" "MEMBER_AFTER_CONTAINER|23:41"
             "STATIC|27:37" "EVALUATED|30:34" "UNCALLED|36:34" "UNTAKEN|42:38"
             "PROMOTED|49:34" "ARRAY|54:49" "UNION|59:44" "ANONYMOUS_UNION|64:32"
             "POSITIONAL|69:48" "NESTED_DUPLICATE|75:53|duplicate"
             "NESTED_OVERLAP|81:68")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 name)
    list(GET fields 1 position)
    set(message "${overlap}")
    list(LENGTH fields count)
    if(count EQUAL 3)
        set(message "${duplicate}")
    endif()
    foreach(target x86_64-unknown-linux-gnu mips-unknown-elf)
        execute_process(
            COMMAND "${CC}" -S -O0 -target ${target} -D${name} "${SOURCE}"
                    -o "${OUTPUT}-${name}-${target}.s"
            RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout
            ERROR_VARIABLE stderr)
        if(status EQUAL 0)
            message(FATAL_ERROR "${name} (${target}) compiled")
        endif()
        string(FIND "${stderr}" "initializer_overlap.x:${position}: ${message}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR
                "${name} (${target}) lacks '${message}' at ${position}\n${stdout}\n${stderr}")
        endif()
    endforeach()
endforeach()

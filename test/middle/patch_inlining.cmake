# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Each error case of patch_inlining.x reports the copied $::patch with a note
# at every function that emits it, on either target.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(error "error: required inlining emits this $::patch in more than one function")
# case, error position, note positions and texts
foreach(case "TWO_CALLERS|9:49|10:41: note: inlined here|11:42: note: inlined here"
             "OUT_OF_LINE|13:49|13:26: note: emitted out of line in 'patched'|14:41: note: inlined here"
             "ADDRESS|16:49|18:41: note: inlined here|17:27: note: inlined here"
             "NESTED|20:47|22:41: note: inlined here|23:42: note: inlined here")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 name)
    list(GET fields 1 position)
    list(SUBLIST fields 2 -1 notes)
    foreach(target x86_64-unknown-linux-gnu mips-unknown-elf)
        foreach(level O0 O2)
            execute_process(
                COMMAND "${CC}" -S -${level} -target ${target} -D${name} "${SOURCE}"
                        -o "${OUTPUT}-${name}-${target}-${level}.s"
                RESULT_VARIABLE status
                OUTPUT_VARIABLE stdout
                ERROR_VARIABLE stderr)
            if(status EQUAL 0)
                message(FATAL_ERROR "${name} (${target}, ${level}) compiled")
            endif()
            foreach(expected "${position}: ${error}" ${notes})
                string(FIND "${stderr}" "patch_inlining.x:${expected}" found)
                if(found EQUAL -1)
                    message(FATAL_ERROR
                        "${name} (${target}, ${level}) lacks '${expected}'\n${stdout}\n${stderr}")
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()

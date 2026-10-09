# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under o32 and cross32 a call preserves only the low 32 bits of s0-s7, so on
# VR4300 a u64 live across a call is reloaded after it. Under cross64 and
# cross-n64 the whole register survives and the value may stay in it.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

# Each entry: name|flags separated by commas|split or kept.
foreach(case "o32|-target,mips-unknown-elf,-march=vr4300,-mabi=o32|split"
             "cross32|-target,mips-unknown-elf,-march=vr4300,-mabi=cross32|split"
             "cross64|-target,mips-unknown-elf,-march=vr4300,-mabi=cross64|kept"
             "cross-n64|-mprofile=mips64-n64|kept")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 name)
    list(GET parts 1 flags)
    list(GET parts 2 expected)
    string(REPLACE "," ";" flags "${flags}")
    execute_process(COMMAND "${CC}" -S -O2 ${flags} "${SOURCE}"
                            -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: compilation failed\n${errors}")
    endif()
    file(READ "${OUTPUT}-${name}.s" text)
    # wide_one keeps one u64 across its call to narrow_callee.
    string(FIND "${text}" "wide_one:" begin)
    string(SUBSTRING "${text}" ${begin} -1 body)
    string(FIND "${body}" "narrow_callee" call)
    string(SUBSTRING "${body}" ${call} -1 after)
    string(FIND "${after}" ".return:" end)
    string(SUBSTRING "${after}" 0 ${end} after)
    set(reload "[\t ]ld[\t ]+[$][a-z0-9]+,[0-9]+[(][$](sp|29)[)]")
    if(expected STREQUAL "split")
        if(NOT after MATCHES "${reload}")
            message(FATAL_ERROR
                "${name}: the u64 is not reloaded after the call\n${body}")
        endif()
    elseif(after MATCHES "${reload}" OR
           NOT after MATCHES "[$](s[0-7]|1[6-9]|2[0-3])[,\n]")
        message(FATAL_ERROR
            "${name}: the u64 did not stay in an s register\n${body}")
    endif()
endforeach()

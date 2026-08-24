# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

function(compile_case name source)
    execute_process(
        COMMAND "${CC}" -S -O2 -target x86_64-w64-windows-gnu
                "${source}" -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE assembly_status
        OUTPUT_VARIABLE assembly_stdout
        ERROR_VARIABLE assembly_stderr
    )
    if(NOT assembly_status EQUAL 0)
        message(FATAL_ERROR
            "${name} COFF assembly failed\n${assembly_stdout}\n${assembly_stderr}")
    endif()
    execute_process(
        COMMAND "${CC}" -c -O2 -target x86_64-w64-windows-gnu
                "${source}" -o "${OUTPUT}-${name}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr
    )
    if(NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "${name} COFF object failed\n${object_stdout}\n${object_stderr}")
    endif()
    file(READ "${OUTPUT}-${name}.s" assembly)
    foreach(pattern ".seh_proc" ".seh_endprologue" ".seh_endproc")
        string(FIND "${assembly}" "${pattern}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${name} lacks ${pattern}\n${assembly}")
        endif()
    endforeach()
endfunction()

compile_case(aligned "${ALIGNED_SOURCE}")
compile_case(vla "${VLA_SOURCE}")
compile_case(probed "${PROBE_SOURCE}")

file(READ "${OUTPUT}-aligned.s" aligned)
foreach(pattern ".seh_pushreg %rbp" ".seh_setframe %rbp,")
    string(FIND "${aligned}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "aligned COFF unwind lacks ${pattern}\n${aligned}")
    endif()
endforeach()

file(READ "${OUTPUT}-vla.s" vla)
foreach(pattern ".seh_pushreg %rbp" ".seh_pushreg %r15"
                ".seh_setframe %r15,")
    string(FIND "${vla}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "VLA COFF unwind lacks ${pattern}\n${vla}")
    endif()
endforeach()

file(READ "${OUTPUT}-probed.s" probed)
foreach(pattern "frame.probe" ".seh_pushreg %rbp" ".seh_setframe %rbp,")
    string(FIND "${probed}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "probed COFF unwind lacks ${pattern}\n${probed}")
    endif()
endforeach()

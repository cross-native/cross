# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(features
    -mcx16 -mrdtscp -mxsave -mxsaveopt -mxsavec -mxsaves -mfsgsbase
    -mrdpid -mpku -mclflushopt -mclwb -mprefetchw -mmonitor -mwaitpkg
    -minvpcid -mcldemote -mmovdiri -mmovdir64b -menqcmd -mwbnoinvd)

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu)
    string(REPLACE "-" "_" suffix "${target}")
    execute_process(
        COMMAND "${CC}" -S ${features} -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${suffix}.s"
        RESULT_VARIABLE assembly_status
        OUTPUT_VARIABLE assembly_stdout
        ERROR_VARIABLE assembly_stderr
    )
    execute_process(
        COMMAND "${CC}" -c ${features} -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${suffix}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr
    )
    if(NOT assembly_status EQUAL 0 OR NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "system raw ISA forms failed for ${target}\n${assembly_stdout}\n"
            "${assembly_stderr}\n${object_stdout}\n${object_stderr}")
    endif()
    file(READ "${OUTPUT}-${suffix}.s" assembly)
    foreach(opcode
            rdtscp xgetbv xsetbv rdfsbaseq rdgsbaseq wrfsbaseq wrgsbaseq
            rdpid rdpkru wrpkru prefetchnta prefetcht0 prefetchw clflush
            clflushopt clwb fxsave64 fxrstor64 xsave64 xrstor64 xsaveopt64
            xsavec64 xsaves64 xrstors64 stmxcsr ldmxcsr cmpxchg8b cmpxchg16b
            monitor mwait umonitor umwait tpause invpcid cldemote movdiri
            movdir64b enqcmd enqcmds wbnoinvd)
        if(NOT assembly MATCHES "[\t ]${opcode}([\t \r\n]|$)")
            message(FATAL_ERROR
                "system raw output for ${target} is missing ${opcode}\n${assembly}")
        endif()
    endforeach()
endforeach()

function(reject_feature name flag instruction feature)
    execute_process(
        COMMAND "${CC}" -S ${features} "${flag}" "${SOURCE}"
                -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0 OR NOT stderr MATCHES
       "target instruction '[$]::${instruction}' requires feature '${feature}'")
        message(FATAL_ERROR
            "${name} system feature gate was not enforced\n${stdout}\n${stderr}")
    endif()
endfunction()

reject_feature(rdtscp -mno-rdtscp _rdtscp rdtscp)
reject_feature(cx16 -mno-cx16 _lock_cmpxchg16b cx16)
reject_feature(waitpkg -mno-waitpkg _umonitor waitpkg)
reject_feature(movdir64b -mno-movdir64b _movdir64b movdir64b)

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_stderr
)
if(NOT registry_status EQUAL 0)
    message(FATAL_ERROR "--print-instructions failed\n${registry_stderr}")
endif()
foreach(entry _rdtscp _xsave _rdfsbase _clflushopt _lock_cmpxchg16b
              _umonitor _invpcid _movdir64b _enqcmd _wbnoinvd)
    string(FIND "${registry}" "$::${entry} instruction" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "instruction registry is missing $::${entry}")
    endif()
endforeach()

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -march=haswell "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE assembly_status
    OUTPUT_VARIABLE assembly_stdout
    ERROR_VARIABLE assembly_stderr
)
execute_process(
    COMMAND "${CC}" -c -march=haswell "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_stdout
    ERROR_VARIABLE object_stderr
)
if(NOT assembly_status EQUAL 0 OR NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "feature-gated raw ISA forms failed\n${assembly_stdout}\n"
        "${assembly_stderr}\n${object_stdout}\n${object_stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)
foreach(opcode andnq tzcntq shlxq rorxq pdepq pextq popcntq lzcntq crc32q)
    if(NOT assembly MATCHES "[\t ]${opcode}[\t ]")
        message(FATAL_ERROR "raw ISA output is missing ${opcode}\n${assembly}")
    endif()
endforeach()

function(reject_feature name flag instruction feature)
    execute_process(
        COMMAND "${CC}" -S -march=haswell "${flag}" "${SOURCE}"
                -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0 OR
       NOT stderr MATCHES
           "target instruction '[$]::${instruction}' requires feature '${feature}'")
        message(FATAL_ERROR
            "${name} feature gate was not enforced\n${stdout}\n${stderr}")
    endif()
endfunction()

reject_feature(bmi -mno-bmi _andn bmi)
reject_feature(bmi2 -mno-bmi2 _shlx bmi2)
reject_feature(popcnt -mno-popcnt _popcnt popcnt)
reject_feature(lzcnt -mno-lzcnt _lzcnt lzcnt)
reject_feature(sse42 -mno-sse4.2 _crc32 sse4.2)

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_stderr
)
if(NOT registry_status EQUAL 0)
    message(FATAL_ERROR "--print-instructions failed\n${registry_stderr}")
endif()
foreach(entry
        "_andn instruction [bmi]"
        "_shlx instruction [bmi2]"
        "_popcnt instruction [popcnt]"
        "_lzcnt instruction [lzcnt]"
        "_crc32 instruction [sse4.2]")
    string(FIND "${registry}" "${entry}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "instruction registry is missing ${entry}\n${registry}")
    endif()
endforeach()

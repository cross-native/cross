# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE assembly_status
    OUTPUT_VARIABLE assembly_output
    ERROR_VARIABLE assembly_error
)
if(NOT assembly_status EQUAL 0)
    message(FATAL_ERROR
        "readable-symbol assembly failed\n${assembly_output}\n${assembly_error}")
endif()
file(READ "${OUTPUT}.s" assembly)
foreach(expected
        ".globl \"math::twice\""
        "\"math::twice\":"
        ".globl \"app::entry\""
        "\"app::entry\":")
    string(FIND "${assembly}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "default mangler did not preserve '${expected}'\n${assembly}")
    endif()
endforeach()

# The object handoff proves that GNU/LLVM assembly quoting accepts the scoped
# spelling and that quotes are not part of the object symbol.
execute_process(
    COMMAND "${CC}" -c "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_output
    ERROR_VARIABLE object_error
)
if(NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "readable-symbol object emission failed\n${object_output}\n${object_error}")
endif()

execute_process(
    COMMAND "${CC}" -S -mmangling=simple
            "${SOURCE}" -o "${OUTPUT}.encoded.s"
    RESULT_VARIABLE encoded_status
    OUTPUT_VARIABLE encoded_output
    ERROR_VARIABLE encoded_error
)
if(NOT encoded_status EQUAL 0)
    message(FATAL_ERROR
        "configured mangler failed\n${encoded_output}\n${encoded_error}")
endif()
file(READ "${OUTPUT}.encoded.s" encoded)
foreach(expected "_XN4math5twice:" "_XN3app5entry:")
    string(FIND "${encoded}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "selected simple mangler lost '${expected}'\n${encoded}")
    endif()
endforeach()

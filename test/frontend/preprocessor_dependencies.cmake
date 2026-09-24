# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CPP CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(local_dir "${OUTPUT}.local")
set(user_dir "${OUTPUT}.user")
set(system_dir "${OUTPUT}.system")
file(MAKE_DIRECTORY "${local_dir}" "${user_dir}" "${system_dir}")
file(WRITE "${local_dir}/asset # $.h"
    "#pragma once\n#define ASSET_VALUE 7\n")
file(WRITE "${local_dir}/inactive.h" "#error inactive include selected\n")
file(WRITE "${user_dir}/shared.h" "#define SHARED_VALUE 11\n")
file(WRITE "${system_dir}/shared.h" "#error wrong search order\n")
file(WRITE "${system_dir}/system.h" "#define SYSTEM_VALUE 13\n")
set(source "${local_dir}/main.x")
file(WRITE "${source}"
    "#define TAKE 1\n#if TAKE\n#include \"asset # $.h\"\n"
    "#else\n#include \"inactive.h\"\n#endif\n"
    "#include \"asset # $.h\"\n#include <shared.h>\n"
    "#include <system.h>\n"
    "global i32 answer = ASSET_VALUE + SHARED_VALUE + SYSTEM_VALUE;\n")
file(WRITE "${local_dir}/second.x" "global i32 second = 2;\n")
set(search -I "${user_dir}" -isystem "${system_dir}")

foreach(tool CPP CC)
    execute_process(COMMAND "${${tool}}" -M ${search} "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE dep ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${tool} -M failed\n${dep}\n${err}")
    endif()
    foreach(fragment "main.x" "asset\\ \\#\\ $$.h" "inactive.h"
                     "shared.h" "system.h")
        string(FIND "${dep}" "${fragment}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "${tool} dependency output lacks '${fragment}'\n${dep}")
        endif()
    endforeach()
    execute_process(COMMAND "${${tool}}" -MM ${search} "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE mm ERROR_VARIABLE err)
    if(NOT status EQUAL 0 OR NOT mm STREQUAL dep)
        message(FATAL_ERROR "${tool} -MM differs from explicit-directory -M\n${mm}\n${err}")
    endif()
endforeach()

execute_process(COMMAND "${CPP}" -MD ${search} "${source}"
    -o "${OUTPUT}.i" -MF "${OUTPUT}.d"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cpp -MD failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.i" expanded)
if(NOT expanded MATCHES "global i32 answer = 7 [+] 11 [+] 13;")
    message(FATAL_ERROR "includes and macros did not expand in order\n${expanded}")
endif()
file(READ "${OUTPUT}.d" depfile)
if(NOT depfile STREQUAL dep)
    message(FATAL_ERROR "-MD dependency file differs from -M\n${depfile}\n${dep}")
endif()

execute_process(COMMAND "${CPP}" -MD "-I${user_dir}"
    "-isystem${system_dir}" "${source}" -o "${OUTPUT}.default.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT EXISTS "${OUTPUT}.default.d")
    message(FATAL_ERROR "default -MD depfile or joined search paths failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.default.d" default_dep)
if(NOT default_dep STREQUAL dep)
    message(FATAL_ERROR "default -MD prerequisites differ\n${default_dep}\n${dep}")
endif()

execute_process(COMMAND "${CPP}" -M "${OUTPUT}.i"
    RESULT_VARIABLE status OUTPUT_VARIABLE preprocessed_dep ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT preprocessed_dep MATCHES "preprocessor-dependencies\\.i")
    message(FATAL_ERROR "preprocessed primary dependency failed\n${preprocessed_dep}\n${err}")
endif()
if(preprocessed_dep MATCHES "shared.h")
    message(FATAL_ERROR "preprocessed primary retained old includes\n${preprocessed_dep}")
endif()

execute_process(COMMAND "${CPP}" -MMD ${search} "${source}"
    "${local_dir}/second.x" -MF "${OUTPUT}.multi.d"
    -MQ "target # $" -MT raw_target
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "multi-input -MMD failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.multi.d" multi)
string(FIND "${multi}" "target\\ \\#\\ $$ raw_target:" escaped_targets)
string(REGEX MATCHALL "raw_target:" targets "${multi}")
list(LENGTH targets target_count)
if(escaped_targets EQUAL -1 OR NOT target_count EQUAL 2 OR
   NOT multi MATCHES "second.x")
    message(FATAL_ERROR "expected two dependency rules\n${multi}")
endif()

execute_process(COMMAND "${CC}" -MD -S ${search} "${source}"
    -o "${OUTPUT}.s" -MF "${OUTPUT}.cc.d"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cc -MD compile failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}.cc.d" ccdep)
if(NOT ccdep MATCHES "preprocessor-dependencies\\.s:")
    message(FATAL_ERROR "cc dependency target ignored -o\n${ccdep}")
endif()

execute_process(COMMAND "${CPP}" -MF "${OUTPUT}.invalid.d" "${source}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "require -M")
    message(FATAL_ERROR "orphan -MF was accepted\n${out}\n${err}")
endif()

execute_process(COMMAND "${CPP}" -MD ${search} "${source}"
    -o "${OUTPUT}.collision.d"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "must be different paths")
    message(FATAL_ERROR "dependency output collision was accepted\n${out}\n${err}")
endif()

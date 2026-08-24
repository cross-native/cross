# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED LARGE_SOURCE OR "${LARGE_SOURCE}" STREQUAL "")
    message(FATAL_ERROR "LARGE_SOURCE must name a path")
endif()

execute_process(
    COMMAND "${CC}" -S -O2 -fpic -target x86_64-unknown-linux-gnu
            "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "TLS assembly failed (${status})\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)
foreach(pattern ".tdata" "awT" "@tls_object" "%fs:0"
                "local_tls@TPOFF" "external_tls@GOTTPOFF")
    string(FIND "${assembly}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "TLS assembly lacks '${pattern}'\n${assembly}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -c -O2 -fpic -target x86_64-unknown-linux-gnu
            "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "TLS object failed (${status})\n${stdout}\n${stderr}")
endif()

execute_process(
    COMMAND "${CC}" -emit-llvm -O2 -target x86_64-unknown-linux-gnu
            "${SOURCE}" -o "${OUTPUT}.ll"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "TLS LLVM debug output failed (${status})\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.ll" llvm)
foreach(pattern "thread_local(localexec)" "thread_local(initialexec)")
    string(FIND "${llvm}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "TLS LLVM output lacks '${pattern}'\n${llvm}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu
            "${ERROR_SOURCE}" -o "${OUTPUT}-bad.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0 OR NOT stderr MATCHES "hidden resolver call" OR
   NOT stderr MATCHES "tls_model requires thread_local")
    message(FATAL_ERROR "expected standalone TLS diagnostics\n${stdout}\n${stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S -target x86_64-w64-windows-gnu
            "${SOURCE}" -o "${OUTPUT}-coff.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "COFF static TLS assembly failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}-coff.s" coff)
foreach(pattern ".section \".tls$\",\"dw\"" "%gs:88" "@SECREL32"
                "_tls_index" "_tls_used" "discard,_tls_used")
    string(FIND "${coff}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "COFF static TLS assembly lacks '${pattern}'\n${coff}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -c -target x86_64-w64-windows-gnu
            "${SOURCE}" -o "${OUTPUT}-coff.o"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "COFF static TLS object failed\n${stdout}\n${stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S -target x86_64-apple-darwin
            "${SOURCE}" -o "${OUTPUT}-macho.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0 OR NOT stderr MATCHES "TLV resolver ABI")
    message(FATAL_ERROR "expected standalone Darwin TLV diagnostic\n${stdout}\n${stderr}")
endif()

execute_process(
    COMMAND "${CC}" -c -fpic -mcmodel=large
            -target x86_64-unknown-linux-gnu
            "${SOURCE}" -o "${OUTPUT}-large-pic.o"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR
        "large-PIC initial-exec TLS object failed\n${stdout}\n${stderr}")
endif()

execute_process(
    COMMAND "${CC}" -c -O2 -fpic -mcmodel=large
            -target x86_64-unknown-linux-gnu
            "${LARGE_SOURCE}" -o "${OUTPUT}-large-local.o"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR
        "large-PIC local-exec TLS object failed\n${stdout}\n${stderr}")
endif()

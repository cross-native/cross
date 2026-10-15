# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A function that never returns skips the saves of the preserved registers it
# overwrites, and its call-frame information marks their incoming values
# undefined; it stays exact for the registers it keeps. When the host has
# gdb, a native backtrace through spin reaches its caller and shows RBX lost.
foreach(required CC SOURCE RUNNER HOST_CXX OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_DWARFDUMP NAMES llvm-dwarfdump REQUIRED)
find_program(LLVM_NM NAMES llvm-nm REQUIRED)
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

function(run label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

# Sets `rows` to the call-frame rows of spin in a -g object.
function(spin_rows label)
    set(object "${OUTPUT}/${label}.o")
    run("cc ${label}" "${CC}" -c -g ${ARGN} "${SOURCE}" -o "${object}")
    run("nm ${label}" "${LLVM_NM}" "${object}")
    if(NOT stdout MATCHES "0*([0-9a-f]+) T spin\n")
        message(FATAL_ERROR "${label}: no spin\n${stdout}")
    endif()
    set(address "${CMAKE_MATCH_1}")
    run("frames ${label}" "${LLVM_DWARFDUMP}" --debug-frame "${object}")
    string(REGEX MATCH " FDE cie=[0-9a-f]+ pc=0*${address}[.][.][.]" header "${stdout}")
    string(FIND "${stdout}" "${header}" start)
    if(header STREQUAL "" OR start EQUAL -1)
        message(FATAL_ERROR "${label}: no FDE for spin\n${stdout}")
    endif()
    string(SUBSTRING "${stdout}" ${start} -1 frame)
    string(FIND "${frame}" "\n\n\n" end)
    string(SUBSTRING "${frame}" 0 ${end} frame)
    set(rows "${frame}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

function(reject label text)
    foreach(pattern IN LISTS ARGN)
        if(text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} has '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu)
spin_rows(plain -O2 ${x86})
expect("x86-64" "${rows}" "DW_CFA_undefined: RBX\n" "\n  0x[0-9a-f]+: CFA=RSP[+]16: RBX=undefined, RIP=[[]CFA-8[]]\n")
# Without a save of RBP, the CFA stays eight bytes above it.
spin_rows(frame -O2 -fno-omit-frame-pointer ${x86})
expect("x86-64 frame pointer" "${rows}" "RBP=undefined" "CFA=RBP[+]8: RBX=undefined, RBP=undefined")
spin_rows(large -O2 -fno-omit-frame-pointer -DLARGE ${x86})
expect("x86-64 large frame" "${rows}" "CFA=RBP[+]8: RBX=undefined, RBP=undefined")
reject("x86-64 large frame" "${rows}" "RBP=[[]CFA" "CFA=RBP[+]16")
spin_rows(ms_abi -O2 -DMS_ABI ${x86})
expect("ms_abi" "${rows}" "RSI=undefined, RDI=undefined" "XMM6=undefined"
    "XMM15=undefined")
# Saves are kept at -O0 and with unwind tables.
spin_rows(saved -O0 ${x86})
reject("x86-64 -O0" "${rows}" "undefined")
spin_rows(unwind -O2 -funwind-tables ${x86})
reject("x86-64 -funwind-tables" "${rows}" "undefined")
# MIPS also loses the incoming $ra at the first call.
spin_rows(mips -O2 -target mips-unknown-elf)
expect("MIPS" "${rows}" "DW_CFA_undefined: S0_64\n" "S0_64=undefined, RA_64=undefined")
spin_rows(mips_o32 -O2 -DO32 -target mips-unknown-elf)
expect("MIPS o32" "${rows}" "S0_64=undefined, GP_64=undefined, RA_64=undefined, D20_64=undefined"
    "D31_64=undefined")

find_program(GDB NAMES gdb)
if(NOT GDB OR NOT HOST_TARGET STREQUAL "x86_64-unknown-linux-gnu")
    message(STATUS "skipping the native gdb backtrace: gdb on an x86-64 Linux host is required")
    return()
endif()
run("cc native" "${CC}" -c -g -O2 -mabi=sysv_abi ${x86} "${SOURCE}" -o "${OUTPUT}/native.o")
run("link native" "${HOST_CXX}" -g "-DCROSS_ENTRY=spin_entry" "${RUNNER}"
    "${OUTPUT}/native.o" -o "${OUTPUT}/native")
execute_process(COMMAND "${GDB}" -batch -nx -ex "break step" -ex "run" -ex "bt"
        -ex "frame 2" -ex "print $rbx" -ex "kill" "${OUTPUT}/native"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
expect("gdb" "${stdout}\n${stderr}" "\n#1 +0x[0-9a-f]+ in spin " "\n#2 +0x[0-9a-f]+ in spin_entry "
    "\n[$]1 = <not saved>\n")

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

function(run_cc output)
    execute_process(
        COMMAND "${CC}" ${ARGN} "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cc failed (${status})\n${stdout}\n${stderr}")
    endif()
endfunction()

function(require_text file pattern)
    file(READ "${file}" text)
    string(FIND "${text}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${file} lacks '${pattern}'\n${text}")
    endif()
endfunction()

function(reject_text file pattern)
    file(READ "${file}" text)
    string(FIND "${text}" "${pattern}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${file} unexpectedly contains '${pattern}'\n${text}")
    endif()
endfunction()

set(target -target x86_64-unknown-linux-gnu -O2)
run_cc("${OUTPUT}-small.s" -S ${target})
run_cc("${OUTPUT}-pic.s" -S ${target} -fpic)
run_cc("${OUTPUT}-pic-no-plt.s" -S ${target} -fPIC -fno-plt)
run_cc("${OUTPUT}-pie.s" -S ${target} -fpie)
run_cc("${OUTPUT}-direct.s" -S ${target} -fpic
       -fdirect-access-external-data)
run_cc("${OUTPUT}-medium.s" -S ${target} -mcmodel=medium)
run_cc("${OUTPUT}-large.s" -S ${target} -mcmodel=large)
run_cc("${OUTPUT}-medium.o" -c ${target} -mcmodel=medium)
run_cc("${OUTPUT}-large.o" -c ${target} -mcmodel=large)
run_cc("${OUTPUT}-pic-medium.s" -S ${target} -fpic -mcmodel=medium)
run_cc("${OUTPUT}-pic-medium.o" -c ${target} -fpic -mcmodel=medium)
run_cc("${OUTPUT}-pic-large.s" -S ${target} -fPIC -mcmodel=large)
run_cc("${OUTPUT}-pic-large.o" -c ${target} -fPIC -mcmodel=large)
run_cc("${OUTPUT}-pic-large-no-plt.s" -S ${target} -fPIC -fno-plt
       -mcmodel=large)
run_cc("${OUTPUT}-pic-large-no-plt.o" -c ${target} -fPIC -fno-plt
       -mcmodel=large)

require_text("${OUTPUT}-small.s" "external_function@PLT")
require_text("${OUTPUT}-pic.s" "external_data@GOTPCREL(%rip)")
require_text("${OUTPUT}-pic.s" "external_function@PLT")
require_text("${OUTPUT}-pic-no-plt.s" "*external_function@GOTPCREL(%rip)")
require_text("${OUTPUT}-pie.s" "external_data@GOTPCREL(%rip)")
reject_text("${OUTPUT}-direct.s" "external_data@GOTPCREL")
require_text("${OUTPUT}-direct.s" "external_data(%rip)")
require_text("${OUTPUT}-medium.s" "movabsq\t$external_data")
require_text("${OUTPUT}-large.s" "movabsq\t$external_function")
require_text("${OUTPUT}-large.s" "call\t*")
require_text("${OUTPUT}-pic-medium.s" "_GLOBAL_OFFSET_TABLE_(%rip)")
require_text("${OUTPUT}-pic-medium.s" "@GOTOFF")
require_text("${OUTPUT}-pic-medium.s" "external_function@PLT")
require_text("${OUTPUT}-pic-large.s" "$_GLOBAL_OFFSET_TABLE_-")
require_text("${OUTPUT}-pic-large.s" "external_data@GOT")
require_text("${OUTPUT}-pic-large.s" "@GOTOFF")
require_text("${OUTPUT}-pic-large.s" "external_function@PLTOFF")
require_text("${OUTPUT}-pic-large.s" "call\t*")
require_text("${OUTPUT}-pic-large-no-plt.s" "external_function@GOT")
reject_text("${OUTPUT}-pic-large-no-plt.s" "external_function@PLTOFF")

run_cc("${OUTPUT}-coff-large.s" -S -O2
       -target x86_64-w64-windows-gnu -fpic -mcmodel=large)
run_cc("${OUTPUT}-coff-large.o" -c -O2
       -target x86_64-w64-windows-gnu -fpic -mcmodel=large)
require_text("${OUTPUT}-coff-large.s" "movabsq\t$external_data")
require_text("${OUTPUT}-coff-large.s" "movabsq\t$external_function")
require_text("${OUTPUT}-coff-large.s" "call\t*")

run_cc("${OUTPUT}-macho-large.s" -S -O2
       -target x86_64-apple-darwin -fpic -mcmodel=large)
run_cc("${OUTPUT}-macho-large.o" -c -O2
       -target x86_64-apple-darwin -fpic -mcmodel=large)
require_text("${OUTPUT}-macho-large.s" "movabsq\t$_external_data@GOT")
require_text("${OUTPUT}-macho-large.s" "movabsq\t$_external_function@GOT")
require_text("${OUTPUT}-macho-large.s" "call\t*")
reject_text("${OUTPUT}-macho-large.s" "@GOTOFF")
reject_text("${OUTPUT}-macho-large.s" "@PLTOFF")

execute_process(
    COMMAND "${CC}" -S ${target} -fpie -mcmodel=kernel
            "${SOURCE}" -o "${OUTPUT}-bad-kernel.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0 OR NOT stderr MATCHES "mcmodel=kernel")
    message(FATAL_ERROR "expected kernel/PIE diagnostic\n${stdout}\n${stderr}")
endif()

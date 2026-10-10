# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under a profile that traps floating exceptions, a floating operation that
# may raise one runs only where the source runs it, and -ffast-math creates no
# intermediate result that could trap.

foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S ${ARGN} "${SOURCE}"
                            -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed to compile\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}/${label}.s" assembly)
    string(REPLACE ";" "," assembly "${assembly}")
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()

# The lines of `function` up to its first jump or branch and, on MIPS, that
# branch's delay slot: what runs before any test of the function's inputs.
function(entry_block result function architecture)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "no function ${function} in\n${assembly}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(REPLACE "\n" ";" lines "${text}")
    if(architecture STREQUAL "mips")
        set(branch "^\t(b[a-z0-9]*|j[a-z]*)\t")
        set(delay 1)
    else()
        set(branch "^\tj[a-z]*\t")
        set(delay 0)
    endif()
    set(block "")
    set(remaining -1)
    foreach(line IN LISTS lines)
        if(remaining EQUAL 0)
            break()
        endif()
        string(APPEND block "${line}\n")
        if(remaining GREATER 0)
            math(EXPR remaining "${remaining} - 1")
        elseif(line MATCHES "${branch}")
            set(remaining ${delay})
        endif()
    endforeach()
    set(${result} "${block}" PARENT_SCOPE)
endfunction()

function(function_text result function)
    string(FIND "${assembly}" "\n${function}:\n" start)
    string(FIND "${assembly}" "\n.size ${function}," end)
    if(start EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "no function ${function} in\n${assembly}")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} text)
    set(${result} "${text}" PARENT_SCOPE)
endfunction()

function(expect_entry label function architecture pattern present)
    entry_block(block ${function} ${architecture})
    if(present AND NOT block MATCHES "${pattern}")
        message(FATAL_ERROR
            "${label}: ${function} does not run '${pattern}' before its first branch\n${block}")
    elseif(NOT present AND block MATCHES "${pattern}")
        message(FATAL_ERROR
            "${label}: ${function} runs '${pattern}' before its first branch\n${block}")
    endif()
endfunction()

set(x86_64_flags -target x86_64-unknown-linux-gnu)
set(x86_64_divide "\tdivss\t")
set(x86_64_compare "\tu?comiss\t")
set(mips_flags -target mips-unknown-elf -march=vr4300 -mabi=o32)
set(mips_divide "\tdiv[.]s\t")
set(mips_compare "\tc[.][a-z]+[.]s\t")
set(trapping "--model=${MODEL}" -mprofile=trapping-fp)
foreach(architecture x86_64 mips)
    foreach(level O2 O3)
        set(label trapping-${architecture}-${level})
        compile(${label} -${level} ${${architecture}_flags} ${trapping})
        function_text(text guarded_div)
        if(NOT text MATCHES "${${architecture}_divide}")
            message(FATAL_ERROR "${label}: guarded_div has no division\n${text}")
        endif()
        expect_entry(${label} guarded_div ${architecture}
                     "${${architecture}_divide}" FALSE)
        expect_entry(${label} guarded_loop ${architecture}
                     "${${architecture}_divide}" FALSE)
        expect_entry(${label} count_zero ${architecture}
                     "${${architecture}_compare}" FALSE)
        # The masked default environment lets the invariant test move ahead
        # of the loop.
        set(label default-${architecture}-${level})
        compile(${label} -${level} ${${architecture}_flags})
        expect_entry(${label} count_zero ${architecture}
                     "${${architecture}_compare}" TRUE)
    endforeach()
endforeach()

# -ffast-math reassociates a sum into packed partial sums only when no
# exception but divide-by-zero traps, and fuses a multiply and an add only
# when overflow and underflow are masked.
foreach(profile default trapping-fp divide-trap-fp)
    set(flags -O3 -ffast-math ${x86_64_flags})
    if(NOT profile STREQUAL "default")
        list(APPEND flags "--model=${MODEL}" -mprofile=${profile})
    endif()
    compile(fast-${profile} ${flags})
    function_text(text sum)
    if(profile STREQUAL "trapping-fp")
        if(text MATCHES "\tv?addps\t")
            message(FATAL_ERROR "fast-${profile}: sum is reassociated\n${text}")
        endif()
    elseif(NOT text MATCHES "\tv?addps\t")
        message(FATAL_ERROR "fast-${profile}: sum is not reassociated\n${text}")
    endif()
endforeach()
foreach(profile trapping-fp range-trap-fp)
    compile(contract-${profile} -O2 -ffast-math -mfma ${x86_64_flags}
            "--model=${MODEL}" -mprofile=${profile})
    function_text(text fused)
    if(profile STREQUAL "range-trap-fp" AND text MATCHES "\tvfmadd")
        message(FATAL_ERROR "contract-${profile}: fused is contracted\n${text}")
    elseif(profile STREQUAL "trapping-fp" AND NOT text MATCHES "\tvfmadd")
        message(FATAL_ERROR "contract-${profile}: fused is not contracted\n${text}")
    endif()
endforeach()

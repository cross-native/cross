# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly flags suffix variable)
    separate_arguments(arguments NATIVE_COMMAND "${flags}")
    execute_process(
        COMMAND "${CC}" -S -O3 -fno-eval-calls ${arguments}
                -target x86_64-unknown-linux-gnu "${SOURCE}"
                -o "${OUTPUT}.${suffix}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${suffix} loop-unrolling compilation failed\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    file(READ "${OUTPUT}.${suffix}.s" assembly)
    set(${variable} "${assembly}" PARENT_SCOPE)
endfunction()

function(extract_function assembly symbol variable)
    string(FIND "${assembly}" "${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "could not find assembly body for ${symbol}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 tail)
    string(FIND "${tail}" ".size ${symbol}," length)
    if(length EQUAL -1)
        string(FIND "${tail}" ".seh_endproc" length)
    endif()
    if(length EQUAL -1)
        message(FATAL_ERROR "could not find assembly end for ${symbol}")
    endif()
    string(SUBSTRING "${tail}" 0 ${length} body)
    set(${variable} "${body}" PARENT_SCOPE)
endfunction()

compile_assembly("-fno-unroll-loops" scalar scalar_assembly)
compile_assembly("-funroll-loops" unrolled unrolled_assembly)
compile_assembly("-funroll-loops -fno-cprop-registers" no-cprop no_cprop_assembly)
compile_assembly("-funroll-loops -fno-peephole2" no-peephole no_peephole_assembly)
extract_function("${scalar_assembly}" unroll_power scalar_power)
extract_function("${unrolled_assembly}" unroll_power unrolled_power)
extract_function("${unrolled_assembly}" unroll_rotate unrolled_rotate)
extract_function("${unrolled_assembly}" unroll_sequence unrolled_sequence)
extract_function("${unrolled_assembly}" unroll_read unrolled_read)
extract_function("${unrolled_assembly}" unroll_add_read unrolled_add_read)
extract_function("${unrolled_assembly}" unroll_masked_read
                 unrolled_masked_read)
extract_function("${unrolled_assembly}" unroll_store unrolled_store)
if(scalar_power MATCHES "addq[\t ]+[$]4" OR
   NOT unrolled_power MATCHES "addq[\t ]+[$]4" OR
   NOT unrolled_power MATCHES "andq[\t ]+[$]-4" OR
   NOT unrolled_sequence MATCHES "andq[\t ]+[$]-4")
    message(FATAL_ERROR
        "four-way unrolling or rounded scalar-tail guard is missing\n"
        "${unrolled_power}\n${unrolled_sequence}")
endif()
if(NOT unrolled_rotate MATCHES "addq[\t ]+[$]4" OR
   NOT unrolled_rotate MATCHES "andl[\t ]+[$]28" OR
   NOT unrolled_rotate MATCHES "[\t ]rolq[\t ]")
    message(FATAL_ERROR
        "masked variable rotate did not use a shared four-way induction "
        "group\n${unrolled_rotate}")
endif()
string(REGEX MATCHALL "movq[\t ]+[^,\n]+,[\t ]*[^\n]*\\(%r9"
       unrolled_store_writes "${unrolled_store}")
list(LENGTH unrolled_store_writes unrolled_store_write_count)
if(NOT unrolled_store MATCHES "andq[\t ]+[$]-4" OR
   unrolled_store_write_count LESS 5)
    message(FATAL_ERROR
        "ordered pointer-store loop was not unrolled four ways\n"
        "${unrolled_store}")
endif()
if(NOT unrolled_read MATCHES "andq[\t ]+[$]-2" OR
   NOT unrolled_read MATCHES "\\([^\n]*,8\\)")
    message(FATAL_ERROR
        "two-way memory-reading unroll or indexed memory fold is missing\n"
        "${unrolled_read}")
endif()
# The two cloned unsigned-add iterations must form one independent group and
# update the loop-carried accumulator once.  The second update below belongs
# to the scalar cleanup loop.  A left-deep recurrence has four updates in the
# unrolled body and loses the memory-level parallelism exposed by cloning.
string(REGEX MATCHALL "addq[\t ]+[^,\n]+,[\t ]*%rdx"
       add_recurrence_updates "${unrolled_add_read}")
list(LENGTH add_recurrence_updates add_recurrence_update_count)
if(NOT unrolled_add_read MATCHES "andq[\t ]+[$]-2" OR
   NOT unrolled_add_read MATCHES "leaq[\t ]+\\(%r8,%r8\\)" OR
   NOT add_recurrence_update_count EQUAL 2)
    message(FATAL_ERROR
        "unsigned add recurrence was not grouped before its backedge update\n"
        "${unrolled_add_read}")
endif()
if(NOT unrolled_masked_read MATCHES "andq[\t ]+[$]-4" OR
   NOT unrolled_masked_read MATCHES "[\t ]cmov[a-z]+[\t ]" OR
   NOT unrolled_masked_read MATCHES "[\t ]testb[\t ]+[$]1" OR
   NOT unrolled_masked_read MATCHES "movq[\t ]+8\\(" OR
   NOT unrolled_masked_read MATCHES "movq[\t ]+16\\(" OR
   NOT unrolled_masked_read MATCHES "movq[\t ]+24\\(" OR
   NOT unrolled_masked_read MATCHES "addq[\t ]+[$]4")
    message(FATAL_ERROR
        "masked-memory unrolling did not fold offsets, narrow its test, "
        "or coalesce the backedge induction\n${unrolled_masked_read}")
endif()
if(NOT unrolled_power MATCHES "leaq?[\t ]+" OR
   unrolled_power MATCHES "imulq[\t ]+[$]3" OR
   NOT no_peephole_assembly MATCHES "imulq[\t ]+[$]3")
    message(FATAL_ERROR
        "-fpeephole2 did not strength-reduce multiplication by three\n"
        "${unrolled_power}")
endif()
string(REGEX MATCHALL "[\t ]mov[qwlb][\t ]" cprop_moves
       "${unrolled_assembly}")
string(REGEX MATCHALL "[\t ]mov[qwlb][\t ]" no_cprop_moves
       "${no_cprop_assembly}")
list(LENGTH cprop_moves cprop_move_count)
list(LENGTH no_cprop_moves no_cprop_move_count)
if(NOT cprop_move_count LESS no_cprop_move_count)
    message(FATAL_ERROR
        "-fcprop-registers did not remove machine copies "
        "(${cprop_move_count} versus ${no_cprop_move_count})")
endif()

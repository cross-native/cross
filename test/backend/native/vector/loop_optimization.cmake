# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 -funwind-tables -march=x86-64-v3
            "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR
        "optimized vector-loop compilation failed (${result})\n"
        "${stdout}\n${stderr}")
endif()

file(READ "${OUTPUT}" assembly)
function(extract_function source_assembly symbol variable)
    string(FIND "${source_assembly}" "${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "could not find assembly body for ${symbol}")
    endif()
    string(SUBSTRING "${source_assembly}" ${start} -1 tail)
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
foreach(opcode vpaddq vmulpd vzeroupper)
    string(FIND "${assembly}" "	${opcode}" opcode_position)
    if(opcode_position EQUAL -1)
        message(FATAL_ERROR
            "optimized vector-loop assembly does not contain ${opcode}")
    endif()
endforeach()

# The integer reduction is four-way interleaved and its single-use loads are
# legal packed-add memory operands. Keep this shape as a regression guard: it
# both exposes memory-level parallelism and avoids transient vector registers.
string(REGEX MATCHALL
    "vpaddq[\t ]+([0-9]+)?\\([^\n]+\\),[\t ]+%ymm[0-9]+,[\t ]+%ymm[0-9]+"
    reduction_memory_adds "${assembly}")
list(LENGTH reduction_memory_adds reduction_memory_add_count)
if(NOT reduction_memory_add_count EQUAL 8)
    message(FATAL_ERROR
        "the two optimized reductions do not each contain four memory-source vpaddq instructions\n"
        "${assembly}")
endif()
foreach(displacement 32 64 96)
    if(NOT assembly MATCHES
       "vpaddq[\t ]+${displacement}\\([^\n]+\\),[\t ]+%ymm")
        message(FATAL_ERROR
            "optimized reduction lost its ${displacement}-byte interleave displacement")
    endif()
endforeach()

# A vectorized inner loop inside an outer loop must compute its rounded tail
# limit once in the function preheader. Recomputing it once per outer trip is
# both slower and a needless long-lived register-pressure source.
string(REGEX MATCHALL "andq[\t ]+[$]-16" rounded_limits "${assembly}")
list(LENGTH rounded_limits rounded_limit_count)
if(NOT rounded_limit_count EQUAL 5)
    message(FATAL_ERROR
        "vectorized nested-loop limit was not materialized exactly once\n"
        "${assembly}")
endif()

# Index-derived affine terms are carried as packed recurrences. Re-expanding
# index*constant with AVX2's qword-multiply sequence in every unrolled group is
# both slower and much larger. The byte-swap idiom should independently select
# one packed shuffle per interleaved load group.
extract_function("${assembly}" affine_xor_reduction o2_affine_xor)
if(o2_affine_xor MATCHES "vpmuludq")
    message(FATAL_ERROR
        "affine vector induction retained an AVX2 qword multiply\n"
        "${o2_affine_xor}")
endif()
string(REGEX MATCHALL
    "vpshufb[\t ]+[.]Lcross[.]machine[.][0-9_]+[.]swap16"
    packed_byte_swaps "${assembly}")
list(LENGTH packed_byte_swaps packed_byte_swap_count)
if(NOT packed_byte_swap_count EQUAL 4)
    message(FATAL_ERROR
        "16-bit byte-swap reduction did not select four packed shuffles\n"
        "${assembly}")
endif()

# At O3, one allocated shuffle mask feeds every interleaved byte swap.  The
# constant qword-multiply expansion likewise caches the invariant upper half
# of its multiplier: only the four data vectors should need a right shift.
execute_process(
    COMMAND "${CC}" -S -O3 -funwind-tables -march=x86-64-v3
            "${SOURCE}" -o "${OUTPUT}.o3.s"
    RESULT_VARIABLE o3_status
    OUTPUT_VARIABLE o3_stdout
    ERROR_VARIABLE o3_stderr
)
if(NOT o3_status EQUAL 0)
    message(FATAL_ERROR
        "O3 vector-loop compilation failed (${o3_status})\n"
        "${o3_stdout}\n${o3_stderr}")
endif()
file(READ "${OUTPUT}.o3.s" o3_assembly)
extract_function("${o3_assembly}" byte_swap_reduction o3_byte_swap)
extract_function("${o3_assembly}" constant_product_reduction
                 o3_constant_product)
string(REGEX MATCH
    "vmovdqu[\t ]+[.]Lcross[.]machine[.][0-9_]+[.]swap16\\(%rip\\),[\t ]+%ymm([0-9]+)"
    o3_mask_load "${o3_byte_swap}")
if(NOT o3_mask_load)
    message(FATAL_ERROR
        "O3 byte-swap loop did not allocate one invariant shuffle mask\n"
        "${o3_byte_swap}")
endif()
set(o3_mask_register "${CMAKE_MATCH_1}")
string(REGEX MATCHALL
    "vpshufb[\t ]+%ymm${o3_mask_register},[\t ]+%ymm"
    o3_register_byte_swaps "${o3_byte_swap}")
list(LENGTH o3_register_byte_swaps o3_register_byte_swap_count)
if(NOT o3_register_byte_swap_count EQUAL 4 OR
   o3_byte_swap MATCHES
       "vpshufb[\t ]+[.]Lcross[.]machine[.][0-9_]+[.]swap16")
    message(FATAL_ERROR
        "O3 byte-swap loop did not reuse its allocated shuffle mask\n"
        "${o3_byte_swap}")
endif()
string(REGEX MATCHALL "vpbroadcastq" qword_constant_splats
       "${o3_constant_product}")
string(REGEX MATCHALL "vpsrlq[\t ]+[$]32" qword_high_shifts
       "${o3_constant_product}")
list(LENGTH qword_constant_splats qword_constant_splat_count)
list(LENGTH qword_high_shifts qword_high_shift_count)
if(NOT qword_constant_splat_count EQUAL 2 OR
   NOT qword_high_shift_count EQUAL 4)
    message(FATAL_ERROR
        "O3 AVX2 qword multiply did not cache its invariant high half\n"
        "${o3_constant_product}")
endif()

# Horizontal reduction must stay packed after the four vector accumulators
# are combined. Extracting every lane into a GPR is substantially larger and
# slower, while strict floating reduction still needs one ordered add per lane.
foreach(opcode vextracti128 vpshufd)
    string(FIND "${assembly}" "\t${opcode}" opcode_position)
    if(opcode_position EQUAL -1)
        message(FATAL_ERROR
            "optimized integer reduction does not contain ${opcode}")
    endif()
endforeach()
string(FIND "${assembly}" "\tpaddq" legacy_packed_add)
if(NOT legacy_packed_add EQUAL -1)
    message(FATAL_ERROR
        "optimized AVX reduction contains a legacy SSE paddq transition\n"
        "${assembly}")
endif()
string(FIND "${assembly}" "\tpxor" legacy_packed_xor)
if(NOT legacy_packed_xor EQUAL -1)
    message(FATAL_ERROR
        "optimized AVX reduction contains a legacy SSE pxor transition\n"
        "${assembly}")
endif()
string(FIND "${assembly}" "\tvmovq" vex_scalar_extract)
if(vex_scalar_extract EQUAL -1)
    message(FATAL_ERROR
        "optimized AVX reduction does not use a VEX scalar extract\n"
        "${assembly}")
endif()
string(REGEX MATCHALL "vextractf128" floating_high_extracts "${assembly}")
list(LENGTH floating_high_extracts floating_high_extract_count)
if(NOT floating_high_extract_count EQUAL 1)
    message(FATAL_ERROR
        "strict floating reduction should extract its high half exactly once\n"
        "${assembly}")
endif()
if(assembly MATCHES "vpermilpd[	 ]+\\$0,")
    message(FATAL_ERROR
        "strict floating reduction retained an identity lane permutation")
endif()

# AVX1 has VEX-encoded 128-bit packed integer operations even though its
# 256-bit integer forms require AVX2. Keep the reduction tail VEX-only while
# permitting the 128-bit loop body to use SSE2, and prove the emitted feature
# combination is accepted by the target assembler.
execute_process(
    COMMAND "${CC}" -S -O2 -funwind-tables -mavx -mno-avx2
            -mprefer-vector-width=128 "${SOURCE}"
            -o "${OUTPUT}.avx-only.s"
    RESULT_VARIABLE avx_only_status
    OUTPUT_VARIABLE avx_only_stdout
    ERROR_VARIABLE avx_only_stderr
)
if(NOT avx_only_status EQUAL 0)
    message(FATAL_ERROR
        "AVX-only vector-loop compilation failed (${avx_only_status})\n"
        "${avx_only_stdout}\n${avx_only_stderr}")
endif()
file(READ "${OUTPUT}.avx-only.s" avx_only_assembly)
foreach(opcode vpshufd vpaddq vmovq)
    string(FIND "${avx_only_assembly}" "\t${opcode}" opcode_position)
    if(opcode_position EQUAL -1)
        message(FATAL_ERROR
            "AVX-only integer reduction tail does not contain ${opcode}\n"
            "${avx_only_assembly}")
    endif()
endforeach()
if(avx_only_assembly MATCHES "vpaddq[\t ]+[^\n]*%ymm")
    message(FATAL_ERROR
        "AVX-only vector loop selected an AVX2 YMM integer add\n"
        "${avx_only_assembly}")
endif()
execute_process(
    COMMAND "${CC}" -c -O2 -funwind-tables -mavx -mno-avx2
            -mprefer-vector-width=128 "${SOURCE}"
            -o "${OUTPUT}.avx-only.o"
    RESULT_VARIABLE avx_only_object_status
    OUTPUT_VARIABLE avx_only_object_stdout
    ERROR_VARIABLE avx_only_object_stderr
)
if(NOT avx_only_object_status EQUAL 0)
    message(FATAL_ERROR
        "AVX-only vector-loop object emission failed "
        "(${avx_only_object_status})\n"
        "${avx_only_object_stdout}\n${avx_only_object_stderr}")
endif()

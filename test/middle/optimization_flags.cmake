# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O0 "${SOURCE}" -o "${OUTPUT}.strict.s"
    RESULT_VARIABLE strict_status
    ERROR_VARIABLE strict_error
)
if(NOT strict_status EQUAL 0)
    message(FATAL_ERROR "strict floating compilation failed\n${strict_error}")
endif()
file(READ "${OUTPUT}.strict.s" strict_assembly)
if(NOT strict_assembly MATCHES "addsd" OR
   NOT strict_assembly MATCHES "mulsd")
    message(FATAL_ERROR
        "strict floating operations disappeared\n${strict_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -ftree-ccp -ftree-copy-prop -ftree-dce
            "${SOURCE}" -o "${OUTPUT}.ccp.s"
    RESULT_VARIABLE ccp_status
    ERROR_VARIABLE ccp_error
)
if(NOT ccp_status EQUAL 0)
    message(FATAL_ERROR "CCP compilation failed\n${ccp_error}")
endif()
file(READ "${OUTPUT}.ccp.s" ccp_assembly)
string(REGEX MATCHALL "[\t ]j(e|ne)[\t ]" strict_branches
       "${strict_assembly}")
string(REGEX MATCHALL "[\t ]j(e|ne)[\t ]" ccp_branches
       "${ccp_assembly}")
list(LENGTH strict_branches strict_branch_count)
list(LENGTH ccp_branches ccp_branch_count)
math(EXPR expected_strict_branches "${ccp_branch_count} + 1")
if(NOT strict_branch_count EQUAL expected_strict_branches)
    message(FATAL_ERROR
        "CCP did not fold the constant CFG edge\n${ccp_assembly}")
endif()
string(REGEX MATCHALL "[\t ]xorpd[\t ]" strict_xorpd
       "${strict_assembly}")
string(REGEX MATCHALL "[\t ]xorpd[\t ]" ccp_xorpd
       "${ccp_assembly}")
list(LENGTH strict_xorpd strict_xorpd_count)
list(LENGTH ccp_xorpd ccp_xorpd_count)
math(EXPR expected_strict_xorpd "${ccp_xorpd_count} + 1")
if(NOT strict_xorpd_count EQUAL expected_strict_xorpd)
    message(FATAL_ERROR
        "CCP did not fold exact floating negation into the literal sign bit\n"
        "${ccp_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -ftree-dse -ftree-dce "${SOURCE}"
            -o "${OUTPUT}.dse.s"
    RESULT_VARIABLE dse_status
    ERROR_VARIABLE dse_error
)
if(NOT dse_status EQUAL 0)
    message(FATAL_ERROR "DSE compilation failed\n${dse_error}")
endif()
file(READ "${OUTPUT}.dse.s" dse_assembly)
if(NOT strict_assembly MATCHES "[$]41" OR
   dse_assembly MATCHES "[$]41" OR
   NOT dse_assembly MATCHES "[$]42" OR
   NOT dse_assembly MATCHES "[$]43" OR
   NOT dse_assembly MATCHES "[$]44" OR
   NOT strict_assembly MATCHES "[$]101" OR
   NOT strict_assembly MATCHES "[$]102" OR
   NOT strict_assembly MATCHES "[$]103" OR
   dse_assembly MATCHES "[$]101" OR
   dse_assembly MATCHES "[$]102" OR
   dse_assembly MATCHES "[$]103" OR
   NOT dse_assembly MATCHES "[$]104")
    message(FATAL_ERROR
        "local-slot DSE did not remove only overwritten stores\n"
        "${dse_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -fipa-pure-const -ftree-dce "${SOURCE}"
            -o "${OUTPUT}.pure.s"
    RESULT_VARIABLE pure_status
    ERROR_VARIABLE pure_error
)
if(NOT pure_status EQUAL 0)
    message(FATAL_ERROR "IPA purity compilation failed\n${pure_error}")
endif()
file(READ "${OUTPUT}.pure.s" pure_assembly)
if(NOT strict_assembly MATCHES
       "call[\t ]+optimization_pure_leaf" OR
   pure_assembly MATCHES "call[\t ]+optimization_pure_leaf" OR
   NOT pure_assembly MATCHES
       "call[\t ]+optimization_cell_leaf")
    message(FATAL_ERROR
        "IPA purity removed the wrong call boundary\n${pure_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -ftree-fre -ftree-dce "${SOURCE}"
            -o "${OUTPUT}.fre.s"
    RESULT_VARIABLE fre_status
    ERROR_VARIABLE fre_error
)
if(NOT fre_status EQUAL 0)
    message(FATAL_ERROR "FRE compilation failed\n${fre_error}")
endif()
file(READ "${OUTPUT}.fre.s" fre_assembly)
string(REGEX MATCHALL "[	 ]addl[	 ]" strict_additions
       "${strict_assembly}")
string(REGEX MATCHALL "[	 ]addl[	 ]" fre_additions "${fre_assembly}")
list(LENGTH strict_additions strict_addition_count)
list(LENGTH fre_additions fre_addition_count)
math(EXPR expected_strict_additions "${fre_addition_count} + 1")
if(NOT strict_addition_count EQUAL expected_strict_additions)
    message(FATAL_ERROR
        "-ftree-fre did not remove the redundant integer expression\n"
        "strict=${strict_addition_count} fre=${fre_addition_count}\n"
        "${fre_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -ffast-math "${SOURCE}"
            -o "${OUTPUT}.fast.s"
    RESULT_VARIABLE fast_status
    ERROR_VARIABLE fast_error
)
if(NOT fast_status EQUAL 0)
    message(FATAL_ERROR "fast-math compilation failed\n${fast_error}")
endif()
file(READ "${OUTPUT}.fast.s" fast_assembly)
if(fast_assembly MATCHES "addsd" OR fast_assembly MATCHES "mulsd")
    message(FATAL_ERROR
        "fast-math identities reached native assembly\n${fast_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -fno-signed-zeros "${SOURCE}"
            -o "${OUTPUT}.no-signed-zero.s"
    RESULT_VARIABLE signed_zero_status
    ERROR_VARIABLE signed_zero_error
)
if(NOT signed_zero_status EQUAL 0)
    message(FATAL_ERROR
        "signed-zero compilation failed\n${signed_zero_error}")
endif()
file(READ "${OUTPUT}.no-signed-zero.s" signed_zero_assembly)
if(signed_zero_assembly MATCHES "addsd" OR
   NOT signed_zero_assembly MATCHES "mulsd")
    message(FATAL_ERROR
        "-fno-signed-zeros changed the wrong floating identities\n"
        "${signed_zero_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -ffinite-math-only "${SOURCE}"
            -o "${OUTPUT}.finite.s"
    RESULT_VARIABLE finite_status
    ERROR_VARIABLE finite_error
)
if(NOT finite_status EQUAL 0)
    message(FATAL_ERROR
        "finite-math compilation failed\n${finite_error}")
endif()
file(READ "${OUTPUT}.finite.s" finite_assembly)
string(REGEX MATCHALL "[	 ]ucomisd[	 ]" strict_float_compares
       "${strict_assembly}")
string(REGEX MATCHALL "[	 ]ucomisd[	 ]" finite_float_compares
       "${finite_assembly}")
list(LENGTH strict_float_compares strict_float_compare_count)
list(LENGTH finite_float_compares finite_float_compare_count)
math(EXPR expected_strict_float_compares
     "${finite_float_compare_count} + 1")
if(NOT strict_float_compare_count EQUAL
       expected_strict_float_compares OR
   NOT finite_assembly MATCHES "addsd" OR
   NOT finite_assembly MATCHES "mulsd")
    message(FATAL_ERROR
        "-ffinite-math-only did not fold only the finite self-comparison\n"
        "${finite_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -mtune=znver3 "${SOURCE}"
            -o "${OUTPUT}.tuned.s"
    RESULT_VARIABLE tuned_status
    ERROR_VARIABLE tuned_error
)
if(NOT tuned_status EQUAL 0)
    message(FATAL_ERROR "tuned compilation failed\n${tuned_error}")
endif()
file(READ "${OUTPUT}.tuned.s" tuned_assembly)
if(NOT tuned_assembly MATCHES "[.]p2align 5")
    message(FATAL_ERROR
        "-mtune did not affect native layout policy\n${tuned_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -O0 -falign-functions=64 "${SOURCE}"
            -o "${OUTPUT}.aligned.s"
    RESULT_VARIABLE aligned_status
    ERROR_VARIABLE aligned_error
)
if(NOT aligned_status EQUAL 0)
    message(FATAL_ERROR "explicit alignment compilation failed\n${aligned_error}")
endif()
file(READ "${OUTPUT}.aligned.s" aligned_assembly)
if(NOT aligned_assembly MATCHES "[.]p2align 6")
    message(FATAL_ERROR
        "-falign-functions did not override target policy\n${aligned_assembly}")
endif()

execute_process(
    COMMAND "${CC}" -S -Oz "${SOURCE}" -o "${OUTPUT}.oz.s"
    RESULT_VARIABLE oz_status
    ERROR_VARIABLE oz_error
)
if(NOT oz_status EQUAL 0)
    message(FATAL_ERROR "Oz compilation failed\n${oz_error}")
endif()
file(READ "${OUTPUT}.oz.s" oz_assembly)
if(NOT oz_assembly MATCHES "[.]p2align 0")
    message(FATAL_ERROR
        "minimum-size function alignment was not selected\n${oz_assembly}")
endif()

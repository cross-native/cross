# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu)
    string(REPLACE "-" "_" suffix "${target}")
    execute_process(
        COMMAND "${CC}" -S -march=haswell -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${suffix}.s"
        RESULT_VARIABLE assembly_status
        OUTPUT_VARIABLE assembly_stdout
        ERROR_VARIABLE assembly_stderr
    )
    execute_process(
        COMMAND "${CC}" -c -march=haswell -target "${target}" "${SOURCE}"
                -o "${OUTPUT}-${suffix}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr
    )
    if(NOT assembly_status EQUAL 0 OR NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "baseline raw forms failed for ${target}\n${assembly_stdout}\n"
            "${assembly_stderr}\n${object_stdout}\n${object_stderr}")
    endif()
    file(READ "${OUTPUT}-${suffix}.s" assembly)
    foreach(opcode
            movb movw movl movq addb addw addl imulw imull
            shlb shrb sarb rolb rorb testb adcb sbbb incb decb negb notb
            xchgb xaddb btw btsw btrw btcw bswapl bswapq
            sete setne cmovew cmovnew cmovgl cmovlel cmovaq cmovbq
            cpuid rdtsc clc cmc stc syscall nop ud2 int3 retq)
        if(NOT assembly MATCHES "[\t ]${opcode}([\t \n]|$)")
            message(FATAL_ERROR
                "raw baseline output for ${target} is missing ${opcode}\n${assembly}")
        endif()
    endforeach()
    foreach(pattern
            "cmpxchgq[\t ]+%rcx, %r10"
            "lock cmpxchgq[\t ]+%rcx, [(]%r10[)]"
            "xchgq[\t ]+%rcx, [(]%r10[)]"
            "lock xaddq[\t ]+%rcx, [(]%r10[)]"
            "movbeq[\t ]+[(]%r9[)], %rax"
            "movbeq[\t ]+%rax, [(]%r10[)]")
        if(NOT assembly MATCHES "${pattern}")
            message(FATAL_ERROR
                "raw atomic/movbe output for ${target} is missing ${pattern}\n${assembly}")
        endif()
    endforeach()
    foreach(pattern
            "movzbq[\t ]+%cl, %rax"
            "movsbq[\t ]+%cl, %rax"
            "movzwq[\t ]+%r8w, %rax"
            "movswq[\t ]+%r8w, %rax"
            "movslq[\t ]+%edx, %rax"
            "movzbq[\t ]+3[(]%r9[)], %rax"
            "movsbq[\t ]+5[(]%r9[)], %rax"
            "leaq[\t ]+7[(]%r9[)], %r11"
            "bsfq[\t ]+%r11, %rax"
            "bsrq[\t ]+[(]%r10[)], %rax"
            "mulb[\t ]+%cl"
            "mulw[\t ]+%r8w"
            "mull[\t ]+%r9d"
            "mulq[\t ]+%r10"
            "imulq[\t ]+%r10"
            "cwd([\t \n]|$)"
            "cdq([\t \n]|$)"
            "cqo([\t \n]|$)"
            "divq[\t ]+%r10"
            "idivq[\t ]+%r10")
        if(NOT assembly MATCHES "${pattern}")
            message(FATAL_ERROR
                "raw extension output for ${target} is missing ${pattern}\n${assembly}")
        endif()
    endforeach()
    foreach(pattern
            "movq[\t ]+[(]%r9[)], %rax"
            "movq[\t ]+8[(]%r9[)], %rax"
            "movq[\t ]+%rax, [(]%r10[)]"
            "movq[\t ]+%rax, 16[(]%r10[)]"
            "movq[\t ]+[(]%r9,%r8,8[)], %rax"
            "movq[\t ]+%rax, [(]%r10,%r8,8[)]"
            "addq[\t ]+[(]%r9[)], %rax"
            "subq[\t ]+%rax, [(]%r10[)]"
            "cmoveq[\t ]+[(]%r9[)], %rax"
            "xaddq[\t ]+%rcx, 16[(]%r10[)]"
            "btsq[\t ]+%rax, [(]%r10[)]")
        if(NOT assembly MATCHES "${pattern}")
            message(FATAL_ERROR
                "raw typed-memory output for ${target} is missing ${pattern}\n${assembly}")
        endif()
    endforeach()
endforeach()

execute_process(
    COMMAND "${CC}" -S -march=haswell -mno-movbe "${SOURCE}"
            -o "${OUTPUT}-no-movbe.s"
    RESULT_VARIABLE movbe_status
    OUTPUT_VARIABLE movbe_stdout
    ERROR_VARIABLE movbe_stderr
)
if(movbe_status EQUAL 0 OR
   NOT movbe_stderr MATCHES
       "target instruction '[$]::_movbe' requires feature 'movbe'")
    message(FATAL_ERROR
        "movbe feature gate was not enforced\n${movbe_stdout}\n${movbe_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -S "${ERROR_SOURCE}" -o "${OUTPUT}-errors.s"
    RESULT_VARIABLE error_status
    OUTPUT_VARIABLE error_stdout
    ERROR_VARIABLE error_stderr
)
if(error_status EQUAL 0 OR
   NOT error_stderr MATCHES
       "no typed form of target instruction '[$]::_mov' matches these operands" OR
   NOT error_stderr MATCHES
       "no typed form of target instruction '[$]::_add' matches these operands")
    message(FATAL_ERROR
        "typed raw overload diagnostics were not enforced\n"
        "${error_stdout}\n${error_stderr}")
endif()
foreach(pattern
        "ordinary instruction form cannot access an atomic-qualified lvalue"
        "instruction cannot write a const-qualified lvalue")
    if(NOT error_stderr MATCHES "${pattern}")
        message(FATAL_ERROR
            "typed raw memory diagnostic is missing ${pattern}\n${error_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_stderr
)
if(NOT registry_status EQUAL 0)
    message(FATAL_ERROR "--print-instructions failed\n${registry_stderr}")
endif()
string(REPLACE "\n" ";" registry_lines "${registry}")
set(unique_registry_lines "${registry_lines}")
list(REMOVE_DUPLICATES unique_registry_lines)
list(LENGTH registry_lines registry_line_count)
list(LENGTH unique_registry_lines unique_registry_line_count)
if(NOT registry_line_count EQUAL unique_registry_line_count)
    message(FATAL_ERROR "--print-instructions emitted duplicate overload rows")
endif()
foreach(entry _mov _test _adc _sbb _sar _rol _ror _bswap _xchg _xadd
              _cmpxchg _lock_cmpxchg _lock_xadd _movbe
              _movzx _movsx _lea _bsf _bsr _mul _imul_full _div _idiv
              _cwd _cdq _cqo
              _bt _bts _btr _btc _cmove _sete _cpuid _rdtsc _syscall _ud2)
    string(FIND "${registry}" "$::${entry} instruction" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "instruction registry is missing $::${entry}")
    endif()
endforeach()

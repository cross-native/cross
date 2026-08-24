# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
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
            "packed raw ISA forms failed for ${target}\n${assembly_stdout}\n"
            "${assembly_stderr}\n${object_stdout}\n${object_stderr}")
    endif()
    file(READ "${OUTPUT}-${suffix}.s" assembly)
    foreach(opcode movdqa movdqu addps sqrtpd paddb psubq pand pxor pmullw
                   pslld pshufd haddps addsubpd pshufb phaddd pabsb pblendw
                   pmulld ptest roundps pcmpgtq vaddps vsqrtpd vaddpd vxorps
                   vpaddb vpmulld vpshufb vpermq vpermd vpsllvd vzeroupper
                   paddusb psubsw pavgb pmaxub pmaddwd packsswb punpcklqdq
                   pslldq pmovmskb phaddsw psignb palignr packusdw pmuldq
                   phminposuw dpps vpaddusb vpsubsw vpacksswb vpunpcklqdq
                   vpalignr vpblendw vperm2i128 vpslldq vmovdqa vpmovmskb)
        if(NOT assembly MATCHES "[\t ]${opcode}([\t \r\n]|$)")
            message(FATAL_ERROR
                "packed raw output for ${target} is missing ${opcode}\n${assembly}")
        endif()
    endforeach()
    foreach(pattern
            "movdqu[\t ]+[(]%r9[)], %xmm1"
            "movdqu[\t ]+%xmm1, [(]%r10[)]"
            "addps[\t ]+16[(]%r9[)], %xmm1"
            "psubw[\t ]+16[(]%r9[)], %xmm1"
            "phaddsw[\t ]+[(]%r9[)], %xmm1"
            "palignr[\t ]+[$]3, 16[(]%r9[)], %xmm1"
            "vaddps[\t ]+[(]%r11[)], %ymm2, %ymm3"
            "vpaddb[\t ]+32[(]%r11[)], %ymm2, %ymm3"
            "vpsubq[\t ]+32[(]%r11[)], %ymm2, %ymm3"
            "vpalignr[\t ]+[$]5, [(]%r11[)], %ymm2, %ymm3")
        if(NOT assembly MATCHES "${pattern}")
            message(FATAL_ERROR
                "packed typed-memory output for ${target} is missing ${pattern}\n${assembly}")
        endif()
    endforeach()
endforeach()

function(reject_feature name flag instruction feature)
    execute_process(
        COMMAND "${CC}" -S -march=haswell "${flag}" "${SOURCE}"
                -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0 OR
       NOT stderr MATCHES
           "target instruction '[$]::${instruction}' requires feature '${feature}'")
        message(FATAL_ERROR
            "${name} packed feature gate was not enforced\n${stdout}\n${stderr}")
    endif()
endfunction()

reject_feature(ssse3 -mno-ssse3 _pshufb ssse3)
reject_feature(sse41 -mno-sse4.1 _pblendw sse4.1)
reject_feature(avx -mno-avx _vaddps avx)
reject_feature(avx2 -mno-avx2 _vpaddb avx2)

execute_process(
    COMMAND "${CC}" --print-instructions
    RESULT_VARIABLE registry_status
    OUTPUT_VARIABLE registry
    ERROR_VARIABLE registry_stderr
)
if(NOT registry_status EQUAL 0)
    message(FATAL_ERROR "--print-instructions failed\n${registry_stderr}")
endif()
foreach(entry _movdqa _paddb _paddusb _palignr _pshufb _pblendw
              _vaddps _vpaddb _vpalignr _vperm2i128 _vpermq)
    string(FIND "${registry}" "$::${entry} instruction" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "instruction registry is missing $::${entry}")
    endif()
endforeach()

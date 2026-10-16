# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# If-conversion forms floating selects only where the target selects
# floating values without a branch: x86-64 masks (SSE logic, BLENDV with
# SSE4.1, VBLENDV with AVX) and MIPS IV conditional moves; the VR4300 keeps
# its branches, and so does any profile that traps the speculated operation.

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

function(expect label function pattern present)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "${label}: no function ${function}\n${assembly}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    if(present AND NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} lacks '${pattern}'\n${text}")
    elseif(NOT present AND text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} has '${pattern}'\n${text}")
    endif()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu)
set(x86_branch "\tj[a-z]+\t")
foreach(march x86-64 x86-64-v2 x86-64-v3)
    compile(x86-${march} -O2 ${x86} -march=${march})
    foreach(function distance wrap64 pick_ne pick_sign)
        expect(x86-${march} ${function} "${x86_branch}" FALSE)
    endforeach()
    # Two constants on an integer condition: 0.75 is 0x3f400000 and -0.5 is
    # 0xbf000000, selected in EAX with no mask, blend, or constant load.
    compile(x86-${march}-O3 -O3 ${x86} -march=${march})
    expect(x86-${march}-O3 offset "${x86_branch}" FALSE)
    expect(x86-${march}-O3 offset "\tandl\t[$]2151677952, %eax" TRUE)
    expect(x86-${march}-O3 offset "\txorl\t[$]3204448256, %eax" TRUE)
    expect(x86-${march}-O3 offset "blendv|andnps|movss\t[.]L" FALSE)
endforeach()
# Without machine combining the constants are loaded and blended.
compile(x86-no-combine -O3 ${x86} -march=x86-64-v3 -fno-machine-combine)
expect(x86-no-combine offset "\tvblendvps\t" TRUE)
expect(x86-no-combine offset "\tandl\t[$]2151677952, %eax" FALSE)
compile(x86-sse2 -O2 ${x86} -march=x86-64)
expect(x86-sse2 distance "\tcmpltss\t" TRUE)
expect(x86-sse2 distance "\tandnps\t" TRUE)
expect(x86-sse2 wrap64 "\tcmplesd\t" TRUE)
expect(x86-sse2 pick_ne "\tcmpneqss\t" TRUE)
expect(x86-sse2 pick_sign "\tnegl\t" TRUE)
compile(x86-sse41 -O2 ${x86} -march=x86-64-v2)
expect(x86-sse41 distance "\tblendvps\t" TRUE)
expect(x86-sse41 wrap64 "\tblendvpd\t" TRUE)
compile(x86-avx -O2 ${x86} -march=x86-64-v3)
expect(x86-avx distance "\tvcmpltss\t" TRUE)
expect(x86-avx distance "\tvblendvps\t" TRUE)
# A boolean mask instead of the fused comparison.
compile(x86-no-fusion -O2 ${x86} -march=x86-64 -fno-compare-select-fusion)
expect(x86-no-fusion distance "\tcmpltss\t" FALSE)
expect(x86-no-fusion distance "\tmovd\t%eax, %xmm0" TRUE)
foreach(mode "O2;-fno-if-conversion" "O0")
    string(REPLACE ";" "" label "x86-${mode}")
    compile(${label} -${mode} ${x86})
    expect(${label} distance "${x86_branch}" TRUE)
endforeach()
# A trapping profile speculates no subtraction; selecting computed values
# still runs its one comparison.
compile(x86-trapping -O2 ${x86} "--model=${MODEL}" -mprofile=trapping-fp)
expect(x86-trapping distance "${x86_branch}" TRUE)
expect(x86-trapping pick_ne "${x86_branch}" FALSE)

set(mips -target mips-unknown-elf -mabi=o32 -fif-conversion-limit=6)
set(mips_branch "\tb(c1[tf]|eq|ne)[a-z]*\t")
compile(mips4 -O2 ${mips} -march=mips4)
expect(mips4 distance "\tmov[tf][.]s\t" TRUE)
expect(mips4 wrap64 "\tmov[tf][.]d\t" TRUE)
expect(mips4 pick_ne "\tc[.]eq[.]s\t" TRUE)
expect(mips4 pick_sign "\tmov[nz][.]s\t" TRUE)
foreach(function distance wrap64 pick_ne pick_sign)
    expect(mips4 ${function} "${mips_branch}" FALSE)
endforeach()
foreach(mode "vr4300" "mips4;-mno-cond-move" "mips4;-fno-if-conversion")
    string(REPLACE ";" "" label "mips-${mode}")
    compile(${label} -O2 ${mips} -march=${mode})
    expect(${label} distance "${mips_branch}" TRUE)
    expect(${label} distance "\tmov[tf][.]s\t" FALSE)
endforeach()

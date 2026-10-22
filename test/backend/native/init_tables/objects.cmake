# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The initializer tables of the shipped profiles in objects of each format:
# MinGW-w64's .CRT$XC* and .dtors (inverted priorities) through ms_abi
# adapters that keep what ms_abi preserves, Mach-O's typed sections without
# priorities, and the ELF arrays of o32 and n64 with entries of the address
# width.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
find_program(LLVM_OBJDUMP NAMES llvm-objdump)
find_program(LLVM_READOBJ NAMES llvm-readobj)
find_program(LLVM_READELF NAMES llvm-readelf)
if(NOT LLVM_OBJDUMP OR NOT LLVM_READOBJ OR NOT LLVM_READELF)
    message(FATAL_ERROR "llvm-objdump, llvm-readobj, and llvm-readelf are required")
endif()

function(run_checked label)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

set(adapter "[^\n]*_adapter_[^\n]*\n")
run_checked(coff "${CC}" -O2 -DPRIORITIES=1 -target x86_64-w64-windows-gnu
            -c "${SOURCE}" -o "${OUTPUT}/coff.o")
run_checked(coff-relocations "${LLVM_OBJDUMP}" -r "${OUTPUT}/coff.o")
foreach(section "[.]CRT[$]XCU" "[.]CRT[$]XCT00101" "[.]dtors" "[.]dtors[.]65434")
    expect(coff "${stdout}"
        "RELOCATION RECORDS FOR \\[${section}\\]:\n[^\n]*\n0+ IMAGE_REL_AMD64_ADDR64 ${adapter}")
endforeach()
run_checked(coff-code "${LLVM_OBJDUMP}" -d "${OUTPUT}/coff.o")
expect(coff "${stdout}" "movq\t%rsi, " "movq\t%rdi, " "movdqu\t%xmm6, " "movdqu\t%xmm15, ")

run_checked(macho "${CC}" -O2 -target x86_64-apple-darwin -c "${SOURCE}"
            -o "${OUTPUT}/macho.o")
run_checked(macho-sections "${LLVM_READOBJ}" --sections --relocations "${OUTPUT}/macho.o")
expect(macho "${stdout}"
    "Name: __mod_init_func [^\n]*\n    Segment: __DATA [^}]*Type: ModInitFuncPointers"
    "Name: __mod_term_func [^\n]*\n    Segment: __DATA [^}]*Type: ModTermFuncPointers"
    "X86_64_RELOC_UNSIGNED 0 ${adapter}")
execute_process(COMMAND "${CC}" -O2 -DPRIORITIES=1 -target x86_64-apple-darwin
                -S "${SOURCE}" -o "${OUTPUT}/macho-priority.s"
    RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES
   "error: the constructor table 'x86_64-darwin-init' has no priorities")
    message(FATAL_ERROR "a Mach-O priority was not diagnosed\n${stderr}")
endif()

# NAME, the entry size, its relocation, then the compiler's target flags.
function(check_elf name size relocation)
    run_checked(${name} "${CC}" -O2 -DPRIORITIES=1 ${ARGN} -c "${SOURCE}"
                -o "${OUTPUT}/${name}.o")
    run_checked(${name}-sections "${LLVM_READELF}" -S -r -W "${OUTPUT}/${name}.o")
    foreach(section "[.]init_array +INIT_ARRAY" "[.]init_array[.]00101 +INIT_ARRAY"
            "[.]fini_array +FINI_ARRAY" "[.]fini_array[.]00101 +FINI_ARRAY")
        expect(${name} "${stdout}" "${section} +0+ [0-9a-f]+ 0+${size} 00 +WA ")
    endforeach()
    expect(${name} "${stdout}"
        "Relocation section '[.]rela?[.]init_array' [^\n]*\n[^\n]*\n[^\n]* ${relocation}")
endfunction()
check_elf(o32 4 R_MIPS_32 -mprofile=vr4300-o32)
check_elf(n64 8 R_MIPS_64 -target mips64el-unknown-elf -mabi=n64)

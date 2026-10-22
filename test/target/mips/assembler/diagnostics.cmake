# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `as` rejects invalid assembly with a diagnostic at the offending line and
# column, warns where GNU as warns, and writes no object after an error; it
# also reports the GNU forms it does not implement.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")

set(cases 0)
# expect(NAME STATUS TEXT REGEX [AS-OPTION...]): assemble TEXT, whose lines
# are separated by '|', and match stderr against REGEX.
function(expect name status text regex)
    string(REPLACE "|" "\n" source "${text}")
    set(input "${OUTPUT}.${name}.s")
    file(WRITE "${input}" "${source}\n")
    file(REMOVE "${OUTPUT}.${name}.o")
    execute_process(COMMAND "${AS}" ${ARGN} "${input}" -o "${OUTPUT}.${name}.o"
                    RESULT_VARIABLE result ERROR_VARIABLE stderr)
    if(status STREQUAL "error")
        if(result EQUAL 0 OR EXISTS "${OUTPUT}.${name}.o")
            message(FATAL_ERROR "${name}: expected failure without an object\n${stderr}")
        endif()
    elseif(NOT result EQUAL 0)
        message(FATAL_ERROR "${name}: expected success\n${stderr}")
    endif()
    if(NOT stderr MATCHES "${regex}")
        message(FATAL_ERROR "${name}: stderr does not match '${regex}'\n${stderr}")
    endif()
    math(EXPR count "${cases} + 1")
    set(cases ${count} PARENT_SCOPE)
endfunction()

set(o32 -target mips-unknown-elf -march=vr4300)
expect(isa error ".set mips1|        dsll $2, $3, 4"
       "isa.s:2:9: error: instruction 'dsll' is not available in mips1\n        dsll" ${o32})
expect(isa-push error ".set push|.set mips2|.set pop|        dsll $2, $3, 4|.set mips1|ll $2, 0($3)"
       "isa-push.s:6:1: error: instruction 'll' is not available in mips1" ${o32})
expect(immediate error "        addiu $2, $3, 40000"
       "immediate.s:1:23: error: immediate 40000 is out of range \\[-32768, 32767\\]" ${o32})
expect(unsigned error "        lui $2, 0x10000"
       "unsigned.s:1:17: error: immediate 65536 is out of range \\[0, 65535\\]" ${o32})
expect(mnemonic error "        frob $2, $3" "mnemonic.s:1:9: error: unknown instruction 'frob'" ${o32})
expect(register error "        addu $2, $3, $xyz"
       "register.s:1:22: error: unknown general register '[$]xyz'" ${o32})
expect(n64-register error "        addu $t4, $t5, $t6"
       "register [$]t4 does not exist in the n64 ABI" -target mips64-unknown-elf)
expect(directive error ".frobnicate 1" "directive.s:1:1: error: unknown directive '.frobnicate'" ${o32})
expect(pop error ".set pop" "pop.s:1:1: error: .set pop without a matching .set push" ${o32})
expect(double error "        add.d $f1, $f2, $f4"
       "double.s:1:15: error: a double-precision value needs an even register with 32-bit FPRs, not [$]f1"
       ${o32})
expect(single error "        add.s $f1, $f2, $f4"
       "single.s:1:15: error: a single-precision operation needs an even register without odd single-precision registers, not [$]f1"
       ${o32})
expect(local error ".set noreorder|        b .Lmissing|        nop"
       "local.s:2:11: error: undefined local symbol '.Lmissing'" ${o32})
expect(range error ".set noreorder|        beq $0, $0, far|        nop|        .space 0x40000|far: nop"
       "range.s:2:21: error: branch target is out of range" ${o32})
expect(operator error "        lw $2, %foo(x)($28)"
       "operator.s:1:16: error: relocation operator %foo is not supported" ${o32})
expect(nested-operator error "        lw $2, %got(%lo(x))($28)"
       "nested-operator.s:1:16: error: relocation operator %got[(]%lo[)] is not supported" ${o32})
expect(neg error "        lui $2, %hi(%neg(%gp_rel(f)))"
       "neg.s:1:17: error: %neg[(]%gp_rel[(][.][.][.][)][)] needs a 64-bit [(]n64[)] object" ${o32})
expect(li error "        li $2, 0x100000000"
       "li.s:1:16: error: li needs a 32-bit immediate; use dli for 64-bit values" ${o32})
expect(dli error ".set mips2|        dli $2, 5" "dli.s:2:9: error: instruction 'dli' is not available in mips2" ${o32})
expect(numeric error "        .word 1b" "numeric.s:1:15: error: no previous definition of local label '1'" ${o32})
expect(div error "        div $2, $3, $4"
       "div.s:1:9: error: instruction 'div' with these operands is not available in mips3"
       ${o32})
expect(cfi error ".cfi_offset 31, -4"
       "cfi.s:1:1: error: this directive must appear between .cfi_startproc and .cfi_endproc" ${o32})
expect(frame error ".cfi_startproc|        nop" "frame.s:1:1: error: missing .cfi_endproc" ${o32})
expect(redefined error "x:|x:" "redefined.s:2:1: error: symbol 'x' is already defined" ${o32})
expect(sync error ".set mips2|        sync 4"
       "sync.s:2:9: error: instruction 'sync' with these operands is not available in mips2" ${o32})
# A field that no form of dext takes; one that dinsm does not take.
expect(bit-field error "        dext $2, $3, 40, 40"
       "bit-field.s:1:22: error: immediate 40 is out of range \\[0, 31\\]"
       -target mips64-unknown-elf -march=mips64r2)
expect(bit-field-form error "        dinsm $2, $3, 4, 8"
       "bit-field-form.s:1:26: error: the operands of 'dinsm' must satisfy pos[+]size > 32"
       -target mips64-unknown-elf -march=mips64r2)
expect(byte error "        .byte 300" "byte.s:1:15: error: value 300 does not fit in 1 byte" ${o32})
expect(quad error "        .8byte symbol"
       "quad.s:1:16: error: an eight-byte symbolic value needs a 64-bit object" ${o32})
expect(jump error ".set noreorder|        j 0x1002|        nop"
       "jump.s:2:11: error: jump target is not word aligned" ${o32})
expect(macro error ".macro m|        nop" "macro.s:1:1: error: missing .endm" ${o32})
expect(required error ".macro m a:req|        li $2, \\a|.endm|        m"
       "required.s:4:9: error: missing value for required parameter 'a' of macro 'm'" ${o32})
expect(endif error ".if 1|        nop" "error: missing .endif at the end of the file" ${o32})
expect(flags error ".section .foo,\"a\"|.section .foo,\"aw\""
       "flags.s:2:1: error: changed section flags for .foo" ${o32})
expect(cross-section error ".data|a: .word 0|.section .other|c: .word 0|.text|        .word a - c"
       "cross-section.s:6:15: error: cannot represent a difference of symbols in different sections"
       ${o32})
expect(macro-line error ".macro bad|        frob|.endm|.set noat|        bad"
       "macro-line.s:5:9: error: unknown instruction 'frob'" ${o32})
expect(at warning "        addu $1, $2, $3"
       "at.s:1:14: warning: used [$]at without \".set noat\"" ${o32})
expect(fatal error "        addu $1, $2, $3"
       "fatal.s:1:14: error: used [$]at without \".set noat\"" ${o32} --fatal-warnings)
expect(nomacro warning ".set nomacro|        li $2, 0x12345678"
       "nomacro.s:2:16: warning: macro instruction expanded into multiple instructions" ${o32})
expect(error-directive error ".error \"stop here\"" "error-directive.s:1:1: error: stop here" ${o32})
expect(module-late error "        nop|.module fp=xx"
       "module-late.s:2:1: error: .module is not permitted after code or a .set directive" ${o32})
expect(module-option error ".module frob"
       "module-option.s:1:1: error: unsupported .module option 'frob'" ${o32})
expect(module-gp64 error ".module gp=64"
       "module-gp64.s:1:1: error: gp=64 needs an ABI with 64-bit registers, not [a-z0-9]+" ${o32})
expect(module-fp64 warning ".module fp=64"
       "module-fp64.s:1:1: warning: fp=64 with the 32-bit registers of the [a-z0-9]+ ABI" ${o32})
expect(set-option error ".set frob" "set-option.s:1:1: error: unsupported .set option 'frob'" ${o32})
expect(set-cpu error ".set arch=frob" "set-cpu.s:1:1: error: unknown CPU 'frob'" ${o32})
expect(fpr-number error "        add.s $f0, $2, $f4"
       "fpr-number.s:1:20: error: expected a floating-point register" ${o32})
expect(gp32-macro error "        ld $2, 0($4)"
       "gp32-macro.s:1:9: error: 'ld' with 32-bit GPRs is a two-word macro that is not supported" ${o32})
expect(half-symbol error "        .2byte symbol"
       "half-symbol.s:1:16: error: an o32 object has no relocation for a 2-byte field" ${o32})
expect(deferred-li error ".set noreorder|a: li $2, b - a|        .space 0x10000|b: nop"
       "deferred-li.s:2:11: error: immediate 65540 is out of range \\[-32768, 32767\\]" ${o32})
expect(gprel-addend error ".data|.space 0x10000|far: .word 0|.text|        lw $2, %gp_rel(far+0x8000)($28)"
       "gprel-addend.s:5:16: error: the %gp_rel addend 32768 does not fit the 16-bit field of an o32 relocation"
       ${o32})
expect(noat-macro error ".set noat|        slt $2, $3, 0x12345"
       "noat-macro.s:2:9: error: this macro needs [$]at, which \".set noat\" reserves" ${o32})
expect(trap-allegrex error "        teq $2, $3"
       "trap-allegrex.s:1:9: error: instruction 'teq' is not available in mips2"
       -target mipsallegrexel-sony-psp-elf -march=allegrex)
expect(command-gp64 error "        nop"
       "<command line>:1:1: error: gp=64 needs an ABI with 64-bit registers" ${o32} -mgp64)
message(STATUS "${cases} diagnostics matched")

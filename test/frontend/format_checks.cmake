# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The shipped $::format::printf and $::format::scanf checkers under the C
# type sizes of each shipped C ABI of ARCH: x86-64 System V and Microsoft, or
# MIPS o32, n64, and EABI32.
foreach(required CC ARCH OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile case target source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S -target ${target} -Wall ${ARGN}
        "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
    set(status "${result}" PARENT_SCOPE)
    set(err "${out}${error}" PARENT_SCOPE)
endfunction()

# expect(CASE TARGET ABI BODY): ARGN_EXPECTED lists "LINE:COLUMN: message"
# patterns, or is "clean". BODY is the function body, which starts on line 5
# of the file, after the declarations of printf, scanf, and entry.
function(expect case target abi body)
    set(source "[[abi(\"${abi}\"), check($::format::printf)]] i32 printf(in const u8 *format, ...);
[[abi(\"${abi}\"), check($::format::scanf)]] i32 scanf(in const u8 *format, ...);
global void entry(in i32 i, in u32 u, in i64 l, in u64 ul, in iptr ip, in uptr up, in i16 s, in u8 c,
                  in f32 f, in f64 d, in const u8 *text, in void *pointer) {
${body}
}
")
    compile(${case} ${target} "${source}" ${ARGN})
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${case}: compilation failed\n${err}")
    endif()
    set(count 0)
    foreach(expected IN LISTS ARGN_EXPECTED)
        if(expected STREQUAL "clean")
            if(NOT err STREQUAL "")
                message(FATAL_ERROR "${case}: expected no diagnostic\n${err}")
            endif()
            return()
        endif()
        if(NOT err MATCHES "${case}.x:${expected}")
            message(FATAL_ERROR "${case}: missing '${expected}'\n${err}")
        endif()
        math(EXPR count "${count} + 1")
    endforeach()
    string(REGEX MATCHALL "warning:" reports "${err}")
    list(LENGTH reports total)
    if(NOT total EQUAL count)
        message(FATAL_ERROR "${case}: expected ${count} warnings, got ${total}\n${err}")
    endif()
endfunction()

if(ARCH STREQUAL mips)
# MIPS: o32 and EABI32 have 32-bit long and size_t; n64 has 64-bit long and
# a binary128 long double.
set(mips_body [=[
    printf("%ld %zu\n", i, up);
    printf("%ld\n", l);
    printf("%lld %Lf\n", l, d);
]=])
set(ARGN_EXPECTED "6:21: warning: format '%ld' expects C 'long' \\(i32\\), but argument 2 has type 'i64'")
expect(o32 mips-unknown-elf o32 "${mips_body}")
expect(eabi32 mips-unknown-elf eabi32 "${mips_body}")
set(ARGN_EXPECTED
    "5:25: warning: format '%ld' expects C 'long' \\(i64\\), but argument 2 has type 'i32'"
    "7:29: warning: format '%Lf' expects C 'long double' \\(f128\\), but argument 3 has type 'f64'")
expect(n64 mips64-unknown-elf n64 "${mips_body}")

return()
endif()

set(x86 x86_64-unknown-linux-gnu)
set(clean_body [=[
    printf("%d %i %u %x %o %c %s %p %% %5.2f %-8s|\n", i, i, u, u, u, c, text, pointer, d, text);
    printf("%ld %lu %lld %llu %zu %zd %td %jd %hd %hhu %f %e %g %a\n", l, ul, l, ul, up, ip, ip, l, s, c, f, d, d, d);
    printf("%*d %.*s %-+5d %#x %#o %08.3f\n", i, i, i, text, i, u, u, d);
    printf("%2$s %1$d\n", i, text);
    printf(text, i, l);
    i32 count;
    i64 big;
    f32 single;
    f64 wide;
    u8 name[8];
    scanf("%d %ld %f %lf %s %7s %c %[a-z] %*d %n %p", &count, &big, &single, &wide, name, name, name, name, &count,
          &pointer);
]=])

# System V: long and long long are 64 bits, so %ld and %lld both take an i64.
set(ARGN_EXPECTED clean)
expect(sysv_clean ${x86} sysv_abi "${clean_body}")
set(ARGN_EXPECTED
    "6:21: warning: format '%ld' expects C 'long' \\(i64\\), but argument 2 has type 'i32' \\[-Wformat\\]"
    "7:20: warning: format '%d' expects C 'int' \\(i32\\), but argument 2 has type 'i64'"
    "8:20: warning: format '%s' expects a pointer to C 'char', but argument 2 has type 'i32'"
    "9:20: warning: format '%f' expects C 'double' \\(f64\\), but argument 2 has type 'i32'"
    "10:21: warning: format '%Lf' expects C 'long double' \\(f80\\), but argument 2 has type 'f64'"
    "11:12: warning: format '%d' expects a matching C 'int' \\(i32\\) argument"
    "12:23: warning: too many arguments for format"
    "13:12: warning: unknown conversion type character 'y' in format"
    "14:12: warning: zero-length format string"
    "15:12: warning: spurious trailing '%' in format"
    "16:12: warning: use of 'h' length modifier with 'f' type character"
    "17:12: warning: '#' flag used with '%#d' in format"
    "18:21: warning: format '%\\*d' expects C 'int' \\(i32\\) for '\\*', but argument 2 has type 'i64'"
    "19:17: warning: format '%f' expects a pointer to C 'float', but argument 2 has type 'f64 \\*'"
    "20:17: warning: format '%d' expects a pointer to C 'int', but argument 2 has type 'i32'"
    "21:17: warning: writing into a constant object \\(argument 2\\)"
    "22:12: warning: missing \\$ operand number in format")
expect(sysv_errors ${x86} sysv_abi [=[
    f64 wide;
    printf("%ld\n", i);
    printf("%d\n", l);
    printf("%s\n", i);
    printf("%f\n", i);
    printf("%Lf\n", d);
    printf("%d %d\n", i);
    printf("%d\n", i, i);
    printf("%y\n");
    printf("");
    printf("50%");
    printf("%hf\n", d);
    printf("%#d\n", i);
    printf("%*d\n", l, i);
    scanf("%f", &wide);
    scanf("%d", i);
    scanf("%d", (const i32 *)&i);
    printf("%1$d %d\n", i, i);
]=])

# Microsoft: long is 32 bits and long double is f64, so %ld takes an i32.
set(ARGN_EXPECTED
    "5:21: warning: format '%ld' expects C 'long' \\(i32\\), but argument 2 has type 'i64'"
    "8:21: warning: format '%lu' expects C 'long' \\(i32\\), but argument 2 has type 'u64'")
expect(ms ${x86} ms_abi [=[
    printf("%ld\n", l);
    printf("%ld %lld %llu\n", i, l, ul);
    printf("%Lf %zu\n", d, up);
    printf("%lu\n", ul);
]=])
expect(ms_windows x86_64-w64-windows-gnu ms_abi [=[
    printf("%ld\n", l);
    printf("%ld %lld %llu\n", i, l, ul);
    printf("%Lf %zu\n", d, up);
    printf("%lu\n", ul);
]=])

# Without -Wformat, or under -Wno-format, nothing is checked.
compile(disabled ${x86} "[[abi(\"sysv_abi\"), check($::format::printf)]] i32 printf(in const u8 *format, ...);
global void entry(in i64 l) { printf(\"%d\\n\", l); }
" -Wno-format)
if(NOT status EQUAL 0 OR NOT err STREQUAL "")
    message(FATAL_ERROR "disabled: -Wno-format\n${err}")
endif()

# A vprintf-like function takes the format alone; a Cross ABI function has no
# C types, so only the format and the number of arguments are checked; an
# attribute region checks every function in it.
compile(forms ${x86} [=[
[[abi("sysv_abi"), check($::format::printf)]] {
    i32 vprintf(in const u8 *format, in void *arguments);
    i32 fprintf(in void *stream, in const u8 *format, ...);
    i32 snprintf(in u8 *buffer, in uptr size, in const u8 *format, ...);
}
[[check($::format::printf)]] i32 report(in const u8 *format, ...);
global void entry(in void *stream, in u8 *buffer, in i64 l, in void *arguments) {
    vprintf("%d %d\n", arguments);
    vprintf("%q\n", arguments);
    fprintf(stream, "%d\n", l);
    snprintf(buffer, 8, "%s\n", l);
    report("%d\n", l);
    report("%d %d\n", l);
}
]=])
foreach(expected
        "forms.x:9:13: warning: unknown conversion type character 0xa in format|forms.x:9:13: warning: unknown conversion type character"
        "forms.x:10:29: warning: format '%d' expects C 'int' \\(i32\\), but argument 3 has type 'i64'"
        "forms.x:11:33: warning: format '%s' expects a pointer to C 'char', but argument 4 has type 'i64'"
        "forms.x:13:12: warning: format '%d' expects a matching C 'int' argument")
    if(NOT err MATCHES "${expected}")
        message(FATAL_ERROR "forms: missing '${expected}'\n${err}")
    endif()
endforeach()
string(REGEX MATCHALL "warning:" reports "${err}")
list(LENGTH reports total)
if(NOT status EQUAL 0 OR NOT total EQUAL 4)
    message(FATAL_ERROR "forms: expected four warnings\n${err}")
endif()

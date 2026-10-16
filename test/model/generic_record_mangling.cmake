# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Link names of functions whose signatures or generic arguments name generic
# record instances, through the shipped manglings and a model whose generic
# rule spells the instance's result and parameter types.

foreach(required CC MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

set(source "${OUTPUT}/instances.x")
file(WRITE "${source}" [=[
struct list<T> { T *items; uptr count; };
struct buf<T, uptr N> { T data[N]; };
global uptr count_words(in struct list<u32> *words) { return words->count; }
global T head<T>(in struct list<T> values) { return values.items[0]; }
global u32 first_of<T, uptr N>(in struct buf<T, N> *b) { return (u32)b->data[0]; }
global uptr size_of<T>(in T *value) { return sizeof(T); }
global struct pair { T a; T b; } make<T>(in T a, in T b) {
    struct pair result = { a, b };
    return result;
}
global u64 use(in struct list<u32> l, in struct buf<u16, 3> *b, in struct list<u8> *n) {
    return head(l) + first_of<u16, 3>(b) + size_of(n) + make(1u64, 2u64).b;
}
]=])

function(link_names name)
    set(arguments ${ARGN})
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu ${arguments} "-mmangling=${name}"
        -S "${source}" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "mangling ${name} failed\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}/${name}.s" assembly)
    set("${name}_assembly" "${assembly}" PARENT_SCOPE)
endfunction()

function(expect name)
    foreach(symbol IN LISTS ARGN)
        string(FIND "${${name}_assembly}" "${symbol}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "${name}: missing link name '${symbol}'\n${${name}_assembly}")
        endif()
    endforeach()
endfunction()

link_names(cross)
expect(cross ".globl count_words" ".globl \"head::<u32>\"" ".globl \"first_of::<u16,3uptr>\""
    ".globl \"size_of::<struct list<u8>>\"")
link_names(simple)
expect(simple ".globl _XN11count_words" ".globl _XN4headG1_t3u32" ".globl _XN8first_ofG2_t3u16_v53uptr"
    ".globl \"_XN7size_ofG1_t15struct list<u8>\"")
link_names(itanium)
expect(itanium ".globl \"_Z11count_wordsPu16struct list<u32>\"" ".globl \"_Z4headIjEu16struct list<u32>\"")
link_names(signature-test "--model=${MODEL}")
expect(signature-test ".globl \"head__u32__instruct list<u32>\""
    ".globl \"first_of__u32__inPstruct buf<u16,3uptr>\""
    ".globl \"make__struct make<u64>::pair__inu64_inu64\"")

# A record that a generic function's header defines is spelled through the
# instance that owns it, so the link name does not depend on the path of
# the source file.
foreach(directory one two/deeper)
    file(MAKE_DIRECTORY "${OUTPUT}/${directory}")
    file(COPY "${source}" DESTINATION "${OUTPUT}/${directory}")
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu "--model=${MODEL}"
        -mmangling=signature-test -S "${OUTPUT}/${directory}/instances.x" -o "${OUTPUT}/${directory}/instances.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "compilation in ${directory} failed\n${out}\n${err}")
    endif()
    file(STRINGS "${OUTPUT}/${directory}/instances.s" globals REGEX "^[ \t]*\\.globl")
    set("globals_${directory}" "${globals}")
    string(FIND "${globals}" "${OUTPUT}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "a link name contains the source path\n${globals}")
    endif()
endforeach()
if(NOT "${globals_one}" STREQUAL "${globals_two/deeper}")
    message(FATAL_ERROR "link names depend on the source path\n${globals_one}\n${globals_two/deeper}")
endif()

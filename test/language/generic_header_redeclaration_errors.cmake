# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A generic redeclaration must repeat each definition of its header exactly.

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# Each expected diagnostic is "line:column: kind: message", matched literally.
function(reject case source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(
        COMMAND "${CC}" -target x86_64-unknown-linux-gnu -S "${OUTPUT}/${case}.x"
                -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1)
        message(FATAL_ERROR "${case}: expected a diagnostic (status ${status})\n${out}\n${err}")
    endif()
    foreach(expected IN LISTS ARGN)
        string(FIND "${err}" "${case}.x:${expected}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${case}: missing '${case}.x:${expected}'\n${err}")
        endif()
    endforeach()
    string(FIND "${err}" "have incompatible interfaces\n" generic)
    if(NOT case MATCHES "^instance_" AND NOT generic EQUAL -1)
        message(FATAL_ERROR "${case}: unexpected second diagnostic\n${err}")
    endif()
endfunction()

set(body "{ struct Box result; return result; }")

reject(member_name "struct Box { T value; } wrap<T>(in T x);
struct Box { T other; } wrap<T>(in T x) ${body}
"
    "2:14: error: member 'other' of 'struct Box' does not match member 'value' of the earlier declaration of generic function 'wrap'"
    "1:14: note: earlier member 'value' is here")

reject(member_type "struct Box { T value; } wrap<T>(in T x);
struct Box { T *value; } wrap<T>(in T x) ${body}
"
    "2:14: error: member 'value' of 'struct Box' has a different type than in the earlier declaration of generic function 'wrap'"
    "1:14: note: earlier member 'value' is here")

reject(extra_member "struct Box { T value; } wrap<T>(in T x);
struct Box { T value; T extra; } wrap<T>(in T x) ${body}
"
    "2:23: error: member 'extra' of 'struct Box' is not in the earlier declaration of generic function 'wrap'"
    "1:1: note: earlier definition is here")

reject(missing_member "struct Box { T value; T extra; } wrap<T>(in T x);
struct Box { T value; } wrap<T>(in T x) ${body}
"
    "2:1: error: 'struct Box' lacks member 'extra' of the earlier declaration of generic function 'wrap'"
    "1:23: note: earlier member 'extra' is here")

reject(record_kind "struct Box { T value; } wrap<T>(in T x);
union Box { T value; } wrap<T>(in T x) { union Box result; return result; }
"
    "2:1: error: redeclaration of generic function 'wrap' defines 'union Box' where its earlier declaration defines 'struct Box'"
    "1:1: note: earlier definition is here")

reject(record_tag "struct Box { T value; } wrap<T>(in T x);
struct Crate { T value; } wrap<T>(in T x) { struct Crate result; return result; }
"
    "2:1: error: redeclaration of generic function 'wrap' defines 'struct Crate' where its earlier declaration defines 'struct Box'")

reject(anonymous_record "uptr measure<T>(in struct { T a; } *value);
uptr measure<T>(in struct Named { T a; } *value) { return sizeof(*value); }
"
    "2:20: error: redeclaration of generic function 'measure' defines 'struct Named' where its earlier declaration defines an anonymous struct")

reject(missing_definition "uptr measure<T>(in struct Shape { T a; } *value);
uptr measure<T>(in void *value) { return 0uptr; }
"
    "2:1: error: redeclaration of generic function 'measure' does not repeat the definition of 'struct Shape'"
    "1:20: note: earlier definition is here")

reject(extra_definition "uptr measure<T>(in void *value);
uptr measure<T>(in struct Shape { T a; } *value) { return 0uptr; }
"
    "2:20: error: redeclaration of generic function 'measure' defines 'struct Shape', which its earlier declaration does not define"
    "1:1: note: earlier declaration is here")

reject(record_attributes "struct Box [[packed]] { T value; u8 tag; } wrap<T>(in T x);
struct Box { T value; u8 tag; } wrap<T>(in T x) ${body}
"
    "2:1: error: 'struct Box' has different attributes than in the earlier declaration of generic function 'wrap'")

reject(member_attributes "struct Box { T value [[aligned(16)]]; } wrap<T>(in T x);
struct Box { T value [[aligned(8)]]; } wrap<T>(in T x) ${body}
"
    "2:14: error: member 'value' of 'struct Box' has different attributes than in the earlier declaration of generic function 'wrap'")

reject(bit_width "struct Flags { u32 low : 3; T value; } wrap<T>(in T x);
struct Flags { u32 low : 4; T value; } wrap<T>(in T x) { struct Flags result; return result; }
"
    "2:16: error: member 'low' of 'struct Flags' has a different bit-field width than in the earlier declaration of generic function 'wrap'")

reject(enumerator_value "enum Level { Low = 1, High = 5 } classify<T>(in T x);
enum Level { Low = 1, High = 6 } classify<T>(in T x) { return Low; }
"
    "2:23: error: enumerator 'High' of 'enum Level' has a different value than in the earlier declaration of generic function 'classify'"
    "1:23: note: earlier enumerator 'High' is here")

reject(enumerator_name "enum Level { Low, High } classify<T>(in T x);
enum Level { Low, Top } classify<T>(in T x) { return Low; }
"
    "2:19: error: enumerator 'Top' of 'enum Level' does not match enumerator 'High' of the earlier declaration of generic function 'classify'")

reject(enumeration_underlying "enum Level [[underlying(u8)]] { Low } classify<T>(in T x);
enum Level { Low } classify<T>(in T x) { return Low; }
"
    "2:1: error: 'enum Level' has different attributes than in the earlier declaration of generic function 'classify'")

# A dependent extent is checked for each instance after substitution.
reject(instance_extent "struct Buf { T items[N]; } fill<T, uptr N>(in T x);
struct Buf { T items[N + 1uptr]; } fill<T, uptr N>(in T x) { struct Buf result; return result; }
global uptr use() { return sizeof(fill<u16, 3>((u16)0)); }
"
    "1:1: error: generic declarations of 'fill' have incompatible interfaces after substitution")

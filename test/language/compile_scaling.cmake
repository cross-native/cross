# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Large functions compile in time roughly linear in their size. Record chains
# must not rescan the source table per record or revalidate the chain for each
# record laid out. Each case takes a second or two; the timeout catches a
# return to that work.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile case level source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${level} -target x86_64-unknown-linux-gnu
                            "${input}" -o "${OUTPUT}/${case}.s"
        TIMEOUT 15
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${case} ${level} failed or timed out (${status})\n${out}\n${err}")
    endif()
endfunction()

string(REPEAT " + x" 16000 chain)
compile(operator_chain -O0 "global u32 f(in u32 x) {\n    return x${chain};\n}\n")

set(branches "")
set(cases "")
set(temporaries "")
foreach(index RANGE 1 8000)
    string(APPEND branches "    if (x == ${index}u32) y += ${index}u32;\n")
    string(APPEND cases "    case ${index}u32: y = ${index}u32; break;\n")
    math(EXPR previous "${index} - 1")
    string(APPEND temporaries "    u32 v${index} = v${previous} + ${index}u32;\n")
endforeach()
set(prologue "global u32 f(in u32 x) {\n    u32 y = 0u32;\n")
compile(sequential_ifs -O0 "${prologue}${branches}    return y;\n}\n")
compile(sequential_ifs -O2 "${prologue}${branches}    return y;\n}\n")
compile(large_switch -O2
    "${prologue}    switch (x) {\n${cases}    default: break;\n    }\n    return y;\n}\n")
compile(many_locals -O2
    "global u32 f(in u32 x) {\n    u32 v0 = x;\n${temporaries}    return v8000;\n}\n")

# Nested blocks leave a chain of forwarding joins at -O2.
string(REPEAT "    if (x) {\n" 4000 nested_open)
string(REPEAT "    }\n" 4000 nested_close)
compile(nested_ifs -O2
    "global u32 f(in u32 x) {\n${nested_open}    x += 1u32;\n${nested_close}    return x;\n}\n")

# Translation-time copies of a by-value record chain query the layout of each
# level; every query checks the rest of the chain and every layout context
# validates it. The local chain resolves through private evaluation views.
set(records "")
set(local_records "")
foreach(index RANGE 0 479)
    math(EXPR next "${index} + 1")
    string(APPEND records "struct R${index} { struct R${next} child; };\n")
    if(index LESS 360)
        string(APPEND local_records "    struct L${index} { struct L${next} child[1]; };\n")
    endif()
endforeach()
compile(record_chain -O0 "${records}struct R480 { uptr value; };\n\
[[eval_only]] static uptr copy_chain() {\n    struct R0 first = {};\n\
    *((uptr *)&first) = 7uptr;\n    struct R0 second = first;\n\
    return *((uptr *)&second) == 7uptr ? sizeof(second) : 0uptr;\n}\n\
$::static_assert($::eval(copy_chain()) == sizeof(uptr), \"record chain\");\n")
compile(local_record_chain -O0 "[[eval_only]] static uptr copy_local_chain() {\n\
${local_records}    struct L360 { uptr value; };\n    struct L0 first = {};\n\
    *((uptr *)&first) = 7uptr;\n    struct L0 second = first;\n\
    return *((uptr *)&second) == 7uptr ? sizeof(second) : 0uptr;\n}\n\
$::static_assert($::eval(copy_local_chain()) == sizeof(uptr), \"local record chain\");\n")

# Semantic preparation checks the by-value closure of each record in a chain
# once, not once per record: a file-scope chain behind one layout query and a
# chain of local records in a runtime function.
set(chain_records "")
set(chain_locals "")
foreach(index RANGE 0 7999)
    math(EXPR next "${index} + 1")
    string(APPEND chain_records "struct G${index} { struct G${next} child; };\n")
    if(index LESS 4000)
        string(APPEND chain_locals "    struct V${index} { struct V${next} child; };\n")
    endif()
endforeach()
compile(record_validation -O0 "${chain_records}struct G8000 { uptr value; };\n\
$::static_assert(sizeof(struct G0) == sizeof(uptr), \"record validation\");\n")
compile(local_record_validation -O0 "global uptr f() {\n${chain_locals}\
    struct V4000 { uptr value; };\n    return sizeof(struct V0);\n}\n")

# Translation-time evaluation sizes a deeply nested array in one pass over its
# layers, not once more for every layer.
string(REPEAT "[1]" 2400 dimensions)
compile(deep_array_object -O0 "[[eval_only]] static uptr deep_array() {\n\
    u8 value${dimensions};\n    *((u8 *)&value) = 7u8;\n    return (uptr)*((u8 *)&value);\n}\n\
$::static_assert($::eval(deep_array()) == 7uptr, \"deep array object\");\n")

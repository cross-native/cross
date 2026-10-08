# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A register object has no address, and typedef alignment is diagnosed rather
# than ignored.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu "${input}"
                            -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(expected STREQUAL "")
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${case} was rejected\n${out}\n${err}")
        endif()
    elseif(status EQUAL 0 OR
           NOT err MATCHES "${case}\\.x:[0-9]+:[0-9]+: error: ${expected}")
        message(FATAL_ERROR "${case} was not diagnosed with '${expected}'\n${out}\n${err}")
    endif()
endfunction()

set(prelude "void sink(in u64 *pointer);\ntypedef u32 u32x4 [[vector_size(16)]];\n")
set(no_address "a register object has no address")
compile(register_initializer "${no_address}" "${prelude}
global u64 f(in u64 input) { register u64 x = input; u64 *p = &x; return *p; }\n")
compile(register_argument "${no_address}" "${prelude}
global u64 f(in u64 input) { register u64 x = input; sink(&x); return x; }\n")
compile(register_parenthesized "${no_address}" "${prelude}
global u64 f(in u64 input) { register u64 x = input; sink(&(x)); return x; }\n")
compile(register_vector_lane "${no_address}" "${prelude}
global u32 f(in u32 input) { register u32x4 v = input; u32 *lane = &v[1]; return *lane; }\n")
compile(register_unevaluated "${no_address}" "${prelude}
global uptr f(in u64 input) { register u64 x = input; return sizeof(&x); }\n")
compile(register_unused_helper "${no_address}" "${prelude}
inline u64 *helper(in u64 input) { register u64 y = input; return &y; }
global u64 f(in u64 input) { return input; }\n")
compile(register_array "an aggregate object cannot be bound to a machine register" "
global u32 f(in u32 input) { register u32 values[4]; values[1] = input; return values[1]; }\n")
compile(register_value "" "${prelude}
global u64 f(in u64 input) { register u64 x = input; x += 1u64; return x; }\n")
compile(register_pointer_element "" "${prelude}
global u64 f() { u64 data[2] = {1, 2}; register u64 *p = data; sink(&p[1]); return data[1]; }\n")

compile(typedef_aligned "aligned on a typedef is not implemented" "
typedef u32 wide [[aligned(16)]];
global wide object;\n")
compile(typedef_record_aligned "" "
typedef struct [[aligned(16)]] { u32 value; } Wide;
global Wide object;
\$::static_assert(\$::alignof(Wide) == 16uptr, \"record alignment through a typedef\");\n")

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
file(MAKE_DIRECTORY "${OUTPUT}")
set(prefix "struct P { u32 x; u32 values[2]; }; struct N { struct P p; }; ")
function(reject name body expected)
    file(WRITE "${OUTPUT}/${name}.x" "${prefix}${body}\n")
    foreach(level O0 O3)
        execute_process(COMMAND "${CC}" -S -${level} "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
            RESULT_VARIABLE status ERROR_VARIABLE error)
        if(status EQUAL 0 OR NOT error MATCHES "${expected}" OR
           NOT error MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "missing ${name}/${level} diagnostic: ${error}")
        endif()
    endforeach()
endfunction()
reject(assign "global void f(in const struct P p) { p.x = 1u32; }" "const subobject")
reject(update "global void f(in const struct P p) { ++p.x; }" "const subobject")
reject(array "global void f(in const struct P p) { p.values[1] = 1u32; }" "const subobject")
reject(nested "global void f(in const struct N n) { n.p.x += 1u32; }" "const subobject")
reject(address "global u32 *f(in const struct P p) { return &p.x; }" "discards qualifiers")
reject(decay "global u32 *f(in const struct P p) { return p.values; }" "discards qualifiers")

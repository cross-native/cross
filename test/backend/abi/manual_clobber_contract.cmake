# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A fully custom unresolved declaration, whose every endpoint is manual and
# which names no abi base, states its clobbers.
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

set(missing "a fully custom unresolved declaration requires a clobber attribute")
set(use "global u64 use(in u64 v) { return external(v); }\n")
compile(result "${missing}" "global u64 external(in u64 a \"rdi\") -> \"rax\";\n${use}")
compile(unused "${missing}" "global u64 external(in u64 a \"rdi\") -> \"rax\";\n")
compile(void_result "${missing}" "void normalize(i32 status \"eax\");\n")
compile(stack_endpoint "${missing}" "void notify(in u32 code \"push\");\n")
compile(redeclaration "${missing}" "global u64 external(in u64 a \"rdi\") -> \"rax\" [[clobber()]];
global u64 external(in u64 a \"rdi\") -> \"rax\";\n${use}")

compile(empty_clobber "" "global u64 external(in u64 a \"rdi\") -> \"rax\" [[clobber()]];\n${use}")
compile(named_clobber "" "[[clobber(\"rcx\", \"flags\")]] global u64 external(in u64 a \"rdi\") -> \"rax\";\n${use}")
compile(abi_base "" "[[abi(\"sysv_abi\")]] global u64 external(in u64 a \"rdi\") -> \"rax\";\n${use}")
compile(automatic_result "" "global u64 external(in u64 a \"rdi\");\n${use}")
compile(automatic_parameter "" "global u64 external(in u64 a \"rdi\", in u64 b) -> \"rax\";
global u64 use(in u64 v) { return external(v, v); }\n")
compile(defined "" "u64 external(in u64 a \"rdi\") -> \"rax\";
u64 external(in u64 a \"rdi\") -> \"rax\" { return a + 1u64; }\n${use}")
compile(no_endpoint "" "void tick();\nglobal void use() { tick(); }\n")
compile(pointer_type "" "typedef u64 (*custom_fn)(in u64 a \"rdi\") -> \"rax\";
global u64 use(in custom_fn f, in u64 v) { return f(v); }\n")

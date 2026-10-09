# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODE MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
elseif(MODE MATCHES "^mips")
    list(APPEND flags -target "${MODE}-unknown-linux-gnu")
endif()
function(check name expected source)
    if(DEFINED CASE_FILTER AND NOT "${name}" MATCHES "${CASE_FILTER}")
        return()
    endif()
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: unexpected rejection\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
set(declarations [=[
    static uptr two_values<uptr N, bool B>() { return N + (uptr)B; }
    static uptr sized<uptr N>(in u8 (*pointer)[N]) { return sizeof(*pointer); }
    global u32 first, second;
    global u8 bytes[8];
    enum E [[underlying(u16)]] { one = 1u16, two = 2u16 };
    global void owner() { global label point: ; }
    global void other() { global label point: ; }
    static u32 pointer_value<u32 *P>() { return 3u32; }
    static u32 pointer_addend<u8 *P>() { return 5u32; }
    static u32 label_value<label L>() { return 7u32; }
    static uptr dependent_pointer<T, T *P>() { return sizeof(*P); }
    static uptr dependent_value_pointer<uptr N, u8 (*)[N] P>() { return sizeof(*P); }
    static u32 invalid_body<uptr N>() { return missing; }
]=])
foreach(type uptr i8 u8 u128 bool "enum E" "u32 *" "u8 *" label)
    string(MAKE_C_IDENTIFIER "${type}" key)
    string(APPEND declarations "static ${type} value_${key}<${type} N>() { return N; }\n")
endforeach()
string(APPEND declarations "static uptr forwarded<uptr N>() { return value_uptr<N>(); }\n")
if(MODE STREQUAL custom)
    string(APPEND declarations [=[
        typedef i32 (*Stack)(in i32 n) [[abi("stack_result_abi")]];
        [[abi("stack_result_abi")]] static i32 stack(in i32 n) { return n; }
        static uptr stack_value<Stack F>() { return sizeof(F); }
        struct Pair { u64 low; u64 high; };
        typedef struct Pair (*Memory)(in u32 n) [[abi("memory_result_abi")]];
        [[abi("memory_result_abi")]] static struct Pair memory(in u32 n) {
            struct Pair result = {(u64)n, (u64)n}; return result;
        }
        static uptr memory_value<Memory F>() { return sizeof(F); }
        typedef i32 (*Alias)(in i32 n) [[abi("test_abi")]];
        [[abi("test_sysv")]] static i32 canonical(in i32 n) { return n; }
        static uptr alias_value<Alias F>() { return sizeof(F); }
        [[abi("odd_abi")]] static i32 odd(in i32 n) { return n; }
    ]=])
endif()
function(check_body name unused invoked body)
    foreach(type uptr i8 u8 u128 bool "enum E" "u32 *" "u8 *" label)
        string(MAKE_C_IDENTIFIER "${type}" key)
        string(REPLACE "value<${type}, " "value_${key}<" body "${body}")
    endforeach()
    foreach(role helper macro expander)
        set(attribute "")
        set(parameter "in $::meta::tokens input")
        if(role STREQUAL helper)
            set(invoke "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return check_value(input); } global u32 entry() { return apply!(1u32) + apply!(2u32); }")
        elseif(role STREQUAL macro)
            set(attribute "[[macro]]")
            set(invoke "global u32 entry() { return check_value!(1u32) + check_value!(2u32); }")
        else()
            set(attribute "[[syntax_expander]]")
            set(parameter "in $::meta::syntax_match input")
            set(invoke "syntax Check : expression { prefix \"value_check\"; match \"(\" \")\"; expand check_value; } syntax Check; global u32 entry() { return value_check() + value_check(); }")
        endif()
        set(source "${declarations}
            ${attribute} static $::meta::tokens check_value(${parameter}) { ${body} return $::quote {1u32}; }")
        check(${name}_${role}_unused "${unused}" "${source}\nglobal u32 entry() { return 1u32; }")
        check(${name}_${role}_invoked "${invoked}" "${source}\n${invoke}")
    endforeach()
endfunction()
set(four "$::meta::len($::quote {a b c d})")
check_body(integer pass pass "if (value<uptr, ${four}>() != 4uptr) return $::quote {wrong};")
check_body(forwarded pass pass "if (forwarded<${four}>() != 4uptr) return $::quote {wrong};")
check_body(signed pass pass "if (value<i8, (0i8 - (i8)${four})>() != -4i8) return $::quote {wrong};")
check_body(wide pass pass "if (value<u128, ((u128)${four} << 65u32)>() != (4u128 << 65u32)) return $::quote {wrong};")
check_body(boolean pass pass "if (!value<bool, (${four} == 4uptr)>()) return $::quote {wrong};")
check_body(enumeration pass pass "if (value<enum E, (${four} ? one : two)>() != one) return $::quote {wrong};")
check_body(pointer pass pass "if (pointer_value<(${four} ? &first : &second)>() != 3u32) return $::quote {wrong};")
check_body(pointer_addend pass pass "if (pointer_addend<(bytes + ${four})>() != 5u32) return $::quote {wrong};")
check_body(label pass pass "if (label_value<(${four} ? owner::point : other::point)>() != 7u32) return $::quote {wrong};")
check_body(required pass pass "typedef u8 A[value<uptr, ${four}>()]; $::static_assert(sizeof(A) == 4uptr, \"generic required value\");")
check_body(signature pass pass "u8 local[4]; if (sized<${four}>(&local) != 4uptr) return $::quote {wrong};")
check_body(overflow pass "not representable" "if (0u32) value<u8, (${four} + 255uptr)>();")
check_body(bool_overflow pass "not representable" "if (0u32) value<bool, ${four}>();")
check_body(independent_later "not representable" "not representable" "if (0u32) two_values<${four}, 2u32>();")
check_body(independent_type "not an integer" "not an integer" "if (0u32) value<u8, (${four} ? 1.0f32 : 2.0f32)>();")
check_body(unselected pass pass "if (value<u8, (1u32 ? 7u32 : ${four})>() != 7u8) return $::quote {wrong};")
check_body(independent_size pass pass "typedef u8 A[sizeof(value<u8, ${four}>())]; $::static_assert(sizeof(A) == 1uptr, \"result type\");")
check_body(local_value "runtime local or parameter" "runtime local or parameter" "uptr local = 4uptr; if (0u32) value<uptr, local>();")
check_body(pointer_type "incompatible|cannot convert" "incompatible|cannot convert" "if (0u32) pointer_value<(${four} ? bytes : bytes)>();")
check_body(pointer_local "runtime local or parameter|local address" "runtime local or parameter|local address" "u32 local; if (0u32) pointer_value<&local>();")
check_body(pointer_null pass pass "if (pointer_value<(${four} ? 0uptr : 0uptr)>() != 3u32) return $::quote {wrong};")
check_body(label_identity pass pass "if (value<label, (${four} ? owner::point : other::point)>() != owner::point) return $::quote {wrong};")
check_body(dependent_pointer pass pass "typedef u8 A[${four} * 2uptr]; if (dependent_pointer<A, &bytes>() != 8uptr) return $::quote {wrong};")
check_body(dependent_value_pointer pass pass "if (dependent_value_pointer<(${four} * 2uptr), &bytes>() != 8uptr) return $::quote {wrong};")
check_body(dependent_value_mismatch pass "incompatible|cannot convert" "if (0u32) dependent_value_pointer<${four}, &bytes>();")
check_body(callee_body pass "unknown|undeclared|unresolved" "if (0u32) invalid_body<${four}>();")
if(MODE STREQUAL custom)
    check_body(stack_interface pass pass "if (stack_value<(${four} ? stack : stack)>() != sizeof(Stack)) return $::quote {wrong};")
    check_body(memory_interface pass pass "if (memory_value<(${four} ? memory : memory)>() != sizeof(Memory)) return $::quote {wrong};")
    check_body(alias_interface pass pass "if (alias_value<(${four} ? canonical : canonical)>() != sizeof(Alias)) return $::quote {wrong};")
    check_body(stack_mismatch "cannot change its callable ABI" "cannot change its callable ABI" "if (0u32) stack_value<(${four} ? odd : odd)>();")
endif()
foreach(form structured projected discarded)
    set(value "$::syntax::node(input, \"value\")")
    if(form STREQUAL projected)
        set(value "$::meta::tokens(${value})")
    endif()
    set(replacement "$::quote { $::unquote(${value}) }")
    set(invoke "return helper(input);")
    set(argument "${four}")
    if(form STREQUAL discarded)
        set(replacement "$::quote {}")
        set(invoke "return input;")
        set(argument "(${four} + 255uptr)")
    endif()
    check(copied_${form} pass "
        ${declarations}
        [[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) {
            return ${replacement}; }
        syntax Emit : item { prefix \"copy_value\"; match \"{\" value:function_def \"}\"; expand emit; }
        syntax Emit;
        copy_value { static $::meta::tokens helper(in $::meta::tokens input) {
            if (value_u8<${argument}>() != 4u8) return $::quote {wrong}; return input;
        } }
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { ${invoke} }
        global u32 entry() { return apply!(1u32); }")
endforeach()

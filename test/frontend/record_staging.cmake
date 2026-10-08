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
function(check_body name unused invoked body)
    foreach(role helper macro expander)
        set(attribute "")
        set(parameter "in $::meta::tokens input")
        if(role STREQUAL helper)
            set(invoke "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return check_record(input); } global u32 entry() { return apply!(1u32) + apply!(2u32); }")
        elseif(role STREQUAL macro)
            set(attribute "[[macro]]")
            set(invoke "global u32 entry() { return check_record!(1u32) + check_record!(2u32); }")
        else()
            set(attribute "[[syntax_expander]]")
            set(parameter "in $::meta::syntax_match input")
            set(invoke "syntax Check : expression { prefix \"record_check\"; match \"(\" \")\"; expand check_record; } syntax Check; global u32 entry() { return record_check() + record_check(); }")
        endif()
        set(source "${attribute} static $::meta::tokens check_record(${parameter}) { ${body} return $::quote { 1u32 }; }")
        check(${name}_${role}_unused "${unused}" "${source}\nglobal u32 entry() { return 1u32; }")
        check(${name}_${role}_invoked "${invoked}" "${source}\n${invoke}")
    endforeach()
endfunction()

set(two "$::meta::len($::quote { a b })")
set(three "$::meta::len($::quote { a b c })")
set(four "$::meta::len($::quote { a b c d })")
set(zero "$::meta::len($::quote {})")
check_body(array pass pass "struct R { u8 bytes[${two}]; uptr tail; }; if (0u32) { static struct R object; static uptr address = (uptr)&object.tail; }")
check_body(width pass pass "struct R { u32 bits : ${three}; }; struct R object = { 5u32 }; object.bits = 3u32; if (object.bits != 3u32) return $::quote { wrong }; ")
check_body(alignment pass pass "struct R [[aligned(${four})]] { u8 byte; }; $::static_assert($::alignof(struct R) == 4uptr, \"alignment\");")
check_body(member_alignment pass pass "struct R { u8 first; u8 [[aligned(${four})]] second; }; $::static_assert($::alignof(struct R) == 4uptr, \"member alignment\");")
check_body(aggregate pass pass "struct R { u8 bytes[${two}]; u32 bits : ${three}; uptr tail; }; struct R object = {{7u8, 9u8}, 5u32, 13uptr}; if (sizeof(object.bytes) != 2uptr || object.bytes[1] != 9u8 || object.bits != 5u32 || object.tail != 13uptr) return $::quote { wrong };")
check_body(local_types pass pass "u16 local; struct R { u8 bytes[sizeof(local) + ${two}]; }; $::static_assert(sizeof(struct R) == 4uptr, \"local types\");")
check_body(local_pending_type pass pass "typedef u8 A[${two}]; A local; struct R {u8 bytes[sizeof(local)];}; $::static_assert(sizeof(struct R) == 2uptr, \"prepared local\");")
check_body(independent_alignment pass pass "struct R { u32 values[${two}]; }; $::static_assert($::alignof(struct R) == $::alignof(u32), \"independent alignment\");")
check_body(independent_false "fixed array bound must be a positive integer" "fixed array bound must be a positive integer" "struct R { u32 values[${two}]; }; typedef u8 Invalid[$::alignof(struct R) != $::alignof(u32)];")
check_body(width_alignment_false "fixed array bound must be a positive integer" "fixed array bound must be a positive integer" "struct R { u32 bits : ${three}; }; typedef u8 Invalid[$::alignof(struct R) != $::alignof(u32)];")
check_body(self_alignment pass pass "struct R { u8 bytes[$::alignof(struct R) + ${two}]; u32 value; }; $::static_assert($::alignof(struct R) == $::alignof(u32), \"self alignment\");")
check_body(pointer_size pass "fixed array bound must be a positive integer" "struct R { u8 (*pointer)[${zero}]; }; $::static_assert(sizeof(struct R) == sizeof(uptr), \"pointer size\");")
check_body(unused_zero pass "fixed array bound must be a positive integer" "if (0u32) { struct R { u8 bytes[${zero}]; }; }")
check_body(unused_bad_width pass "zero-width bit-field must be unnamed" "if (0u32) { struct R { u32 bits : ${zero}; }; }")
check_body(unused_bad_alignment pass "positive power-of-two" "if (0u32) { struct R [[aligned(${three})]] { u8 byte; }; }")
check_body(independent_width "width exceeds its base type" "width exceeds its base type" "struct R { u8 bytes[${two}]; u32 bits : 33u32; };")
check_body(independent_attribute "positive power-of-two" "positive power-of-two" "struct R [[aligned(3u32)]] { u8 bytes[${two}]; };")
check_body(independent_atomic "atomic qualifier requires" "atomic qualifier requires" "struct Inner {u8 byte;}; typedef struct Inner I; struct R { u8 bytes[${two}]; [[atomic]] I invalid; };")
check_body(independent_initializer "excess entry in aggregate initializer" "excess entry in aggregate initializer" "struct R { u32 bits : ${three}; }; struct R object = {1u32, 2u32};")
check_body(independent_destination "no member named" "no member named" "struct R { u8 bytes[${two}]; }; struct R object = {.missing = 1u32};")
check_body(independent_duplicate "duplicate destination" "duplicate destination" "struct R [[aligned(${four})]] { u8 bytes[2]; }; struct R object = {.bytes[0] = 1u8, .bytes[0] = 2u8};")
check_body(independent_initializer_type "meta values cannot initialize runtime aggregate members" "meta values cannot initialize runtime aggregate members" "struct R { u32 bits : ${three}; }; struct R object = {$::quote { a }};")
check_body(nested_pending pass pass "struct Inner {u8 bytes[${two}];}; struct Outer { struct Inner values[2]; }; struct Outer object = {{{{7u8, 9u8}}, {{11u8, 13u8}}}}; if (object.values[1].bytes[1] != 13u8) return $::quote {wrong};")
check_body(pointer_independent "fixed array bound must be a positive integer" "fixed array bound must be a positive integer" "struct R { u8 (*pointer)[${two}]; }; typedef u8 Invalid[sizeof(struct R) != sizeof(uptr)];")
check(generic_requirements pass [=[
    [[eval_only]] static uptr extent<T>() { return $::meta::len($::quote {a b}) * sizeof(T); }
    static $::meta::tokens helper(in $::meta::tokens input) {
        struct R [[aligned(extent<u16>())]] { u8 bytes[extent<uptr>()]; u32 bits : extent<u8>(); };
        struct R object = {{7u8, 9u8}, 3u32};
        if (sizeof(object.bytes) != 2uptr * sizeof(uptr) || object.bits != 3u32) return $::quote {wrong};
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    global u32 entry() { return apply!(1u32) + apply!(2u32); }
]=])
check(repeated_helper_requirements pass [=[
    [[eval_only]] static uptr extent() {
        struct R { u8 bytes[$::meta::len($::quote {a b})]; };
        return sizeof(struct R);
    }
    static $::meta::tokens helper(in $::meta::tokens input) {
        typedef u8 A[extent()];
        typedef u8 B[extent()];
        struct R [[aligned(extent())]] { A first; B second; };
        if (sizeof(struct R) != 4uptr) return $::quote {wrong};
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    global u32 entry() { return apply!(1u32) + apply!(2u32); }
]=])
check(ordinary_self_alignment pass [=[
    static uptr alignment<T>() { return $::alignof(T); }
    struct R [[aligned(alignment<u32>())]] { u8 bytes[$::alignof(struct R)]; u32 value; };
    $::static_assert($::alignof(struct R) == $::alignof(u32), "independent alignment");
    global uptr entry() { return sizeof(struct R); }
]=])
check(generic_records pass [=[
    static $::meta::tokens helper<T>(in $::meta::tokens input) {
        struct R { T values[$::meta::len($::quote {a b})]; uptr tail; };
        struct R object = {{(T)7u32, (T)9u32}, 13uptr};
        if (sizeof(object.values) != 2uptr * sizeof(T) || object.values[1] != (T)9u32)
            return $::quote {wrong};
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<u16>(helper<uptr>(input)); }
    global u32 entry() { return apply!(1u32) + apply!(2u32); }
]=])
check(global_context "active expansion context" [=[
    struct R { u8 bytes[$::meta::len($::quote {a b})]; };
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { sizeof(struct R); return input; }
    global u32 entry() { return apply!(1u32); }
]=])
check(discarded pass [=[
    [[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
    syntax Drop : item { prefix "drop_records"; match "{" value:function_def "}"; expand drop; }
    syntax Drop;
    drop_records { static $::meta::tokens helper(in $::meta::tokens input) {
        struct R [[aligned($::meta::len($::quote {}))]] {void bad;}; return input;
    } }
    global u32 entry() { return 1u32; }
]=])
foreach(form structured projected)
    set(value "$::syntax::node(input, \"value\")")
    if(form STREQUAL projected)
        set(value "$::meta::tokens(${value})")
    endif()
    set(prefix "[[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) {
        return $::quote { $::unquote(${value}) }; }
        syntax Emit : item { prefix \"copy_records\"; match \"{\" value:function_def \"}\"; expand emit; }
        syntax Emit;")
    set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32) + apply!(2u32); }")
    check(copied_${form} pass "${prefix}
        copy_records { static $::meta::tokens helper(in $::meta::tokens input) {
            struct R [[aligned(${four})]] { u8 bytes[${two}]; u32 bits : ${three}; };
            struct R object = {{7u8, 9u8}, 3u32};
            if (object.bytes[1] != 9u8 || object.bits != 3u32) return $::quote {wrong}; return input;
        } } ${suffix}")
    check(copied_zero_${form} "zero-width bit-field must be unnamed" "${prefix}
        copy_records { static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { struct R {u32 bits : ${zero};}; } return input;
        } } ${suffix}")
endforeach()

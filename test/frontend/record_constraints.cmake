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
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):" OR
               err MATCHES "requires an active expansion context")
            message(FATAL_ERROR "${name}/${level}: missing independent '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

function(check_body name expected body)
    foreach(role helper macro expander)
        set(attribute "")
        set(parameter "in $::meta::tokens input")
        if(role STREQUAL helper)
            set(invoke "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return check_record(input); } global u32 entry() { return apply!(1u32); }")
        elseif(role STREQUAL macro)
            set(attribute "[[macro]]")
            set(invoke "global u32 entry() { return check_record!(1u32); }")
        else()
            set(attribute "[[syntax_expander]]")
            set(parameter "in $::meta::syntax_match input")
            set(invoke "syntax Check : expression { prefix \"record_check\"; match \"(\" \")\"; expand check_record; } syntax Check; global u32 entry() { return record_check(); }")
        endif()
        set(source "${attribute} static $::meta::tokens check_record(${parameter}) { ${body} return $::quote { 1u32 }; }")
        check(${name}_${role}_unused "${expected}" "${source}\nglobal u32 entry() { return 1u32; }")
        check(${name}_${role}_invoked "${expected}" "${source}\n${invoke}")
    endforeach()
endfunction()

set(pending "u8 waiting[$::meta::len($::quote { a b })];")
set(alignment "[[aligned($::meta::len($::quote { a b c d }))]]")
check_body(void "incomplete or non-object type" "struct R ${alignment} { ${pending} void bad; };")
check_body(void_array "incomplete or non-object type" "struct R { ${pending} void bad[2]; };")
check_body(function "incomplete or non-object type" "typedef u32 F(in u32 value); struct R { ${pending} F bad; };")
check_body(duplicate "duplicate record member" "struct R { ${pending} u32 field; u16 field; };")
check_body(incomplete "incomplete record type" "struct Unknown; struct R { ${pending} struct Unknown bad; };")
check_body(incomplete_array "incomplete array type" "struct R { ${pending} u32 bad[]; };")
check_body(bit_base "bit-field base type" "struct R { ${pending} f32 bad : 1u32; };")
check_body(bit_atomic "bit-field cannot have atomic type" "struct R { ${pending} [[atomic]] u32 bad : 1u32; };")
check_body(meta_pointer "meta values cannot be record members" "struct R { ${pending} $::meta::tokens *bad; };")
check_body(scalable "scalable or incomplete vector type" "typedef u32 V [[scalable_vector(4)]]; struct R { ${pending} V bad; };")
check_body(cycle "member cycle" "struct R { ${pending} struct R bad[2]; };")
check_body(indirect_cycle "member cycle" "struct S; struct R { ${pending} struct S field; }; struct S { struct R field; };")
check_body(nested "incomplete or non-object type" "struct Nested { void bad; }; struct R { ${pending} struct Nested field; };")
check_body(empty "at least one member" "struct Empty {}; struct R { ${pending} struct Empty field; };")

check_body(pointer_control pass [=[
    struct Incomplete;
    struct R {
        u8 field[1u32 ? 2u32 : $::meta::len($::quote { a b })];
        struct Incomplete *forward;
        struct R *self;
        void *untyped;
        u32 (*call)(in u32 input);
    };
]=])
check_body(lexical_owner pass [=[
    u16 local = 7u16;
    struct R { u8 bytes[sizeof(local)]; };
    $::static_assert(sizeof(struct R) == 2uptr, "record lexical owner");
]=])
check_body(lexical_owner_nested pass [=[
    u16 local = 7u16;
    {
        u32 local = 9u32;
        struct Inner { u8 bytes[sizeof(local)]; };
        $::static_assert(sizeof(struct Inner) == 4uptr, "inner binding");
    }
    struct Outer { u8 bytes[sizeof(local)]; };
    $::static_assert(sizeof(struct Outer) == 2uptr, "outer binding");
]=])
check(nominal_control pass [=[
    namespace Outer { struct R { u32 value; }; }
    static $::meta::tokens helper(in $::meta::tokens input) {
        struct R { struct Outer::R by_value; struct R *self; };
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    global u32 entry() { return apply!(1u32); }
]=])

# Discarded public captures remain opaque; emitting the same declaration checks
# its structural constraints, including with projected tokens instead of nodes.
set(capture [=[
    [[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) {
        return $::quote { 1u32 };
    }
    syntax Drop : expression { prefix "drop"; match "(" value:type ")"; expand drop; }
    syntax Drop;
    global u32 entry() { return drop(struct Invalid { void field; }); }
]=])
check(discarded pass "${capture}")
foreach(form structured projected)
    set(value "$::syntax::node(input, \"value\")")
    if(form STREQUAL projected)
        set(value "$::meta::tokens(${value})")
    endif()
    check(surviving_${form} "incomplete or non-object type" "
        [[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) {
            return $::quote { sizeof($::unquote(${value})) };
        }
        syntax Emit : expression { prefix \"emit\"; match \"(\" value:type \")\"; expand emit; }
        syntax Emit;
        global uptr entry() { return emit(struct Invalid { void field; }); }")
endforeach()

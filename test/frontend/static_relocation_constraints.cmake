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
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
# Function/object pointer casts are invalid in erased source as in runtime
# code. Neither an untaken branch nor sizeof hides them. Keep object-pointer
# reinterpretation separate: meta byte backing supports typed views.
set(pointer_declarations [=[
    static u32 target(in u32 value) { return value; }
    static u32 object;
    static const u32 immutable;
    static volatile u32 observable;
    static u32 *slot;
    typedef u32 (*Callback)(in u32);
    typedef void *Opaque;
]=])
foreach(statement
        "static void *saved = (void *)&target;"
        "static Opaque saved = (Opaque)&target;"
        "static u32 *saved = (u32 *)&target;"
        "static Callback saved = (Callback)&object;"
        "sizeof((void *)&target);"
        "static void *saved = 1u32 ? (void *)&object : (void *)&target;")
    string(MD5 form "${statement}")
    foreach(role helper macro expander)
        set(attribute)
        set(parameter "$::meta::tokens")
        if(role STREQUAL macro)
            set(attribute "[[macro]]")
        elseif(role STREQUAL expander)
            set(attribute "[[syntax_expander]]")
            set(parameter "$::meta::syntax_match")
        endif()
        check(pointer_cast_${role}_${form}
            "explicit pointer conversion discards qualifiers or uses incompatible pointee types"
            "${pointer_declarations}
            ${attribute} static $::meta::tokens helper(in ${parameter} input) {
                if (0u32) { ${statement} } return $::quote { 1u32 }; }")
    endforeach()
endforeach()
check(pointer_cast_compatible pass "${pointer_declarations}
    static $::meta::tokens helper(in $::meta::tokens input) {
        if (0u32) {
            static const volatile u32 *qualified = (const volatile u32 *)&object;
            static const volatile void *erased = (const volatile void *)&immutable;
            static u32 *restored = (u32 *)(void *)&object;
            static u32 * const *nested = (u32 * const *)&slot;
            static Callback callback = (Callback)&target;
        }
        return input;
    }")
check(pointer_cast_meta_views pass [=[
    static u32 bytes(in $::meta::buffer backing) {
        u32 *word = (u32 *)$::meta::data(backing);
        *word = 37u32;
        return *word;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        $::meta::buffer backing = $::meta::alloc(4uptr);
        if (bytes(backing) != 37u32) return $::quote { invalid_meta_backing };
        return input;
    }
    global u32 entry() { return apply!(1u32); }
]=])
# A label relocation requires an addressable, non-inlined owner. Check source
# uses before erasing their enclosing helper, not only when emitting Data IR.
set(label_declarations [=[
    [[always_inline]] static void inline_owner() { point: ; }
    static void ordinary_owner() { point: ; }
]=])
foreach(statement
        "static label saved = inline_owner::point;"
        "label saved = inline_owner::point;"
        "static struct Holder { label address; } saved = { inline_owner::point };"
        "static uptr saved = (uptr)inline_owner::point;"
        "static label saved = 1u32 ? ordinary_owner::point : inline_owner::point;")
    string(MD5 form "${statement}")
    foreach(role helper macro expander)
        set(attribute)
        set(parameter "$::meta::tokens")
        if(role STREQUAL macro)
            set(attribute "[[macro]]")
        elseif(role STREQUAL expander)
            set(attribute "[[syntax_expander]]")
            set(parameter "$::meta::syntax_match")
        endif()
        check(inline_label_${role}_${form} "taking a label address conflicts with always_inline"
            "${label_declarations}
            ${attribute} static $::meta::tokens helper(in ${parameter} input) {
                if (0u32) { ${statement} } return $::quote { 1u32 }; }")
    endforeach()
endforeach()
check(inline_label_generic "taking a label address conflicts with always_inline"
    "${label_declarations}
    static $::meta::tokens helper<T>(in $::meta::tokens input) {
        if (0u32) { static label saved = inline_owner::point; } return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<u32>(input); }
    global u32 entry() { return apply!(1u32); }")
check(inline_label_layout_and_goto pass
    "${label_declarations}
    [[always_inline]] static $::meta::tokens helper(in $::meta::tokens input) {
        $::static_assert(sizeof(inline_owner::point) == sizeof(label), \"label type only\");
        if (0u32) { static label saved = ordinary_owner::point; }
        if (0u32) { goto finish; finish: ; }
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    global u32 entry() { return apply!(1u32); }")
# TLS addresses cannot become ordinary static relocations, even when a helper
# or patch-containing body is erased. Test entity lookup through a forward
# declaration and qualification as well as subobject/decay paths.
set(tls_declarations [=[
namespace ThreadState {
    u32 cell;
    [[thread_local]] global u32 cell;
    [[thread_local]] global u32 array[3];
    struct Record { u32 member; };
    [[thread_local]] global struct Record record;
}
]=])
foreach(initial "&ThreadState::cell" "ThreadState::array" "&ThreadState::array[1]"
                "&ThreadState::record.member")
    string(MAKE_C_IDENTIFIER "${initial}" case)
    foreach(use unused invoked macro expander)
        set(body "if (0u32) { static u32 *saved = ${initial}; } return input;")
        if(use STREQUAL macro)
            set(source "[[macro]] static $::meta::tokens helper(in $::meta::tokens input) { ${body} }")
        elseif(use STREQUAL expander)
            set(source "[[syntax_expander]] static $::meta::tokens helper(in $::meta::syntax_match input) {
                if (0u32) { static u32 *saved = ${initial}; } return $::quote { 1u32 }; }")
        else()
            set(source "static $::meta::tokens helper(in $::meta::tokens input) { ${body} }")
            if(use STREQUAL invoked)
                string(APPEND source "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
                    global u32 entry() { return apply!(1u32); }")
            endif()
        endif()
        check(tls_static_${case}_${use} "thread-local address is not an ordinary static relocation"
            "${tls_declarations} ${source}")
    endforeach()
    foreach(statement "static uptr saved = (uptr)(${initial});"
                      "static struct Holder { u32 *pointer; } saved = { ${initial} };"
                      "$::patch((uptr)(${initial}));")
        string(MD5 form "${statement}")
        check(tls_initial_${case}_${form} "thread-local address is not an ordinary static relocation"
            "${tls_declarations} static $::meta::tokens helper(in $::meta::tokens input) {
                if (0u32) { ${statement} } return input; }")
    endforeach()
endforeach()
check(tls_contextual_addend "thread-local address is not an ordinary static relocation"
    "${tls_declarations}
    static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
    static $::meta::tokens helper(in $::meta::tokens input) {
        if (0u32) { static uptr saved = (uptr)&ThreadState::cell + $::eval(count($::quote { one })); }
        return input;
    }")
check(tls_local_attribute "attribute 'thread_local' is not valid on a local object" [=[
    static $::meta::tokens helper(in $::meta::tokens input) {
        if (0u32) {
            [[thread_local]] static u32 cell;
            static u32 *saved = &cell;
        }
        return input;
    }
]=])
# An evaluated helper already passes through the target pointer resolver. Its
# sandbox diagnostic is distinct from the non-executing symbolic source proof.
check(tls_helper_return "address operation failed during translation-time evaluation"
    "${tls_declarations}
    static u32 *address() { return &ThreadState::cell; }
    static $::meta::tokens helper(in $::meta::tokens input) {
        if (0u32) { static u32 *saved = address(); } return input;
    }")
set(capture [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote {};
}
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Discard : item { prefix "discard_relocation"; match body:function_def; expand discard; }
syntax Keep : item { prefix "keep_relocation"; match body:function_def; expand keep; }
syntax Discard;
syntax Keep;
]=])
foreach(action discard keep)
    set(expected pass)
    if(action STREQUAL keep)
        set(expected "taking a label address conflicts with always_inline")
    endif()
    check(inline_label_capture_${action} "${expected}" "${label_declarations} ${capture}
        ${action}_relocation static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { static label saved = inline_owner::point; } return input;
        }")
endforeach()
set(accepted_tls pass)
if(MODE MATCHES "^mips")
    # A successful source proof reaches the existing target diagnostic. This
    # suite does not add MIPS runtime TLS or hide a source-validation failure.
    set(accepted_tls "MIPS TLS lowering is not implemented yet")
endif()
foreach(action discard keep)
    if(action STREQUAL discard)
        set(expected "${accepted_tls}")
    else()
        set(expected "thread-local address is not an ordinary static relocation")
    endif()
    check(tls_capture_${action} "${expected}" "${tls_declarations} ${capture}
        ${action}_relocation static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { static u32 *saved = &ThreadState::cell; } return input;
        }")
endforeach()
check(tls_unselected_and_shadowed "${accepted_tls}" "${tls_declarations}
    static u32 ordinary;
    static $::meta::tokens helper(in $::meta::tokens input) {
        if (0u32) { static u32 *saved = 1u32 ? &ordinary : &ThreadState::cell; }
        return input;
    }
    namespace ThreadState {
        static $::meta::tokens shadow(in $::meta::tokens input) {
            if (0u32) { static u32 cell; static u32 *saved = &cell; }
            return input;
        }
    }")
# An explicit uptr conversion of a code label is a relocation against the
# label, with the addends of a function address; other widths stay invalid.
check(label_uptr_relocation pass [=[
    global void label_owner() { global label resume: ; }
    static void local_owner() { point: ; }
    global uptr resume_address = (uptr)label_owner::resume;
    global uptr point_after = (uptr)local_owner::point + 4uptr;
    static struct { uptr address; u32 tag; } labelled = { (uptr)(local_owner::point), 1u32 };
    global uptr entry() { static uptr own = (uptr)here; here: return own + labelled.address; }
]=])
check(label_integer_conversion "code labels permit only same-type or explicit uptr conversions" [=[
    static void local_owner() { point: ; }
    global u64 bits = (u64)local_owner::point;
]=])
check(label_uptr_missing "unresolved name 'local_owner::missing'" [=[
    static void local_owner() { point: ; }
    global uptr bits = (uptr)local_owner::missing;
]=])

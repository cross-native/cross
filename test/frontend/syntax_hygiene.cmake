# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")
function(reject case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES ":[0-9]+:[0-9]+: error:" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: (error|note):")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endfunction()

function(reject_splice case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "(<syntax-splice>|<expansion of '[^']+'>):[0-9]+:[0-9]+: error:" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: note: token supplied from here")
        message(FATAL_ERROR "${case} was not diagnosed with splice provenance\n${out}\n${err}")
    endif()
endfunction()

function(reject_generated case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "(<syntax-splice>|<expansion of '[^']+'>|${case}.x):[0-9]+:[0-9]+: error:" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: note:" OR
       NOT err MATCHES "in expansion of syntax 'Move'" OR
       NOT err MATCHES "in expansion of procedural macro 'Definition::make'")
        message(FATAL_ERROR "${case} was not diagnosed with generated-fragment provenance\n${out}\n${err}")
    endif()
endfunction()

# A generated namespace can use declarations from its own raw fragment, but
# reopening a destination namespace does not import unrelated destination names.
foreach(level O0 O2)
    reject(fragment_namespace_macro_prefix_${level} "procedural macro is not visible: 'Child::value'" [=[
namespace Definition {
    namespace Child {
        [[macro]] static $::meta::tokens value(in $::meta::tokens input) { return $::quote { 7u32 }; }
    }
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        return $::quote {
            namespace Made {
                namespace Child {}
                static u32 read() { return Child::value!(); }
            }
        };
    }
}
namespace Destination { Definition::make!() }
global u32 entry() { return Destination::Made::read(); }
]=] -${level} -fno-eval-calls)
    reject(fragment_namespace_late_generic_${level} "must be declared before use" [=[
namespace Definition {
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        return $::quote {
            namespace Inner {
                static u32 read() { return later::<u32>(7u32); }
                [[generic(T)]] static T later(in T value) { return value; }
            }
        };
    }
}
Definition::make!()
global u32 entry() { return Inner::read(); }
]=] -${level} -fno-eval-calls)
    reject(fragment_namespace_free_value_${level} "unresolved name 'missing'" [=[
namespace Definition {
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        return $::quote { namespace Inner { static u32 read() { return missing; } } };
    }
}
namespace Destination {
    namespace Inner { static u32 missing = 99u32; }
    Definition::make!()
}
global u32 entry() { return Destination::Inner::read(); }
]=] -${level} -fno-eval-calls)
    reject(fragment_namespace_free_type_${level} "expected declaration" [=[
namespace Definition {
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        return $::quote { namespace Inner { static Missing value = 7u32; } };
    }
}
namespace Destination {
    namespace Inner { typedef u32 Missing; }
    Definition::make!()
}
]=] -${level} -fno-eval-calls)
endforeach()

# A deferred unit must not acquire a new destination binder even when that
# binder's token was copied from the same source stream as its free use.
foreach(level O0 O2)
    foreach(projection tree text)
        set(source [=[
[[macro]] static $::meta::tokens parameters(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
#if REPARSE
    body = $::meta::parse("function_def", $::meta::tokens(body), $::syntax::context(input));
#endif
    $::meta::tokens output = $::quote { $::unquote(body) };
#if PROJECT
    output = $::meta::tokens(body);
#endif
    return $::quote {
        namespace Destination {
            static u32 $::unquote($::syntax::capture(input, "name")) = 99u32;
            $::unquote(output)
        }
    };
}
syntax Move : item { prefix "move"; match name:ident body:function_def; expand move; }
syntax Move;
namespace Definition {
    [[macro]] static $::meta::tokens make(in $::meta::tokens input) {
        return $::quote {
            namespace Original {
                static u32 own = 7u32;
                move missing static u32 read(parameters!()) { return own + missing; }
            }
        };
    }
}
Definition::make!()
global u32 entry() { return Original::Destination::read(); }
]=])
        foreach(reparse 0 1)
            if(projection STREQUAL "tree")
                reject_generated(deferred_namespace_destination_${projection}_${level}_${reparse}
                    "unresolved name 'missing'" "${source}" -${level} -fno-eval-calls -DPROJECT=0 -DREPARSE=${reparse})
            else()
                reject_generated(deferred_namespace_destination_${projection}_${level}_${reparse}
                    "unresolved name 'missing'" "${source}" -${level} -fno-eval-calls -DPROJECT=1 -DREPARSE=${reparse})
            endif()
        endforeach()
    endforeach()
endforeach()

# Lifting a copied static initializer must not replace its retained label
# binding with a same-spelled label in the destination generic function.
foreach(level O0 O2)
    reject(copied_static_label_${level} "requires a visible label in a concrete function" [=[
[[generic(label Address)]] static label identity() { return Address; }
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax body = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    return $::quote {
        [[generic(T)]] static label destination() {
            $::unquote($::meta::tokens($::meta::child(body, 1uptr)))
            point: return (label)0uptr;
        }
        global label entry() { return destination::<u32>(); }
    };
}
syntax Move : item { prefix "move"; match body:function_def; expand move; }
syntax Move;
move static label original() {
    static label saved = identity::<point>();
    point: return saved;
}
]=] -${level} -fno-eval-calls)
endforeach()

# Label namespaces retain expansion marks, but copies/call_site may still
# deliberately name the same binder. Duplicates within one context are errors.
set(label_macro [=[
[[macro]] static $::meta::tokens emit(in $::meta::tokens input) {
    return $::quote { goto next; };
}
global u32 entry() { emit! {} next: return 0u32; }
]=])
set(duplicate_label_macro [=[
[[macro]] static $::meta::tokens emit(in $::meta::tokens input) {
    return $::quote { next: ; next: ; };
}
global u32 entry() { emit! {} return 0u32; }
]=])
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    reject(quoted_label_capture_${suffix} "unresolved name 'next'"
        "${label_macro}" ${flags})
    reject(duplicate_quoted_label_${suffix} "duplicate label 'next'"
        "${duplicate_label_macro}" ${flags})
    reject(foreign_label_goto_${suffix} "managed goto target must belong to the current function"
        "global void owner() { point: ; } global void entry() { here: goto owner::point; }" ${flags})
    foreach(use "global label target = owner::point;"
                "global label targets[1] = { owner::point };"
                "global label address() { return owner::point; }")
        string(MD5 use_id "${use}")
        reject(inline_label_address_${suffix}_${use_id}
            "taking a label address conflicts with always_inline"
            "[[always_inline]] static void owner() { point: ; } ${use}" ${flags})
    endforeach()
    reject(raw_parenthesized_nonlabel_${suffix} "goto target does not name a visible label or label-valued expression" [=[
[[naked]] static void owner(in u64 point "r9") {
    goto (point);
point: $::_ret();
}
]=] ${flags})
    reject(raw_foreign_quoted_owner_${suffix} "unknown same-function raw label" [=[
namespace Definition {
    [[naked]] static void owner() { point: $::_ret(); }
    [[macro]] static $::meta::tokens branch(in $::meta::tokens input) {
        return $::quote { $::_jmp(owner::point); };
    }
}
namespace Invocation {
    [[naked]] static void owner() { Definition::branch! {} point: $::_ret(); }
}
]=] ${flags})
endforeach()

set(moved_label_prefix [=[
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    $::meta::syntax jump = $::meta::child(block, 1uptr);
]=])
set(moved_label_suffix [=[
}
syntax Move : item { prefix "move"; match body:function_def; expand move; }
syntax Move;
move static u32 original() { goto point; point: return 7u32; }
]=])
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    reject_splice(moved_label_foreign_scope_${suffix} "goto target does not name a visible label"
        "${moved_label_prefix}
        return $::quote { global u32 moved() {
            $::unquote(jump)
            $::unquote($::meta::call_site($::meta::parse(\"point\"))): return 99u32;
        }}; ${moved_label_suffix}" ${flags})
    # Reusing the reference token as a new declaration preserves source scope,
    # but it must not replace the original declaration's distinct identity.
    reject_splice(moved_label_rebound_token_${suffix} "goto target does not name a visible label"
        "${moved_label_prefix}
        $::meta::tokens name = $::meta::slice($::meta::tokens(jump), 1uptr, 1uptr);
        return $::quote { global u32 moved() {
            $::unquote(jump) $::unquote(name): return 99u32;
        }}; ${moved_label_suffix}" ${flags})
    reject_splice(moved_label_object_fallback_${suffix} "goto target does not name a visible label"
        "global label point = moved::other;
        ${moved_label_prefix}
        return $::quote { global u32 moved() {
            $::unquote(jump)
            $::unquote($::meta::call_site($::meta::parse(\"other\"))): return 99u32;
        }}; ${moved_label_suffix}" ${flags})
    reject_splice(moved_raw_label_object_fallback_${suffix}
        "goto target does not name a visible label in its retained source binding" [=[
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    $::meta::syntax jump = $::meta::child(block, 1uptr);
    $::meta::tokens parameters = $::meta::slice($::meta::tokens(source), 4uptr, 1uptr);
    return $::quote { [[naked]] static void moved $::unquote(parameters) {
        $::unquote(jump) other: $::_ret();
    }};
}
syntax Move : item { prefix "move"; match body:function_def; expand move; }
syntax Move;
move [[naked]] static void original(in label point "r9") { goto point; point: $::_ret(); }
]=] ${flags})
endforeach()

set(qualified_copied_label [=[
[[generic(label Address), noinline]] static label identity() { return Address; }
global void owner() { @TARGET_LABEL@ point: ; }
@TARGET_DECLARATION@
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    $::meta::tokens name = $::meta::slice($::meta::tokens($::meta::child(block, 1uptr)), @TOKEN_OFFSET@, 1uptr);
    return $::quote { @USE@ };
}
syntax Move : item { prefix "move"; match body:function_def; expand move; }
syntax Move;
move global void original() { @SOURCE_LABEL@ point: ; }
]=])
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    foreach(linkage local global)
        set(source "${qualified_copied_label}")
        if(linkage STREQUAL global)
            string(REPLACE "@TARGET_LABEL@" "global label" source "${source}")
            string(REPLACE "@TARGET_DECLARATION@" "global label owner::point;" source "${source}")
            string(REPLACE "@SOURCE_LABEL@" "global label" source "${source}")
            string(REPLACE "@TOKEN_OFFSET@" "2uptr" source "${source}")
        else()
            string(REPLACE "@TARGET_LABEL@" "" source "${source}")
            string(REPLACE "@TARGET_DECLARATION@" "" source "${source}")
            string(REPLACE "@SOURCE_LABEL@" "" source "${source}")
            string(REPLACE "@TOKEN_OFFSET@" "0uptr" source "${source}")
        endif()
        foreach(use "global label selected() { return owner::$::unquote(name); }"
                    "global label selected = owner::$::unquote(name);"
                    "global label selected() { return identity::<owner::$::unquote(name)>(); }")
            string(MD5 use_id "${use}")
            string(REPLACE "@USE@" "${use}" test_source "${source}")
            if(use MATCHES "identity")
                set(expected "generic label argument requires a visible label in a concrete function")
            else()
                set(expected "label address does not name a visible label in its retained source binding")
            endif()
            reject(qualified_copied_label_${linkage}_${suffix}_${use_id}
                "${expected}" "${test_source}" ${flags})
        endforeach()
    endforeach()
endforeach()

set(nested_label_prefix [=[
[[syntax_expander]] static $::meta::tokens hold(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Hold : statement { prefix "hold"; match body:stmt; expand hold; }
syntax Hold;
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    $::meta::syntax statement = $::meta::child(block, 1uptr);
    $::meta::syntax extension = $::meta::child($::meta::child(statement, 0uptr), 0uptr);
    $::meta::syntax jump = $::syntax::node($::meta::extension_match(extension), "body");
]=])
set(nested_label_suffix [=[
}
syntax Move : item { prefix "move"; match body:function_def; expand move; }
syntax Move;
move static u32 original() { hold goto point; point: return 7u32; }
]=])
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    reject_splice(nested_label_rebound_token_${suffix} "goto target does not name a visible label"
        "${nested_label_prefix}
        $::meta::tokens name = $::meta::slice($::meta::tokens(jump), 1uptr, 1uptr);
        return $::quote { global u32 moved() {
            $::unquote(jump) $::unquote(name): return 99u32;
        }}; ${nested_label_suffix}" ${flags})
    reject_splice(nested_label_object_fallback_${suffix} "goto target does not name a visible label"
        "global label point = moved::other;
        ${nested_label_prefix}
        return $::quote { global u32 moved() {
            $::unquote(jump)
            $::unquote($::meta::call_site($::meta::parse(\"other\"))): return 99u32;
        }}; ${nested_label_suffix}" ${flags})
    reject_splice(raw_nested_label_scope_${suffix} "goto target does not name a visible label"
        "[[macro]] static $::meta::tokens hold(in $::meta::tokens input) { return input; }
        ${moved_label_prefix}
        return $::quote { global u32 moved() {
            $::unquote(jump)
            $::unquote($::meta::call_site($::meta::parse(\"point\"))): return 99u32;
        }};
        }
        syntax Move : item { prefix \"move\"; match body:function_def; expand move; }
        syntax Move;
        move static u32 original() { hold! { goto point; } point: return 7u32; }" ${flags})
    reject_splice(nested_raw_label_object_fallback_${suffix}
        "goto target does not name a visible label in its retained source binding"
        "${nested_label_prefix}
        $::meta::tokens parameters = $::meta::slice($::meta::tokens(source), 4uptr, 1uptr);
        return $::quote { [[naked]] static void moved $::unquote(parameters) {
            $::unquote(jump) other: $::_ret();
        }};
        }
        syntax Move : item { prefix \"move\"; match body:function_def; expand move; }
        syntax Move;
        move [[naked]] static void original(in label point \"r9\") { hold goto point; point: $::_ret(); }"
        ${flags})
endforeach()

string(REPLACE "child(block, 1uptr)" "child(block, 2uptr)"
    deferred_label_prefix "${nested_label_prefix}")
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    set(deferred_label_rebinding "[[macro]] static $::meta::tokens introduce(in $::meta::tokens input) {
            return $::quote { typedef u32 $::unquote(input); };
        }
        ${deferred_label_prefix}
        if (!$::meta::is_kind(jump, \"deferred\"))
            $::syntax::error($::syntax::span(input), \"expected a deferred goto\");
        $::meta::tokens name = $::meta::slice($::meta::tokens(jump), 1uptr, 1uptr);
        return $::quote { global u32 moved() {
            $::unquote(jump) $::unquote(name): return 99u32;
        }};
        }
        syntax Move : item { prefix \"move\"; match body:function_def; expand move; }
        syntax Move;
        move static u32 original() { introduce!(Word) hold goto point; point: return 7u32; }")
    reject_splice(deferred_label_rebound_token_${suffix} "goto target does not name a visible label"
        "${deferred_label_rebinding}" ${flags})
    string(REPLACE "global u32 moved()" "static u32 moved<T>()"
        generic_label_rebinding "${deferred_label_rebinding}")
    string(REPLACE "static u32 original()" "static u32 original<T>()"
        generic_label_rebinding "${generic_label_rebinding}")
    set(generic_label_rebinding "${generic_label_rebinding}
        global u32 entry() { return moved::<u16>() + moved::<u32>(); }")
    reject_splice(generic_deferred_label_rebound_${suffix} "goto target does not name a visible label"
        "${generic_label_rebinding}" ${flags})
    string(REPLACE "$::unquote(jump)" "$::unquote($::meta::tokens(jump))"
        projected_label_rebinding "${generic_label_rebinding}")
    reject(generic_projected_label_rebound_${suffix} "goto target does not name a visible label"
        "${projected_label_rebinding}" ${flags})
endforeach()

set(moved_generic_prefix [=[
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax body = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    return $::quote { [[generic(T), noinline]] static uptr moved() $::unquote(body) };
}
syntax Move : item { prefix "move"; match body:function_def; expand move; }
syntax Move;
]=])
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    foreach(body "T value = 0; return sizeof(value);"
                 "typedef T (*Callback)(in T); return sizeof(Callback);"
                 "return sizeof(T);")
        string(MD5 body_id "${body}")
        reject_splice(generic_binder_${suffix}_${body_id}
            "captured generic type parameter 'T' is not visible in this instantiation"
            "${moved_generic_prefix}
            move [[generic(T)]] static uptr original() { ${body} }
            global uptr entry() { return moved::<u8>(); }" ${flags})
        string(REPLACE "$::unquote(body)" "$::unquote($::meta::tokens(body))"
            projected_generic_prefix "${moved_generic_prefix}")
        reject(generic_projected_binder_${suffix}_${body_id}
            "captured generic type parameter 'T' is not visible in this instantiation"
            "${projected_generic_prefix}
            move [[generic(T)]] static uptr original() { ${body} }
            global uptr entry() { return moved::<u8>(); }" ${flags})
    endforeach()
endforeach()

set(raw_value_prefix [=[
[[macro]] static $::meta::tokens hold(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens introduce(in $::meta::tokens input) {
    return $::quote { typedef u32 $::unquote(input); };
}
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax body = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    return $::quote {
        global u32 moved(in u32 $::unquote($::syntax::capture(input, "name")))
        $::unquote($::meta::tokens(body))
    };
}
syntax Move : item { prefix "move"; match name:ident body:function_def; expand move; }
syntax Move;
]=])
string(REPLACE "global u32 moved(in u32 $::unquote($::syntax::capture(input, \"name\")))"
    "[[generic(u32 $::unquote($::syntax::capture(input, \"name\")))]] static u32 moved()"
    raw_generic_prefix "${raw_value_prefix}")
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    foreach(body "return hold!(value);"
                 "introduce!(Word) Word copy = hold!(value); return copy;")
        string(MD5 body_id "${body}")
        reject(raw_parameter_rebound_${suffix}_${body_id}
            "captured local value 'value' is not visible"
            "${raw_value_prefix}
            move value static u32 original(in u32 value) { ${body} }" ${flags})
        reject(raw_generic_rebound_${suffix}_${body_id}
            "captured local value 'value' is not visible"
            "${raw_generic_prefix}
            move value [[generic(u32 value)]] static u32 original() { ${body} }
            global u32 entry() { return moved::<7u32>(); }" ${flags})
    endforeach()
endforeach()

# A deferred function's discarded header must not acquire parameters from a
# different header, even if both construct the same call-site identifier.
set(foreign_deferred_header [=[
[[macro]] static $::meta::tokens hold(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens parameter(in $::meta::tokens input) {
    $::meta::tokens name = $::meta::call_site($::meta::parse("value"));
    return $::quote { in u32 $::unquote(name) };
}
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::tokens source = $::meta::tokens($::syntax::node(input, "body"));
    $::meta::tokens body = $::meta::slice(source, 4uptr, 1uptr);
    return $::quote { static u32 moved(parameter!()) $::unquote(body) };
}
syntax Move : item { prefix "move"; match body:function_def; expand move; }
syntax Move;
move static u32 original(parameter!()) { return hold!(value); }
$::static_assert(moved(7u32) == 7u32, "must not bind to a foreign header");
]=])
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    reject(foreign_deferred_header_${suffix} "unresolved name 'value'"
        "${foreign_deferred_header}" ${flags})
endforeach()

set(generic_label_context [=[
[[generic(label Address), noinline]] static label identity() { return Address; }
namespace Definition {
    @DEFINITION@
    [[macro]] static $::meta::tokens address(in $::meta::tokens input) {
        return $::quote { identity::<owner::point>() };
    }
}
namespace Invocation {
    global void owner() { global label point: ; }
    global label selected() { return Definition::address!(); }
}
]=])
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    foreach(definition "" "global u32 owner = 0u32;")
        string(MD5 case_id "${definition}")
        string(REPLACE "@DEFINITION@" "${definition}" source "${generic_label_context}")
        reject(generic_label_context_${suffix}_${case_id}
            "generic label argument does not name a visible function label" "${source}" ${flags})
    endforeach()
    string(REPLACE "@DEFINITION@" "global void owner() { other: ; }" source "${generic_label_context}")
    reject(generic_local_label_context_${suffix}
        "generic label argument requires a visible label in a concrete function" "${source}" ${flags})
    set(identity "[[generic(label Address), noinline]] static label identity() { return Address; }")
    reject(generic_label_runtime_parameter_${suffix} "ordinary value name cannot select a same-spelled label"
        "${identity} global label owner(in label point) { global label point: return identity::<point>(); }" ${flags})
    reject(generic_label_runtime_local_${suffix} "ordinary value name cannot select a same-spelled label"
        "${identity} global label owner() { label point = owner::point; global label point: return identity::<point>(); }" ${flags})
    reject(generic_label_runtime_global_${suffix} "ordinary value name cannot select a same-spelled label"
        "${identity} global label point; global label owner() { global label point: return identity::<point>(); }" ${flags})
    reject(generic_inline_label_address_${suffix} "taking a label address conflicts with always_inline"
        "${identity} [[always_inline]] static void owner() { point: ; } global label selected() { return identity::<owner::point>(); }" ${flags})
    reject(generic_private_label_hidden_${suffix} "generic label argument requires a visible label in a concrete function"
        "${identity} [[macro]] static $::meta::tokens define(in $::meta::tokens input) { return $::quote { point: ; }; }
        global void owner() { define!() } global label selected() { return identity::<owner::point>(); }" ${flags})
endforeach()

set(surround [=[
[[syntax_expander]] static $::meta::tokens surround(in $::meta::syntax_match input) {
    return $::quote {
        { u32 $::unquote($::syntax::capture(input, "name")) = 99u32;
          $::unquote($::meta::tokens($::syntax::node(input, "body"))) }
    };
}
syntax Surround : statement { prefix "surround"; match name:ident "," body:stmt; expand surround; }
]=])
reject(const_relocated "cannot (write|assign|modify).*const|read.only"
    "${surround} global u32 entry() { syntax Surround; const u32 value = 7u32; surround value, value += 1u32; return value; }")
reject(duplicate_local "declared more than once"
    "global u32 entry() { u32 value = 1u32; u32 value = 2u32; return value; }")
reject(duplicate_parameter "duplicate parameter name"
    "global u32 entry(in u32 value, in u32 value) { return value; }")
reject(parameter_local_conflict "declared more than once"
    "global u32 entry(in u32 value) { u32 value = 2u32; return value; }")

set(extract [=[
[[syntax_expander]] static $::meta::tokens extract(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax compound = $::meta::child($::meta::child(body, 0uptr), 0uptr);
    return $::meta::tokens($::meta::child(compound, 2uptr));
}
syntax Extract : statement { prefix "extract"; match body:stmt; expand extract; }
]=])
# Removing a parsed declaration leaves its use unbound. It must not fall back
# to an unrelated same-spelled global that remains available in this program.
foreach(flags "-O0" "-O2;-fno-eval-calls")
    string(REPLACE ";" "_" suffix "${flags}")
    reject(removed_binder_${suffix} "captured local value .* is not visible"
        "${extract} global u32 value = 23u32; global u32 entry() { syntax Extract; extract { u32 value = 7u32; return value; } }"
        ${flags})
endforeach()

# Runtime-only source is not an evaluator allocation merely because its label
# table could be retained by a subsequent capture.
set(ordinary_labels "global u32 entry() { goto point39; ")
foreach(index RANGE 0 38)
    string(APPEND ordinary_labels "point${index}: ; ")
endforeach()
string(APPEND ordinary_labels "point39: return 0u32; }")
file(WRITE "${OUTPUT}/ordinary-label-budget.x" "${ordinary_labels}")
execute_process(COMMAND "${CC}" -S -fno-eval-calls
    -feval-byte-limit=4096 -feval-memory-limit=4096
    "${OUTPUT}/ordinary-label-budget.x" -o "${OUTPUT}/ordinary-label-budget.s"
    RESULT_VARIABLE ordinary_status OUTPUT_VARIABLE ordinary_out ERROR_VARIABLE ordinary_err)
if(NOT ordinary_status EQUAL 0)
    message(FATAL_ERROR "ordinary labels were charged as evaluator storage\n${ordinary_out}\n${ordinary_err}")
endif()

# Debug serialization must preserve the same private label identities as the
# native backends, including static and cross-function address references.
if(WIN32)
    set(host_abi ms_abi)
else()
    set(host_abi sysv_abi)
endif()
foreach(level O0 O2)
    set(ir "${OUTPUT}/label-hygiene-${level}.ll")
    execute_process(COMMAND "${CC}" -emit-llvm -${level} -fno-eval-calls
        "-mabi=${host_abi}" "${CMAKE_CURRENT_LIST_DIR}/syntax_hygiene.x" -o "${ir}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "label hygiene LLVM serialization failed\n${out}\n${err}")
    endif()
    file(READ "${ir}" text)
    string(REGEX MATCHALL "cross[.]label[.][0-9]+:" definitions "${text}")
    list(LENGTH definitions count)
    list(REMOVE_DUPLICATES definitions)
    list(LENGTH definitions unique_count)
    if(count LESS 10 OR NOT count EQUAL unique_count)
        message(FATAL_ERROR "LLVM label definitions lost private identity")
    endif()
    string(REGEX MATCHALL "%cross[.]label[.][0-9]+" references "${text}")
    foreach(reference IN LISTS references)
        string(SUBSTRING "${reference}" 1 -1 name)
        list(FIND definitions "${name}:" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "LLVM label reference ${reference} has no definition")
        endif()
    endforeach()
endforeach()

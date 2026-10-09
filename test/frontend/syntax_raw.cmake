# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
set(directory "${OUTPUT}.cases")
file(MAKE_DIRECTORY "${directory}")

get_filename_component(test_directory "${SOURCE}" DIRECTORY)

# Target-width and erasure checks are separately registered by target.

set(expander "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { 1u32 }; }\n")
set(definition "syntax Value : expression { prefix \"value\"; match body:paren; expand expand; }\n")
function(reject case expected source)
    set(input "${directory}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${directory}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    set(location_pattern "${case}.x:[0-9]+:[0-9]+: (error|note):")
    if(case MATCHES "^deep_(expression(_generic)?|statement|item|type)_limit$")
        # The deepest prefix is copied into a generated replacement. Its error
        # belongs to that precise token; the ancestry must still identify the
        # original invocation and definition rather than fabricate its location.
        set(location_pattern "${case}.x:[0-9]+:[0-9]+: note: token supplied from here")
        if(case MATCHES "^deep_expression")
            set(depth_syntax "DeepExpressionExpansion::Deep")
        elseif(case STREQUAL "deep_item_limit")
            set(depth_syntax "Deep")
        elseif(case STREQUAL "deep_type_limit")
            set(depth_syntax "DeepTypeExpansion::Bound")
        else()
            set(depth_syntax "DeepStatementExpansion::Deep")
        endif()
        if(NOT err MATCHES "syntax '${depth_syntax}' defined here" OR
           NOT err MATCHES "syntax expander defined here")
            message(FATAL_ERROR "${case} lost its original invocation/definition ancestry\n${out}\n${err}")
        endif()
    endif()
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES ":[0-9]+:[0-9]+: error:" OR
       NOT err MATCHES "${location_pattern}")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
    if(case MATCHES "^resource_cursor_")
        string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" resource_cursor_errors "${err}")
        list(LENGTH resource_cursor_errors resource_cursor_error_count)
        set(resource_cursor_expected 1)
        if(case STREQUAL "resource_cursor_prior_error")
            set(resource_cursor_expected 2)
            if(NOT err MATCHES "resource_cursor_prior_error.x:1:1: error: expected declaration")
                message(FATAL_ERROR "${case} erased an earlier independent source error\n${out}\n${err}")
            endif()
        endif()
        if(NOT resource_cursor_error_count EQUAL resource_cursor_expected OR
           NOT err MATCHES "syntax 'Deep' defined here" OR
           NOT err MATCHES "syntax expander defined here")
            message(FATAL_ERROR "${case} cascaded after exhaustion or lost expansion ancestry\n${out}\n${err}")
        endif()
    endif()
    if(case MATCHES "^nested_match_" AND
       (err MATCHES "resource owner executed" OR NOT err MATCHES "syntax 'Inner' defined here" OR
        NOT err MATCHES "syntax 'Value' defined here"))
        message(FATAL_ERROR "${case} lost nested resource ancestry or executed its owner\n${out}\n${err}")
    endif()
    if(case STREQUAL "deep_recognition_limit")
        string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" recognition_errors "${err}")
        list(LENGTH recognition_errors recognition_error_count)
        if(NOT recognition_error_count EQUAL 1 OR
           err MATCHES "discarded nested capture executed")
            message(FATAL_ERROR "${case} lost resource opacity or cascaded\n${out}\n${err}")
        endif()
    endif()
endfunction()

function(accept case source)
    set(input "${directory}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${directory}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${case} failed\n${out}\n${err}")
    endif()
endfunction()

reject(vector_type_duplicate "at most one vector type attribute"
    "global u32 entry() { return (u32)sizeof(u32 [[vector_size(16), ext_vector_type(4)]]); }")
reject(vector_type_pointer "vector element type must be"
    "global u32 entry() { return (u32)sizeof(u32 * [[vector_size(16)]]); }")
reject(vector_type_bad_size "vector size must be a multiple"
    "global u32 entry() { return (u32)sizeof(u32 [[vector_size(10)]]); }")
reject(vector_type_bad_count "requires one positive integer argument"
    "global u32 entry() { return (u32)sizeof(u32 [[ext_vector_type(0)]]); }")
foreach(argument IN ITEMS "0u32" "-1i32" "2u32 - 2u32" "(uptr)0u32")
    string(MD5 key "${argument}")
    reject(vector_bound_${key} "positive integer argument"
        "typedef u32 Unused [[ext_vector_type(${argument})]]; global u32 entry() { return 1u32; }")
endforeach()
reject(vector_bound_fraction "multiple of the element size"
    "typedef u32 Unused [[vector_size(3u32)]]; global u32 entry() { return 1u32; }")
reject(vector_bound_noninteger "required expression is not.*integer|integer.*required"
    "typedef u32 Unused [[ext_vector_type(4.0f64)]]; global u32 entry() { return 1u32; }")
reject(vector_bound_too_large "vector lane count is out of range"
    "typedef u32 Unused [[ext_vector_type(0x100000000u64)]]; global u32 entry() { return 1u32; }")
reject(vector_bound_runtime "runtime_only|runtime-only"
    "[[runtime_only]] static uptr count() { return 4uptr; } typedef u32 Unused [[ext_vector_type(count())]]; global u32 entry() { return 1u32; }")
reject(vector_bound_unused_local "positive integer argument"
    "static $::meta::tokens helper(in $::meta::tokens input) { typedef u32 Unused [[ext_vector_type(0u32)]]; return input; } global u32 entry() { return 1u32; }")
reject(vector_bound_unused_generic "positive integer argument"
    "static u32 helper<uptr N>() { typedef u32 Unused [[ext_vector_type(N)]]; return 1u32; } global u32 entry() { return helper<0uptr>(); }")
reject(vector_bound_alias_redeclaration "redeclared with a different type"
    "typedef u32 Vector [[ext_vector_type(2uptr + 2uptr)]]; typedef u32 Vector [[ext_vector_type(4uptr + 4uptr)]]; global u32 entry() { return 1u32; }")
reject(vector_bound_generic_alias_redeclaration "redeclared with a different type"
    "static u32 helper<uptr N>() { typedef u32 Vector [[ext_vector_type(N)]]; typedef u32 Vector [[ext_vector_type(4)]]; return 1u32; } global u32 entry() { return helper<8uptr>(); }")
foreach(type IN ITEMS "bool" "u32 *" "u32 [[atomic]]")
    string(MD5 key "${type}")
    reject(vector_element_generic_${key} "vector element type must be"
        "static u32 helper<T>() { typedef T Unused [[ext_vector_type(4)]]; return 1u32; } global u32 entry() { return helper<${type}>(); }")
endforeach()
reject(vector_const_generic "cannot.*const|const.*assign"
    "static u32 helper<T>() { T [[ext_vector_type(4)]] value = 1u32; value[0uptr] = 2u32; return 1u32; } global u32 entry() { return helper<const u32>(); }")
reject(vector_type_const "cannot.*const|const.*assign"
    "global u32 entry() { const u32 [[ext_vector_type(4)]] value = 3u32; value = 7u32; return value[0uptr]; }")
reject(vector_typedef_const "cannot.*const|const.*assign"
    "typedef const u32 Value [[ext_vector_type(4)]]; global u32 entry() { Value value = 3u32; value = 7u32; return value[0uptr]; }")

accept(expander_combined_attributes
    "[[eval_only, syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { 7u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
accept(expander_separated_attributes
    "static [[eval_only]] [[syntax_expander]] $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { 7u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
accept(expander_standalone_attributes
    "[[eval_only, syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { 7u32 }; }\n")
accept(expander_after_return_type
    "static $::meta::tokens [[syntax_expander, eval_only]] expand(in $::meta::syntax_match input) { return $::quote { 7u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
accept(expander_after_declarator
    "static $::meta::tokens expand(in $::meta::syntax_match input) [[eval_only, syntax_expander]] { return $::quote { 7u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
accept(expander_standalone_after_declarator
    "static $::meta::tokens expand(in $::meta::syntax_match input) [[syntax_expander]] { return $::quote { 7u32 }; }\n")
file(READ "${directory}/expander_standalone_attributes.s" standalone_assembly)
if(standalone_assembly MATCHES "expand:")
    message(FATAL_ERROR "standalone syntax expander escaped to runtime assembly")
endif()
file(READ "${directory}/expander_standalone_after_declarator.s" standalone_assembly)
if(standalone_assembly MATCHES "expand:")
    message(FATAL_ERROR "post-declarator syntax expander escaped to runtime assembly")
endif()
accept(macro_combined_attributes
    "[[macro, eval_only]] static $::meta::tokens answer(in $::meta::tokens input) { return $::quote { 7u32 }; } global u32 entry() { return answer!{}; }\n")
accept(macro_separated_attributes
    "[[eval_only]] static [[macro]] $::meta::tokens answer(in $::meta::tokens input) { return $::quote { 7u32 }; } global u32 entry() { return answer!{}; }\n")
accept(macro_after_return_type
    "static $::meta::tokens [[macro, eval_only]] answer(in $::meta::tokens input) { return $::quote { 7u32 }; } global u32 entry() { return answer!{}; }\n")
accept(macro_after_declarator
    "static $::meta::tokens answer(in $::meta::tokens input) [[macro, eval_only]] { return $::quote { 7u32 }; } global u32 entry() { return answer!{}; }\n")
reject(expander_conflicting_attribute "cannot be both eval_only and runtime_only"
    "[[runtime_only, syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote {}; }\n")
reject(expander_result_location "expansion functions cannot specify a result location"
    "static $::meta::tokens expand(in $::meta::syntax_match input) -> \"rax\" [[syntax_expander]] { return $::quote {}; }\n")
reject(macro_duplicate_role "more than one role attribute"
    "[[macro]] [[macro]] static $::meta::tokens answer(in $::meta::tokens input) { return $::quote {}; }\n")

reject(reserved "ordinary nonreserved identifier"
    "${expander}syntax Bad : expression { prefix \"if\"; match body:paren; expand expand; }\n")
reject(terminal "exactly one existing token"
    "${expander}syntax Bad : expression { prefix \"two words\"; match body:paren; expand expand; }\n")
reject(balance "balanced|balance delimiters"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" field:ident; expand expand; }\n")
foreach(combinator IN ITEMS separated0 separated1)
    foreach(separator IN ITEMS "(" ")" "[" "]" "[[" "]]" "{" "}")
        string(MD5 key "${combinator}${separator}")
        # Locally invalid even when the rule is unused. Every selected body
        # balances independently, so its one-token separator must do so too.
        reject(separator_balance_${key} "syntax separator cannot be a group delimiter"
            "syntax Bad : rule { match parts:${combinator}(\"x\", \"${separator}\"); }\n")
    endforeach()
endforeach()
foreach(separator IN ITEMS "," ";" ":" "|" "->" "then" "7u32" "'('")
    string(MD5 key "${separator}")
    accept(separator_balanced_${key}
        "${expander}syntax Value : expression { prefix \"value\"; match \"(\" parts:separated1(\"x\", \"${separator}\") \")\"; expand expand; } syntax Value; $::static_assert(value (x ${separator} x) == 1u32, \"balanced separator\");\n")
endforeach()
reject(fields "duplicate syntax capture field"
    "${expander}syntax Bad : expression { prefix \"bad\"; match a:ident a:ident; expand expand; }\n")
reject(clause "requires prefix, match, and expand in order"
    "${expander}syntax Bad : expression { match body:paren; prefix \"bad\"; expand expand; }\n")
reject(parsed_expression_malformed "syntax-match error for active prefix"
    "${expander}syntax Bad : expression { prefix \"bad\"; match value:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32 + ; }\n")
reject(parsed_expression_infix "requires a semicolon, comma, or closing-delimiter fence"
    "${expander}syntax Bad : expression { prefix \"bad\"; match left:expr \"+\" right:expr; expand expand; }\n")
reject(node_on_primitive "requires a parsed or raw-group capture field"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax node = $::syntax::node(input, \"body\"); return $::meta::tokens(node); } syntax Bad : expression { prefix \"bad\"; match body:literal; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(raw_group_count "syntax count/at requires a nested record field"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { uptr count = $::syntax::count(input, \"body\"); return $::quote { 1u32 }; } syntax Bad : expression { prefix \"bad\"; match body:group; expand expand; } syntax Bad; global u32 entry() { return bad (); }\n")
reject(raw_group_storage "syntax match record byte or memory budget exceeded"
    "${expander}${definition}syntax Value; global u32 entry() { return value (word (nested) [other] {raw}); }\n"
    -feval-memory-limit=4096)
reject(raw_group_depth "syntax raw-group nesting depth exceeded"
    "${expander}${definition}syntax Value; global u32 entry() { return value ((((())))); }\n"
    -feval-depth-limit=4)
reject(parsed_statement_incomplete "syntax-match error for active prefix"
    "${expander}syntax Bad : statement { prefix \"bad\"; match body:stmt; expand expand; } syntax Bad; global u32 entry() { bad if (1u32) }\n")
reject(parsed_for_initializer "syntax-match error for active prefix"
    "${expander}syntax Bad : statement { prefix \"bad\"; match body:stmt; expand expand; } syntax Bad; global u32 entry() { bad for (u32 at = ; at < 2u32; ++at) {} return 0u32; }\n")
reject(local_typedef_escape "expected declaration|expected.*type|unknown type"
    "global u32 entry() { { typedef u32 Local; Local inside = 1u32; } Local outside = 2u32; return outside; }\n")
reject(local_typedef_value_collision "conflicts with a typedef"
    "global u32 entry() { typedef u32 Word; u32 Word = 1u32; return Word; }\n")
reject(local_value_typedef_collision "conflicts with a local value"
    "global u32 entry() { u32 Word = 1u32; typedef u32 Word; return Word; }\n")
reject(local_typedef_incompatible "redeclared with a different type"
    "global u32 entry() { typedef u32 Word; typedef u16 Word; return 0u32; }\n")
reject(local_typedef_qualified "local typedef name must be unqualified"
    "global u32 entry() { typedef u32 outer::Word; return 0u32; }\n")
reject(local_declarator_trailing_comma "expected.*declarator|expected local variable name"
    "global u32 entry() { u32 first = 1u32,; return first; }\n")
reject(local_typedef_trailing_comma "expected typedef name|expected.*declarator"
    "global u32 entry() { typedef u32 Word,; return 0u32; }\n")
reject(local_declarator_const "cannot write a const cell"
    "[[eval_only]] static u32 evaluate() { const u32 first = 1u32, second = 2u32; second = 3u32; return first + second; } global u32 value = evaluate();\n")
reject(local_storage_conflict "more than one storage specifier"
    "global u32 entry() { u32 register stack value = 1u32; return value; }\n")
reject(local_typedef_storage_conflict "typedef cannot combine with another storage specifier"
    "global u32 entry() { u32 static typedef Word; return 0u32; }\n")
reject(file_typedef_storage_conflict "typedef cannot combine with another storage specifier"
    "u32 typedef global Word;\n")
reject(reordered_const_typedef "cannot write a const cell"
    "[[eval_only]] static u32 evaluate() { const typedef u32 Word; Word value = 1u32; value = 2u32; return value; } global u32 result = evaluate();\n")
reject(interleaved_typedef_attribute "not valid on a typedef"
    "u32 [[packed]] typedef Wrong;\n")
reject(vector_size_overflow "vector size is out of range"
    "typedef u8 Huge [[vector_size(18446744073709551615)]];\n")
reject(inline_enum_underlying_conflict "redeclared with a different underlying type"
    "enum Kind [[underlying(u16)]] { first = 1u16 }; enum Kind [[underlying(u32)]] { second = 2u32 } value;\n")
reject(inline_record_duplicate "duplicate definition of record"
    "struct Cell { u32 value; }; struct Cell { u32 value; } object;\n")
reject(inline_record_kind_conflict "previously declared with the other record kind"
    "struct Cell { u32 value; }; union Cell { u32 value; } object;\n")
reject(enum_use_attributes "enumeration attributes on a type use are not yet supported"
    "enum Kind [[underlying(u16)]] { first = 1u16 }; enum Kind [[underlying(u16)]] value;\n")
reject(record_use_attributes "record attributes on a type use are not yet supported"
    "struct Cell { u32 value; }; struct Cell [[packed]] object;\n")
reject(parsed_constant_assignment "syntax-match error for active prefix"
    "${expander}syntax Bad : statement { prefix \"bad\"; match body:stmt; expand expand; } syntax Bad; global u32 entry() { bad $::static_assert(1u32 = 2u32, \"not conditional\"); return 0u32; }\n")
reject(parsed_designator_assignment "syntax-match error for active prefix"
    "${expander}syntax Bad : item { prefix \"bad\"; match body:declaration; expand expand; } syntax Bad; bad global u32 values[3] = { [1u32 = 2u32] = 3u32 };\n")
reject(parsed_generic_named_type "syntax-match error for active prefix"
    "${expander}syntax Bad : item { prefix \"bad\"; match body:expr \";\"; expand expand; } syntax Bad; bad unknown::<u32 object>;\n")
reject(generic_named_type "generic type argument cannot declare an object"
    "static T identity<T>(in T value) { return value; } global u32 entry() { return identity<u32 object>(1u32); }\n")
reject(generic_attribute_unknown "unknown attribute 'generic'"
    "static T valid<T>(in T value) [[generic(T)]] { return value; }\n")
reject(generic_attribute_no_binding "expected declaration|expected.*type"
    "[[generic(T)]] static T invalid(in T value) { return value; }\n")
reject(generic_body_scope "expected declaration|expected.*type"
    "static T invalid() { u32 nested<T>(in u32 value); return 0u32; }\n")
reject(generic_parameter_scope "expected declaration|expected.*type"
    "static T invalid(in u32 (*nested<T>)(in u32 value)) { return 0u32; }\n")
reject(generic_next_scope "expected declaration|expected.*type"
    "static T invalid; static U valid<U, T>(in U value) { return value; }\n")
reject(generic_escape "expected declaration|expected.*type"
    "static T valid<T>(in T value) { return value; } T invalid;\n")
reject(parsed_declaration_definition "syntax-match error for active prefix"
    "${expander}syntax Bad : item { prefix \"bad\"; match body:declaration; expand expand; } syntax Bad; bad static u32 value() { return 1u32; }\n")
reject(parsed_definition_prototype "syntax-match error for active prefix"
    "${expander}syntax Bad : item { prefix \"bad\"; match body:function_def; expand expand; } syntax Bad; bad static u32 value();\n")
reject(parsed_prototype_definition "syntax-match error for active prefix"
    "${expander}syntax Bad : item { prefix \"bad\"; match body:function_decl; expand expand; } syntax Bad; bad static u32 value() { return 1u32; }\n")
reject(parsed_header_object "syntax-match error for active prefix"
    "${expander}syntax Bad : item { prefix \"bad\"; match header:function_header body:block; expand expand; } syntax Bad; bad static u32 value { raw; }\n")
reject(parsed_declaration_namespace "syntax-match error for active prefix"
    "${expander}syntax Bad : item { prefix \"bad\"; match body:declaration; expand expand; } syntax Bad; bad namespace named {}\n")
reject(parsed_tree_depth "public syntax tree depth, work, or storage budget exceeded"
    "${expander}syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n"
    -feval-depth-limit=8)
reject(parsed_tree_storage "syntax match record byte or memory budget exceeded|public syntax tree depth, work, or storage budget exceeded"
    "${expander}syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n"
    -feval-memory-limit=2048)
reject(parsed_tree_layout "meta values have no runtime size or alignment"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { uptr width = sizeof($::meta::syntax); return $::quote { 1u32 }; } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(parsed_child_bounds "syntax child index is out of range"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::meta::tokens($::meta::child($::syntax::node(input, \"body\"), 100uptr)); } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(replace_child_shape "syntax replacement changes the grammar production"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax body = $::syntax::node(input, \"body\"); return $::meta::tokens($::meta::replace_child(body, 0uptr, body)); } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(replace_child_type "incompatible argument type for translation-only operation"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::meta::tokens($::meta::replace_child($::syntax::node(input, \"body\"), 0uptr, 1u32)); } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(replace_child_index "syntax child index is out of range"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax body = $::syntax::node(input, \"body\"); return $::meta::tokens($::meta::replace_child(body, 99uptr, body)); } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(replace_child_work "validation work budget|translation-time instruction budget"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax body = $::syntax::node(input, \"body\"); for (uptr at = 0uptr; at < 1000uptr; ++at) body = $::meta::replace_child(body, 0uptr, $::meta::child(body, 0uptr)); return $::meta::tokens(body); } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n"
    -feval-step-limit=3000)
reject(parsed_primitive_field "syntax capture requires a primitive token field"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::syntax::capture(input, \"body\"); } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(parsed_unknown_kind "unknown public syntax node kind"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { if ($::meta::is_kind($::syntax::node(input, \"body\"), \"unknown\")) return $::quote { 1u32 }; return $::quote { 0u32 }; } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(parsed_unknown_production "unknown public syntax production"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { if ($::meta::is_production($::syntax::node(input, \"body\"), \"unknown\")) return $::quote { 1u32 }; return $::quote { 0u32 }; } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(extension_unknown_definition "syntax entity is not visible"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { if ($::meta::is_extension($::syntax::node(input, \"body\"), \"Missing\")) return $::quote { 1u32 }; return $::quote { 0u32 }; } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(extension_invalid_definition "expected a qualified syntax-name string"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { if ($::meta::is_extension($::syntax::node(input, \"body\"), \"Bad:::Name\")) return $::quote { 1u32 }; return $::quote { 0u32 }; } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(extension_definition_type "incompatible argument type for translation-only operation"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { if ($::meta::is_extension($::syntax::node(input, \"body\"), 1u32)) return $::quote { 1u32 }; return $::quote { 0u32 }; } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(extension_match_core "extension_match requires an extension node"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax_match nested = $::meta::extension_match($::syntax::node(input, \"body\")); return $::syntax::input(nested); } syntax Bad : expression { prefix \"bad\"; match body:expr; expand expand; } syntax Bad; global u32 entry() { return bad 1u32; }\n")
reject(fence "followed immediately by terminal ';'"
    "${expander}syntax Bad : expression { prefix \"bad\"; match value:tokens_until(\";\"); expand expand; }\n")
reject(match "syntax-match error for active prefix"
    "${expander}${definition}syntax Value; global u32 entry() { u32 value = 1u32; return value + 1u32; }\n")
reject(function_raw_requires_definition "syntax-match error for active prefix"
    "[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; } syntax Fn : item { prefix \"fn\"; match body:function_raw; expand drop; } syntax Fn; fn static u32 value;\n")
reject(function_raw_header "syntax-match error for active prefix"
    "[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; } syntax Fn : item { prefix \"fn\"; match body:function; expand drop; } syntax Fn; fn static u32 bad(in u32 value) alien { foreign words; }\n")
reject(function_raw_direct "syntax-match error for active prefix"
    "[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; } syntax Fn : item { prefix \"fn\"; match body:function_raw; expand drop; } syntax Fn; fn struct Record { u32 value; }\n")
reject(nullable_repeat "repetition body may be nullable"
    "${expander}syntax Bad : expression { prefix \"bad\"; match parts:repeat0(item:optional(value:literal)); expand expand; }\n")
reject(nullable_optional "optional/repetition body may be nullable"
    "${expander}syntax Bad : expression { prefix \"bad\"; match part:optional(inner:optional(\"x\")); expand expand; }\n")
reject(nullable_rule_repeat "repetition body may be nullable"
    "${expander}syntax Empty : rule { match item:optional(value:literal); } syntax Bad : expression { prefix \"bad\"; match parts:repeat0(rule(Empty)); expand expand; } syntax Bad;\n")
reject(repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(\"x\") \"x\" \")\"; expand expand; }\n")
reject(separated_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:separated0(number:literal, \",\") \",\" \")\"; expand expand; }\n")
reject(rule_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax X : rule { match \"x\"; } syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(rule(X)) \"x\" \")\"; expand expand; } syntax Bad;\n")
reject(choice_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" branch:choice(one:(\"a\" parts:repeat0(\"x\")) | two:(\"b\")) \"x\" \")\"; expand expand; }\n")
reject(optional_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" part:optional(\"a\" parts:repeat1(\"x\")) skip:optional(\"y\") \"x\" \")\"; expand expand; }\n")
reject(nested_repeat_loop_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat1(\"x\" tail:repeat0(\"x\")) \")\"; expand expand; }\n")
reject(nested_separated_loop_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:separated1(\"x\" tail:repeat0(\",\"), \",\") \")\"; expand expand; }\n")
reject(choice_separated_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" branch:choice(one:(parts:separated1(value:literal, \",\")) | two:(\"b\")) \",\" \")\"; expand expand; }\n")
reject(referenced_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Tail : rule { match parts:repeat0(\"x\"); } syntax Bad : expression { prefix \"bad\"; match \"(\" rule(Tail) skip:optional(\"y\") \"x\" \")\"; expand expand; } syntax Bad;\n")
reject(recursive_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Tail : rule { match \"a\" branch:choice(stop:(parts:repeat1(\"x\")) | next:(rule(Tail))); } syntax Bad : expression { prefix \"bad\"; match \"(\" rule(Tail) \"x\" \")\"; expand expand; } syntax Bad;\n")
reject(mutual_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax A : rule { match \"a\" rule(B); } syntax B : rule { match branch:choice(stop:(parts:repeat1(\"x\")) | next:(rule(A))); } syntax Bad : expression { prefix \"bad\"; match \"(\" rule(A) \"x\" \")\"; expand expand; } syntax Bad;\n")
reject(bound_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Tail : rule { match parts:repeat0(\"x\"); } syntax Good : expression { prefix \"good\"; match \"(\" rule(Tail) \")\"; expand expand; } syntax Bad : expression { prefix \"bad\"; match \"(\" rule(Tail) \"x\" \")\"; expand expand; } syntax Good; syntax Bad;\n")
reject(type_attribute_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(value:type \";\") \"[[\" \"atomic\" \"]]\" \")\"; expand expand; }\n")
reject(declaration_storage_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(value:declaration) \"register\" \")\"; expand expand; }\n")
reject(raw_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(value:tokens_until(\";\") \";\") \"alien\" \")\"; expand expand; }\n")
accept(statement_repeat_closing_continuation
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" parts:repeat0(body:stmt) \")\"; expand expand; } syntax Value; $::static_assert(value (u32 unused; return 3u32; { ; }) == 1u32, \"statement repetitions\"); $::static_assert(value () == 1u32, \"empty statements\");\n")
accept(raw_repeat_closing_continuation
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" parts:repeat0(body:tokens_until(\";\") \";\") \")\"; expand expand; } syntax Value; $::static_assert(value (foreign! { words; }; [[unknown]] [payload];) == 1u32, \"raw repetitions\"); $::static_assert(value () == 1u32, \"empty raw repetition\");\n")
accept(nested_repeat_distinct_continuation
    "${expander}syntax Tail : rule { match parts:repeat0(\"x\"); } syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(one:(\"a\" rule(Tail)) | two:(\"b\")) skip:optional(\"y\") \")\"; expand expand; } syntax Value; $::static_assert(value (a x x y) == 1u32, \"nested repeat\"); $::static_assert(value (b) == 1u32, \"other branch\");\n")
accept(recursive_repeat_distinct_continuation
    "${expander}syntax Tail : rule { match \"a\" branch:choice(stop:(parts:repeat1(\"x\")) | next:(rule(Tail))); } syntax Value : expression { prefix \"value\"; match \"(\" rule(Tail) \")\"; expand expand; } syntax Value; $::static_assert(value (a a x x) == 1u32, \"recursive repeat\");\n")
accept(reused_repeat_distinct_continuations
    "${expander}syntax Tail : rule { match parts:repeat0(\"x\"); } syntax Round : expression { prefix \"round\"; match \"(\" rule(Tail) \")\"; expand expand; } syntax Square : expression { prefix \"square\"; match \"[\" rule(Tail) \"]\"; expand expand; } syntax Round, Square; $::static_assert(round (x x) + square [x] == 2u32, \"reused rule\");\n")
accept(leading_type_attribute_capture
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" body:type \")\"; expand expand; } syntax Value; $::static_assert(value ([[atomic]] const u32) + value ([[address_space(17)]] u32 *) == 2u32, \"discarded attributed types\");\n")
accept(leading_type_attribute_queries
    "$::static_assert(sizeof([[atomic]] u32) == sizeof(u32), \"atomic layout\"); $::static_assert(sizeof([[address_space(0)]] u32 *) == sizeof(u32 *), \"pointer layout\"); static uptr width<T>() { return sizeof(T); } $::static_assert(width::<[[atomic]] u32>() == sizeof(u32), \"generic attributed type\");\n")
accept(leading_type_attribute_nonpointer_discard
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" body:type \")\"; expand expand; } syntax Value; global u32 entry() { return value ([[address_space(0)]] u32); }\n")
reject(leading_type_attribute_nonpointer "address_space requires a pointer type"
    "global uptr entry() { return sizeof([[address_space(0)]] u32); }\n")
# The number of derivations is exponential, but declaration/activation analysis
# must memoize rule summaries rather than enumerate them. No invocation occurs.
set(diamond "${expander}syntax D0 : rule { match \"x\"; }\n")
foreach(level RANGE 1 32)
    math(EXPR previous "${level} - 1")
    string(APPEND diamond "syntax D${level} : rule { match branch:choice(left:(rule(D${previous})) | right:(rule(D${previous}))); }\n")
endforeach()
string(APPEND diamond "syntax Diamond : expression { prefix \"diamond\"; match \"(\" parts:repeat0(rule(D32)) \")\"; expand expand; } syntax Diamond;\n")
accept(shared_rule_analysis "${diamond}" -feval-step-limit=100000)
reject(shared_rule_analysis_budget "matching work budget exceeded"
    "${diamond}" -feval-step-limit=1000)
reject(rule_expression_fence "parsed expression/type capture requires"
    "${expander}syntax Value : rule { match value:expr; } syntax Bad : expression { prefix \"bad\"; match \"(\" rule(Value) \"+\" other:expr \")\"; expand expand; } syntax Bad;\n")
reject(choice_type_fence "parsed expression/type capture requires"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" branch:choice(type:(value:type) | other:(\"other\")) \"+\" \")\"; expand expand; }\n")
reject(optional_expression_fence "parsed expression/type capture requires"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" part:optional(value:expr) skip:optional(\",\") \"+\" \")\"; expand expand; }\n")
reject(repeated_expression_fence "parsed expression/type capture requires"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat1(value:expr) \")\"; expand expand; }\n")
reject(recursive_expression_fence "parsed expression/type capture requires"
    "${expander}syntax Value : rule { match branch:choice(value:(value:expr) | next:(\"next\" rule(Value))); } syntax Bad : expression { prefix \"bad\"; match \"(\" rule(Value) \"+\" \")\"; expand expand; } syntax Bad;\n")
accept(rule_expression_terminal_fence
    "${expander}syntax Fence : rule { match \";\"; } syntax Value : expression { prefix \"value\"; match \"(\" body:expr rule(Fence) \")\"; expand expand; } syntax Value; $::static_assert(value (1u32 + 2u32;) == 1u32, \"rule fence\");\n")
accept(composed_expression_fences
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" body:expr fence:choice(comma:(\",\") | semicolon:(\";\")) tail:optional(value:type) \")\"; expand expand; } syntax Value; $::static_assert(value (1u32, u32) + value (2u32;) == 2u32, \"composed fences\");\n")
accept(nullable_expression_fence
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" body:expr comma:optional(\",\") \")\"; expand expand; } syntax Value; $::static_assert(value (1u32) + value (2u32,) == 2u32, \"nullable fence\");\n")
accept(recursive_expression_fences
    "${expander}syntax Values : rule { match value:expr tail:optional(\",\" rule(Values)); } syntax Value : expression { prefix \"value\"; match \"(\" rule(Values) \")\"; expand expand; } syntax Value; $::static_assert(value (1u32, 2u32, 3u32) == 1u32, \"recursive fence\");\n")
accept(choice_committed_repeat_alternative
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(pairs:(parts:repeat1(\"x\" \"y\")) | other:(\"x\" \"z\")) \")\"; expand expand; } syntax Value; $::static_assert(value (x z) == 1u32, \"surviving alternative\");\n")
accept(choice_committed_repeat_reverse
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(other:(\"x\" \"z\") | pairs:(parts:repeat1(\"x\" \"y\"))) \")\"; expand expand; } syntax Value; $::static_assert(value (x z) == 1u32, \"surviving first alternative\");\n")
accept(choice_committed_opaque_expression
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(pairs:(parts:repeat1(\"x\" \"y\")) | other:(\"x\" value:expr \";\")) \")\"; expand expand; } syntax Value; $::static_assert(value (x missing_macro! { not source; };) == 1u32, \"discarded opaque input\");\n")
accept(choice_committed_later_iteration
    "${expander}syntax Pairs : rule { match parts:repeat1(\"x\" \"y\"); } syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(pairs:(rule(Pairs)) | other:(\"x\" \"y\" \"x\" \"z\")) \")\"; expand expand; } syntax Value; $::static_assert(value (x y x z) == 1u32, \"later malformed iteration\");\n")
accept(choice_committed_separator
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(list:(parts:separated1(number:literal, \",\")) | other:(number:literal \",\" \"end\")) \")\"; expand expand; } syntax Value; $::static_assert(value (1u32, end) == 1u32, \"failed list separator\");\n")
accept(optional_committed_repeat
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" maybe:optional(\"tag\" parts:repeat1(\"x\" \"y\")) \"tag\" \"x\" \"z\" \")\"; expand expand; } syntax Value; $::static_assert(value (tag x z) == 1u32, \"absent optional\");\n")
accept(repetition_choice_committed_repeat
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" parts:repeat1(\"a\" branch:choice(pairs:(pairs:repeat1(\"x\" \"y\")) | other:(\"x\" \"z\"))) \")\"; expand expand; } syntax Value; $::static_assert(value (a x z a x y) == 1u32, \"outer repetition\");\n")
reject(choice_committed_still_ambiguous "ambiguous syntax invocation"
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(pairs:(parts:repeat1(\"x\" \"y\")) | first:(\"x\" \"z\") | second:(\"x\" \"z\")) \")\"; expand expand; } syntax Value; global u32 entry() { return value (x z); }\n")
reject(choice_committed_lengths_ambiguous "ambiguous syntax invocation"
    "${expander}syntax Value : expression { prefix \"value\"; match branch:choice(pairs:(parts:repeat1(\"x\" \"y\")) | short:(\"x\") | long:(\"x\" \"z\")); expand expand; } syntax Value; global u32 entry() { return value x z; }\n")
reject(repeat_lengths_ambiguous "ambiguous syntax invocation"
    "${expander}syntax Value : expression { prefix \"value\"; match parts:repeat1(\"x\"); expand expand; } syntax Value; global u32 entry() { return value x x; }\n")
reject(separated_lengths_ambiguous "ambiguous syntax invocation"
    "${expander}syntax Value : expression { prefix \"value\"; match parts:separated1(number:literal, \",\"); expand expand; } syntax Value; global u32 entry() { return value 1u32, 2u32; }\n")
reject(committed_repeat_no_shorter_match "malformed syntax repetition after committed start"
    "${expander}syntax Value : expression { prefix \"value\"; match parts:repeat0(\"x\" \"y\"); expand expand; } syntax Value; global u32 entry() { return value x y x z; }\n")
reject(committed_separator_no_shorter_match "malformed syntax item after committed separator"
    "${expander}syntax Value : expression { prefix \"value\"; match parts:separated0(number:literal, \",\"); expand expand; } syntax Value; global u32 entry() { return value 1u32, 2u32, end; }\n")
reject(choice_resource_failure "syntax repetition depth exceeded"
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(raw:(body:tokens_until(\";\") \";\") | repeated:(parts:repeat1(\"x\"))) \")\"; expand expand; } syntax Value; global u32 entry() { return value (x x x x x x x x x x x x x x x;); }\n"
    -feval-depth-limit=12)
# Raw capture scans must not turn a depth/work failure into an ordinary
# alternative miss. The exact-token branch is valid independently and the raw
# branch has a deliberately missing suffix, so ample depth permits one match.
foreach(capture IN ITEMS paren bracket block group tokens_until function_raw)
    set(open "(")
    set(close ")")
    if(capture STREQUAL bracket)
        set(open "[")
        set(close "]")
    elseif(capture STREQUAL block OR capture STREQUAL function_raw)
        set(open "{")
        set(close "}")
    elseif(capture STREQUAL group)
        set(open "[[")
        set(close "]]")
    endif()
    string(REPEAT "\"${open}\" " 13 open_pattern)
    string(REPEAT "\"${close}\" " 13 close_pattern)
    # Spaces keep consecutive bracket delimiters distinct lexical tokens.
    string(REPEAT "${open} " 13 open_input)
    string(REPEAT "${close} " 13 close_input)
    set(raw "body:${capture}")
    set(exact "${open_pattern}\"x\" ${close_pattern}")
    set(input "${open_input}x ${close_input}")
    if(capture STREQUAL tokens_until)
        set(raw "body:tokens_until(\";\") \";\"")
        string(APPEND exact "\";\"")
        string(APPEND input ";")
    elseif(capture STREQUAL function_raw)
        string(PREPEND exact "\"u32\" \"f\" \"(\" \")\" ")
        string(PREPEND input "u32 f() ")
    endif()
    foreach(order IN ITEMS raw_first exact_first)
        if(order STREQUAL raw_first)
            set(alternatives "raw:(${raw} \"missing\") | exact:(${exact})")
        else()
            set(alternatives "exact:(${exact}) | raw:(${raw} \"missing\")")
        endif()
        set(source "${expander}syntax Value : expression { prefix \"value\"; match branch:choice(${alternatives}); expand expand; } syntax Value; global u32 entry() { return value ${input}; }\n")
        reject(raw_depth_${capture}_${order} "syntax raw-group nesting depth exceeded"
            "${source}" -feval-depth-limit=12)
        accept(raw_depth_${capture}_${order}_ample "${source}" -feval-depth-limit=64)
    endforeach()
endforeach()
reject(optional_raw_depth "syntax raw-group nesting depth exceeded"
    "${expander}syntax Value : expression { prefix \"value\"; match maybe:optional(body:group \"missing\") ${open_pattern}\"x\" ${close_pattern}; expand expand; } syntax Value; global u32 entry() { return value ${open_input}x ${close_input}; }\n"
    -feval-depth-limit=12)
# A parsed alternative can exhaust public-tree depth before its later terminal
# fails. That is a fatal invocation error even if an exact-token alternative
# already matched, or an optional could otherwise take its empty derivation.
# The same sources must succeed with enough depth, establishing that neither
# the independent alternative nor ordinary capture failure is being rejected.
set(deep_input "( ( ( ( 1u32 ) ) ) )")
set(deep_pattern "\"(\" \"(\" \"(\" \"(\" \"1u32\" \")\" \")\" \")\" \")\"")
foreach(capture IN ITEMS expr type stmt declaration function_header function_decl function_def function_raw function)
    set(fence "")
    if(capture STREQUAL "expr")
        set(input "${deep_input};")
        set(exact "${deep_pattern} \";\"")
        set(fence "\";\"")
    elseif(capture STREQUAL "type")
        set(input "u32 [ ${deep_input} ];")
        set(exact "\"u32\" \"[\" ${deep_pattern} \"]\" \";\"")
        set(fence "\";\"")
    elseif(capture STREQUAL "stmt")
        set(input "{ return ${deep_input}; }")
        set(exact "\"{\" \"return\" ${deep_pattern} \";\" \"}\"")
    elseif(capture STREQUAL "declaration")
        set(input "static u32 field = ${deep_input};")
        set(exact "\"static\" \"u32\" \"field\" \"=\" ${deep_pattern} \";\"")
    elseif(capture STREQUAL "function_def")
        set(input "u32 f() { return ${deep_input}; }")
        set(exact "\"u32\" \"f\" \"(\" \")\" \"{\" \"return\" ${deep_pattern} \";\" \"}\"")
    else()
        set(input "u32 f(in u32 value [ ${deep_input} ])")
        set(exact "\"u32\" \"f\" \"(\" \"in\" \"u32\" \"value\" \"[\" ${deep_pattern} \"]\" \")\"")
        if(capture STREQUAL "function_header" OR capture STREQUAL "function_decl")
            string(APPEND input ";")
            string(APPEND exact " \";\"")
            if(capture STREQUAL "function_header")
                set(fence "\";\"")
            endif()
        else()
            string(APPEND input " { deliberately opaque; }")
            string(APPEND exact " \"{\" \"deliberately\" \"opaque\" \";\" \"}\"")
        endif()
    endif()
    set(parsed "body:${capture} ${fence} \"missing\"")
    foreach(order IN ITEMS parsed_first exact_first optional)
        if(order STREQUAL "parsed_first")
            set(pattern "branch:choice(parsed:(${parsed}) | exact:(${exact}))")
        elseif(order STREQUAL "exact_first")
            set(pattern "branch:choice(exact:(${exact}) | parsed:(${parsed}))")
        else()
            set(pattern "maybe:optional(${parsed}) ${exact}")
        endif()
        set(source "${expander}syntax Value : expression { prefix \"value\"; match \"[\" ${pattern} \"]\"; expand expand; } syntax Value; $::static_assert(value [ ${input} ] == 1u32, \"independent exact alternative\");\n")
        reject(parsed_depth_${capture}_${order} "public syntax tree depth, work, or storage budget exceeded"
            "${source}" -feval-depth-limit=32)
        accept(parsed_depth_${capture}_${order}_ample "${source}" -feval-depth-limit=256)
    endforeach()
endforeach()
# A nested matcher uses the parsed capture's private ordinary-diagnostic sink.
# Its resource failure must nevertheless invalidate the outer invocation, even
# when a raw/exact branch has matched or an optional could take its empty arm.
set(resource_expander "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::syntax::note($::syntax::span(input), \"resource owner executed\"); return $::quote { 1u32 }; }\n")
string(REPEAT "x " 128 many_items)
foreach(category expr stmt)
    set(input "inner(${many_items});")
    set(parsed "value:expr \";\" \"missing\"")
    if(category STREQUAL stmt)
        # This enters the deferred statement's unknown-position matcher probe.
        set(input "unknown!() *inner(${many_items});")
        set(parsed "value:stmt \"missing\"")
    endif()
    foreach(order parsed_first raw_first optional)
        set(raw "value:tokens_until(\";\") \";\"")
        if(order STREQUAL parsed_first)
            set(pattern "branch:choice(parsed:(${parsed}) | raw:(${raw}))")
        elseif(order STREQUAL raw_first)
            set(pattern "branch:choice(raw:(${raw}) | parsed:(${parsed}))")
        else()
            set(pattern "maybe:optional(${parsed}) ${raw}")
        endif()
        set(source "${resource_expander}syntax Inner : expression { prefix \"inner\"; match \"(\" items:repeat1(\"x\") \")\"; expand expand; } syntax Value : expression { prefix \"value\"; match \"[\" ${pattern} \"]\"; expand expand; } syntax Inner, Value; $::static_assert(value [ ${input} ] == 1u32, \"raw fallback result\");\n")
        foreach(level O0 O2)
            reject(nested_match_repeat_${category}_${order}_${level} "syntax repetition depth exceeded"
                "${source}" -${level} -fno-eval-calls -feval-depth-limit=64)
            reject(nested_match_memory_${category}_${order}_${level} "syntax match record byte or memory budget exceeded"
                "${source}" -${level} -fno-eval-calls -feval-depth-limit=256 -feval-memory-limit=262144)
            accept(nested_match_${category}_${order}_${level}_ample "${source}"
                -${level} -fno-eval-calls -feval-depth-limit=256)
        endforeach()
        string(REPLACE "${many_items}" "not_a_match" mismatch "${source}")
        accept(nested_match_${category}_${order}_ordinary_mismatch "${mismatch}" -feval-depth-limit=64)
    endforeach()
endforeach()
foreach(order parsed_first exact_first optional)
    # The exact arm has no raw-group scanner. Only the nested Inner match can
    # exhaust this group limit; its private diagnostics must not hide it.
    string(REPEAT "( " 65 nested_open)
    string(REPEAT ") " 65 nested_close)
    string(REPEAT "\"(\" " 65 nested_open_pattern)
    string(REPEAT "\")\" " 65 nested_close_pattern)
    set(input "inner ${nested_open}x ${nested_close};")
    set(exact "\"inner\" ${nested_open_pattern}\"x\" ${nested_close_pattern}\";\"")
    set(parsed "value:expr \";\" \"missing\"")
    if(order STREQUAL parsed_first)
        set(pattern "branch:choice(parsed:(${parsed}) | exact:(${exact}))")
    elseif(order STREQUAL exact_first)
        set(pattern "branch:choice(exact:(${exact}) | parsed:(${parsed}))")
    else()
        set(pattern "maybe:optional(${parsed}) ${exact}")
    endif()
    set(source "${resource_expander}syntax Inner : expression { prefix \"inner\"; match body:paren; expand expand; } syntax Value : expression { prefix \"value\"; match \"[\" ${pattern} \"]\"; expand expand; } syntax Inner, Value; $::static_assert(value [ ${input} ] == 1u32, \"exact fallback result\");\n")
    reject(nested_match_group_${order} "syntax raw-group nesting depth exceeded"
        "${source}" -feval-depth-limit=64)
    accept(nested_match_group_${order}_ample "${source}" -feval-depth-limit=256)
endforeach()
set(recursive_inner "syntax Recursive : rule { match branch:choice(stop:(\"end\") | more:(\"x\" rule(Recursive))); } syntax Inner : expression { prefix \"inner\"; match \"(\" rule(Recursive) \")\"; expand expand; }")
set(source "${resource_expander}${recursive_inner} syntax Value : expression { prefix \"value\"; match branch:choice(parsed:(value:expr \";\" \"missing\") | raw:(value:tokens_until(\";\") \";\")); expand expand; } syntax Inner, Value; $::static_assert(value inner(${many_items}end); == 1u32, \"recursive fallback\");\n")
reject(nested_match_recursive "syntax pattern matching depth exceeded" "${source}" -feval-depth-limit=64)
accept(nested_match_recursive_ample "${source}" -feval-depth-limit=512)
set(empty_expander "[[syntax_expander]] static $::meta::tokens empty(in $::meta::syntax_match input) { return $::quote {}; }\n")
set(empty_rules "syntax Empty : rule { match maybe:optional(\"x\"); } syntax Chain : rule { match rule(Empty) child:rule(Empty); }\n")
foreach(pattern IN ITEMS "child:rule(Empty)" "rule(Chain)" "maybe:optional(\"x\")"
                         "many:repeat0(\"x\")" "many:separated0(value:literal, \",\")")
    string(MD5 key "${pattern}")
    accept(nullable_eof_${key}
        "${empty_expander}${empty_rules}syntax Noop : item { prefix \"noop\"; match ${pattern}; expand empty; } syntax Noop; noop\n")
endforeach()
accept(nullable_rule_consumed_tail
    "${empty_expander}${empty_rules}syntax Tail : rule { match \"(\" value:choice(leaf:(\"x\") | more:(\"go\" rule(Tail))) \")\" rule(Chain); } syntax Noop : item { prefix \"noop\"; match rule(Tail); expand empty; } syntax Noop; noop (go (go (x)))\n")
reject(nonnullable_rule_eof "syntax-match error for active prefix"
    "${empty_expander}syntax Need : rule { match value:literal; } syntax Noop : item { prefix \"noop\"; match rule(Need); expand empty; } syntax Noop; noop\n")
string(REPEAT "noop " 160 independent_syntax)
set(flat_syntax "${empty_expander}${empty_rules}syntax Noop : item { prefix \"noop\"; match rule(Empty); expand empty; } syntax Noop; ${independent_syntax}\n")
accept(independent_syntax_default "${flat_syntax}")
reject(independent_syntax_work "syntax matching work budget exceeded"
    "${flat_syntax}" -feval-step-limit=300)
accept(independent_syntax_more_work "${flat_syntax}" -feval-step-limit=1000000)
string(REPEAT "empty!() " 160 independent_macros)
set(flat_macros "[[macro]] static $::meta::tokens empty(in $::meta::tokens input) { return $::quote {}; } ${independent_macros}\n")
accept(independent_macros_default "${flat_macros}")
reject(independent_macros_work "syntax matching work budget exceeded"
    "${flat_macros}" -feval-step-limit=300)
accept(independent_macros_more_work "${flat_macros}" -feval-step-limit=1000000)
accept(nullable_choice
    "${expander}syntax Value : expression { prefix \"value\"; match \"(\" branch:choice(empty:(value:optional(\"x\")) | value:(\"y\")) \")\"; expand expand; } syntax Value; global u32 entry() { return value () + value (x) + value (y); }\n")
reject(nullable_choice_ambiguous "ambiguous syntax invocation"
    "${empty_expander}syntax Noop : item { prefix \"noop\"; match branch:choice(first:(value:optional(\"x\")) | second:(value:optional(\"y\"))); expand empty; } syntax Noop; noop\n")
reject(nullable_choice_lengths "ambiguous syntax invocation"
    "${expander}syntax Value : expression { prefix \"value\"; match branch:choice(empty:(value:optional(\"x\")) | value:(\"y\")); expand expand; } syntax Value; global u32 entry() { return value y; }\n")
foreach(combinator IN ITEMS optional repeat0 repeat1 separated0 separated1)
    set(choice_body "branch:choice(empty:(value:optional(\"x\")) | value:(\"y\"))")
    if(combinator MATCHES "^separated")
        string(APPEND choice_body ", \",\"")
    endif()
    reject(nullable_choice_${combinator} "optional/repetition body may be nullable"
        "${expander}syntax Bad : expression { prefix \"bad\"; match body:${combinator}(${choice_body}); expand expand; } syntax Bad;\n")
endforeach()
reject(nullable_choice_cycle "left-recursive or nullable syntax rule cycle"
    "${empty_expander}syntax Cycle : rule { match branch:choice(empty:(value:optional(\"x\")) | recursive:(rule(Cycle))); } syntax Noop : item { prefix \"noop\"; match rule(Cycle); expand empty; } syntax Noop;\n")
reject(nullable_choice_continuation "syntax repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match values:repeat0(\"x\") branch:choice(empty:(value:optional(\"y\")) | value:(\"z\")) \"x\"; expand expand; } syntax Bad;\n")
reject(nullable_choice_fence "parsed expression/type capture requires"
    "${expander}syntax Bad : expression { prefix \"bad\"; match value:expr branch:choice(empty:(value:optional(\";\")) | value:(\",\")) \"+\"; expand expand; } syntax Bad;\n")
reject(choice_duplicate "duplicate choice alternative label"
    "${expander}syntax Bad : expression { prefix \"bad\"; match branch:choice(one:(\"x\") | one:(\"y\")); expand expand; }\n")
reject(choice_ambiguous "ambiguous syntax invocation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" branch:choice(one:(\"x\") | two:(\"x\")) \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (x); }\n")
reject(optional_ambiguous "ambiguous syntax invocation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match maybe:optional(\"x\"); expand expand; } syntax Bad; global u32 entry() { return bad x; }\n")
reject(separated_trailing "malformed syntax item after committed separator"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" values:separated0(number:literal, \",\") \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (1u32,); }\n")
reject(malformed_repeat "malformed syntax repetition after committed start"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(\"x\" \"y\") \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (x z); }\n")
reject(repeat_one_empty "syntax-match error for active prefix"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat1(\"x\") \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (); }\n")
reject(separated_one_empty "syntax-match error for active prefix"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:separated1(value:literal, \",\") \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (); }\n")
reject(choice_bad_label "syntax choice has no variant named"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax_match branch = $::syntax::at(input, \"branch\", 0uptr); if ($::syntax::is_variant(branch, \"missing\")) return $::quote { 1u32 }; return $::quote { 0u32 }; } syntax Bad : expression { prefix \"bad\"; match \"(\" branch:choice(one:(\"x\") | two:(\"y\")) \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (x); }\n")
reject(missing_expander "syntax expander is not visible"
    "${definition}syntax Value;\n")
reject(forward_expander "syntax expander is not visible"
    "${definition}syntax Value;\n${expander}")
reject(wrong_role "syntax expander is not visible"
    "[[macro]] static $::meta::tokens expand(in $::meta::tokens input) { return input; }\n${definition}syntax Value;\n")
reject(nonstatic "syntax_expander.*must be static"
    "[[syntax_expander]] $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote {}; }\n")
reject(parameter "requires exactly one.*syntax_match"
    "[[syntax_expander]] static $::meta::tokens expand(out $::meta::syntax_match input) { return $::quote {}; }\n")
reject(variadic "requires exactly one"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input, ...) { return $::quote {}; }\n")
reject(rule_activation "rule cannot be activated"
    "syntax Rule : rule { match value:literal; }\nsyntax Rule;\n")
reject(rule_kind "must denote a syntax rule"
    "${expander}${definition}syntax Other : expression { prefix \"other\"; match rule(Value); expand expand; } syntax Other;\n")
reject(rule_cycle "left-recursive or nullable syntax rule cycle"
    "${expander}syntax Rule : rule { match rule(Rule) \"x\"; } syntax Other : expression { prefix \"other\"; match rule(Rule); expand expand; } syntax Other;\n")
reject(mutual_rule_cycle "left-recursive or nullable syntax rule cycle"
    "${expander}syntax A : rule { match rule(B) \"x\"; } syntax B : rule { match rule(A) \"y\"; } syntax Other : expression { prefix \"other\"; match rule(A); expand expand; } syntax Other;\n")
reject(nullable_rule_cycle "left-recursive or nullable syntax rule cycle"
    "${expander}syntax A : rule { match maybe:optional(\"x\") rule(A); } syntax Other : expression { prefix \"other\"; match rule(A); expand expand; } syntax Other;\n")
reject(recursive_ambiguity "ambiguous syntax invocation"
    "${expander}syntax Chain : rule { match branch:choice(stop:(\"x\") | again:(\"x\" rule(Chain))); } syntax Other : expression { prefix \"other\"; match child:rule(Chain); expand expand; } syntax Other; global u32 entry() { return other x x; }\n")
string(REPEAT "(" 12 recursive_opens)
string(REPEAT ")" 12 recursive_closes)
reject(rule_depth "syntax pattern matching depth exceeded"
    "${expander}syntax Tree : rule { match branch:choice(leaf:(value:literal) | nested:(\"(\" child:rule(Tree) \")\")); } syntax Other : expression { prefix \"other\"; match root:rule(Tree); expand expand; } syntax Other; global u32 entry() { return other ${recursive_opens}1u32${recursive_closes}; }\n"
    -feval-depth-limit=12)
reject(bundle_cycle "cyclic syntax bundle"
    "syntax One : bundle { use Two; } syntax Two : bundle { use One; } syntax One;\n")
reject(bundle_alias "bundle cannot be aliased"
    "${expander}${definition}syntax Pack : bundle { use Value; } syntax Pack as alias;\n")
reject(empty_bundle "requires a nonempty use list"
    "syntax Empty : bundle {}\n")
reject(conflict "conflicting syntax prefixes in activation"
    "${expander}${definition}syntax Other : statement { prefix \"value\"; match body:block; expand expand; } syntax Value, Other;\n")
reject(inherited "conflicts with an inherited binding"
    "${expander}${definition}syntax Other : expression { prefix \"value\"; match body:paren; expand expand; } syntax Value; global u32 entry() { syntax Other; return 0u32; }\n")
reject(ambiguous "ambiguous syntax entity"
    "namespace one { ${expander}${definition} } namespace two { ${expander}${definition} } using one; using two; syntax Value;\n")
reject(qualified "syntax entity is not visible"
    "namespace one { ${expander}${definition} namespace two { syntax one::Value; } } namespace elsewhere { syntax one::two::Value; }\n")
reject(trailing "expected nonreserved syntax entity name"
    "${expander}${definition}syntax Value,;\n")
reject(local_definition "definitions are allowed only at item position"
    "global u32 entry() { syntax Local : rule { match value:literal; } return 1u32; }\n")
reject(local_region "regions are allowed only at item position"
    "${expander}${definition}global u32 entry() { syntax (Value) { } return 1u32; }\n")
reject(generated_registration "cannot introduce syntax registration"
    "[[macro]] static $::meta::tokens make(in $::meta::tokens input) { return $::quote { syntax X : rule { match value:literal; } }; } make!()\n")
reject(parsed_registration "cannot introduce syntax registration"
    "[[macro]] static $::meta::tokens make(in $::meta::tokens input) { return $::meta::parse(\"syntax X : rule { match value:literal; }\"); } make!()\n")
reject(syntax_registration "cannot introduce syntax registration"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { syntax Value; }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(expression_output "without a semicolon"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { 1u32; }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(statement_output "exactly one complete statement"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { ; ; }; } syntax Stmt : statement { prefix \"stmt\"; match body:block; expand expand; } syntax Stmt; global u32 entry() { stmt {} return 1u32; }\n")
reject(nested_semicolon "expected ';'"
    "[[macro]] static $::meta::tokens expr(in $::meta::tokens input) { return $::quote { 1u32 }; } [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { expr!() }; } syntax Stmt : statement { prefix \"stmt\"; match body:block; expand expand; } syntax Stmt; global u32 entry() { stmt {}; return 1u32; }\n")
reject(adjacent "expected ';'"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { return 1u32 }; } syntax Stmt : statement { prefix \"stmt\"; match body:block; expand expand; } syntax Stmt; global u32 entry() { stmt {}; }\n")
reject(unknown_field "no field named 'missing'"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::syntax::capture(input, \"missing\"); } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(record_index "record index is out of range"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax_match child = $::syntax::at(input, \"child\", 1uptr); return $::quote { 1u32 }; } syntax Rule : rule { match body:paren; } syntax Value : expression { prefix \"value\"; match child:rule(Rule); expand expand; } syntax Value; global u32 entry() { return value (); }\n")
reject(record_capture "requires a primitive token field"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::syntax::capture(input, \"child\"); } syntax Rule : rule { match body:paren; } syntax Value : expression { prefix \"value\"; match child:rule(Rule); expand expand; } syntax Value; global u32 entry() { return value (); }\n")
reject(no_runtime_size "meta values have no runtime size or alignment"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { uptr size = sizeof(input); return $::quote { 1u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(const_match "cannot write a const cell"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { const $::meta::syntax_match copy = input; copy = input; return $::quote { 1u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(volatile_match "meta cells require automatic translation-only storage without runtime qualifiers"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { volatile $::meta::syntax_match copy = input; return $::quote { 1u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(host_io "unresolved name 'read_file'"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return read_file(input); } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(cycle "expansion depth exceeded"
    "${expander}${definition}syntax Value; [[syntax_expander]] static $::meta::tokens looping(in $::meta::syntax_match input) { return $::syntax::input(input); } syntax Loop : expression { prefix \"loop\"; match body:paren; expand looping; } syntax Loop; global u32 entry() { return loop (); }\n")
# A quotation allocates a new context wrapper on each recursive invocation.
# Fixed lookup state still forms a cycle; constructor wrappers cannot evade it.
reject(quoted_cycle "expansion depth exceeded"
    "[[syntax_expander]] static $::meta::tokens looping(in $::meta::syntax_match input) { $::meta::tokens prefix = $::meta::slice($::syntax::input(input), 0uptr, 1uptr); return $::quote { $::unquote(prefix) () }; } syntax Loop : expression { prefix \"loop\"; match body:paren; expand looping; } syntax Loop; global u32 entry() { return loop (); }\n")
reject(quoted_cycle_depth "syntax/procedural expansion depth exceeded"
    "[[syntax_expander]] static $::meta::tokens looping(in $::meta::syntax_match input) { $::meta::tokens prefix = $::meta::slice($::syntax::input(input), 0uptr, 1uptr); return $::quote { $::unquote(prefix) () }; } syntax Loop : expression { prefix \"loop\"; match body:paren; expand looping; } syntax Loop; global u32 entry() { return loop (); }\n" -feval-depth-limit=2)
file(READ "${test_directory}/syntax_cycles.x" context_progress)
set(context_entry "\nglobal u32 entry() { return ContextCycleProgress::run(5u32); }\n")
accept(context_progress "${context_progress}${context_entry}" -O0 -fno-eval-calls)
string(REPLACE "else if (count == 1uptr) next = Stage2::body();"
    "else if (count == 1uptr) next = Stage0::body();" context_cycle "${context_progress}")
reject(context_cycle "expansion depth exceeded" "${context_cycle}${context_entry}")
file(READ "${test_directory}/syntax_depth.x" depth_source)
set(depth_entry "\nglobal u32 entry() { return DeepExpressionExpansion::run(7u32); }\n")
accept(deep_expression "${depth_source}${depth_entry}" -O0 -fno-eval-calls)
file(READ "${test_directory}/syntax_generic_depth.x" generic_depth_source)
string(REPLACE "#include \"syntax_depth.x\"" "${depth_source}"
    generic_depth_source "${generic_depth_source}")
accept(deep_expression_generic "${generic_depth_source}${depth_entry}" -O0 -fno-eval-calls
    -feval-step-limit=16000000)
reject(deep_expression_generic_limit "syntax/procedural expansion depth exceeded"
    "${generic_depth_source}${depth_entry}" -feval-step-limit=16000000
    -feval-depth-limit=224)
reject(deep_expression_limit "syntax/procedural expansion depth exceeded"
    "${depth_source}${depth_entry}" -feval-depth-limit=96)
file(READ "${test_directory}/syntax_recognition_depth.x" recognition_source)
set(recognition_flags -feval-step-limit=100000000
    -feval-byte-limit=268435456 -feval-memory-limit=268435456)
accept(deep_recognition "${recognition_source}" -O0 -fno-eval-calls ${recognition_flags})
# The enclosing block's lexical delimiter scan reaches this depth limit before
# any syntax owner is selected; its diagnostic has no expansion ancestry yet.
reject(deep_recognition_limit "public syntax tree depth, work, or storage budget exceeded"
    "${recognition_source}" ${recognition_flags} -feval-depth-limit=96)
file(READ "${test_directory}/syntax_statement_depth.x" statement_depth_source)
accept(deep_statement "${statement_depth_source}" -O0 -fno-eval-calls)
reject(deep_statement_limit "syntax/procedural expansion depth exceeded"
    "${statement_depth_source}" -feval-depth-limit=96)
file(READ "${test_directory}/syntax_item_depth.x" item_depth_source)
accept(deep_item "${item_depth_source}" -O0 -fno-eval-calls -feval-step-limit=16000000)
reject(deep_item_limit "syntax/procedural expansion depth exceeded"
    "${item_depth_source}" -feval-step-limit=16000000 -feval-depth-limit=224)
file(READ "${test_directory}/syntax_type_depth.x" type_depth_source)
accept(deep_type "${type_depth_source}" -O0 -fno-eval-calls -feval-step-limit=16000000)
reject(deep_type_limit "syntax/procedural expansion depth exceeded"
    "${type_depth_source}" -feval-step-limit=16000000 -feval-depth-limit=224)
reject(resource_cursor_work "syntax matching work budget exceeded"
    "${item_depth_source}\nglobal u32 unrelated = ;\n" -feval-step-limit=100000)
reject(resource_cursor_prior_error "syntax matching work budget exceeded"
    "unexpected;\n${item_depth_source}\nglobal u32 unrelated = ;\n"
    -feval-step-limit=100000)
reject(steps "matching work budget exceeded"
    "${expander}${definition}syntax Value; global u32 entry() { return value (); }\n" -feval-step-limit=1)
reject(bytes "byte or memory budget exceeded"
    "${expander}${definition}syntax Value; global u32 entry() { return value (1u32); }\n" -feval-byte-limit=256)
reject(memory "byte or memory budget exceeded"
    "${expander}${definition}syntax Value; global u32 entry() { return value (1u32); }\n" -feval-memory-limit=256)

# Definitions from one primary input must not leak into another registry.
file(WRITE "${directory}/primary-one.x" "${expander}${definition}")
file(WRITE "${directory}/primary-two.x" "syntax Value;\n")
execute_process(COMMAND "${CC}" -S "${directory}/primary-one.x" "${directory}/primary-two.x"
    -o "${directory}/primary.s" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "syntax entity is not visible")
    message(FATAL_ERROR "primary-input syntax registries were not independent\n${out}\n${err}")
endif()

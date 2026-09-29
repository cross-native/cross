# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(accept_splice case source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${case} failed\n${out}\n${err}")
    endif()
endfunction()
function(reject_splice case expected source)
    set(anchor_kind "error")
    if(ARGC GREATER 3)
        set(anchor_kind "${ARGV3}")
    endif()
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES ":[0-9]+:[0-9]+: error:" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: ${anchor_kind}:")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endfunction()

set(declaration_expander [=[
[[syntax_expander]] static $::meta::tokens copy_decl(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
]=])
set(header_expander [=[
[[syntax_expander]] static $::meta::tokens compose_header(in $::meta::syntax_match input) {
    return $::quote {
        [[noinline]] $::unquote($::syntax::node(input, "header")) [[aligned(16)]]
        $::unquote($::syntax::capture(input, "body"))
    };
}
syntax ComposeHeader : item {
    prefix "compose_header"; match header:function_header body:block; expand compose_header;
}
syntax ComposeHeader;
]=])
accept_splice(function_header_body "${header_expander}
compose_header static u32 composed(in u32 value) { return value + 1u32; }
global u32 entry() { return composed(4u32); }")
accept_splice(function_header_generic_body "${header_expander}
compose_header static T composed<T>(in T value) { T copy = value; return copy; }
global u32 entry() { return composed(4u32); }")
reject_splice(function_header_attribute_conflict "a function cannot be both always_inline and noinline"
    "${header_expander}
compose_header [[always_inline]] static u32 composed(in u32 value) { return value; }
global u32 entry() { return composed(4u32); }" note)
accept_splice(function_header_prototype [=[
[[syntax_expander]] static $::meta::tokens declare(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "header")) [[noinline]]; };
}
syntax Declare : item { prefix "declare"; match header:function_header ";"; expand declare; }
syntax Declare;
declare global u32 composed(in u32 value);
global u32 composed(in u32 value) { return value; }
]=])
set(attribute_expander [=[
[[syntax_expander]] static $::meta::tokens decorate(in $::meta::syntax_match input) {
    return $::quote {
        $::unquote($::syntax::capture(input, "attrs"))
        $::unquote($::syntax::node(input, "header"))
        $::unquote($::syntax::capture(input, "body"))
    };
}
syntax Decorate : item {
    prefix "decorate"; match attrs:tokens_until(";") ";" header:function_header body:block;
    expand decorate;
}
syntax Decorate;
]=])
accept_splice(function_header_generic_attribute "${attribute_expander}
decorate [[noinline, aligned(sizeof(T))]]; static T composed<T>(in T value) { return value; }
global u32 entry() { return composed(4u32); }")
accept_splice(function_header_value_generic_attribute "${attribute_expander}
decorate [[generic(u32 N), aligned(N)]]; static u32 composed(in u32 value) { return value; }
global u32 entry() { return composed<16u32>(4u32); }")
reject_splice(function_header_unknown_attribute "unknown attribute" "${attribute_expander}
decorate [[not_a_cross_attribute]]; static u32 composed() { return 1u32; }" note)
reject_splice(function_header_registration_attribute "expansion output cannot introduce syntax registration"
    "${attribute_expander}
decorate [[macro]]; static u32 composed() { return 1u32; }")
accept_splice(function_header_repeated_decoration [=[
[[syntax_expander]] static $::meta::tokens wrap(in $::meta::syntax_match input) {
    $::meta::syntax header = $::syntax::node(input, "header");
    for (uptr at = 0uptr; at < 4uptr; ++at) {
        header = $::meta::parse("function_header", $::quote {
            [[noinline]] $::unquote(header) [[aligned(16)]]
        }, $::syntax::context(input));
        if ($::meta::child_count(header) != 3uptr ||
            !$::meta::is_production($::meta::child(header, 0uptr), "attribute_specifier") ||
            !$::meta::is_production($::meta::child(header, 1uptr), "function_header") ||
            !$::meta::is_production($::meta::child(header, 2uptr), "attribute_specifier"))
            $::syntax::error($::syntax::span(input), "decorated header lost its public shape");
    }
    $::meta::syntax original = header;
    for (uptr at = 0uptr; at < 4uptr; ++at) original = $::meta::child(original, 1uptr);
    if (!$::meta::is_production($::meta::child(original, 0uptr), "declaration_specifiers"))
        $::syntax::error($::syntax::span(input), "nested decoration flattened the input");
    return $::quote { $::unquote(header) $::unquote($::syntax::capture(input, "body")) };
}
syntax Wrap : item { prefix "wrap"; match header:function_header body:block; expand wrap; }
syntax Wrap;
wrap static u32 composed(in u32 value) { return value; }
global u32 entry() { return composed(4u32); }
]=])
set(header_reparse_expander [=[
[[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
    $::meta::syntax header = $::syntax::node(input, "header");
    header = $::meta::parse("function_header", $::quote {
        [[noinline]] $::unquote(header) [[aligned(16)]]
    },
        $::syntax::context(input));
    $::meta::syntax definition = $::meta::parse("function_def", $::quote {
        $::unquote(header) $::unquote($::syntax::capture(input, "body"))
    }, $::syntax::context(input));
    if ($::meta::is_kind(header, "deferred")) {
        if (!$::meta::is_kind(definition, "deferred"))
            $::syntax::error($::syntax::span(input), "composed function must defer");
    } else if (!$::meta::is_production(definition, "function_definition") ||
               $::meta::child_count(definition) != 2uptr ||
               !$::meta::is_production($::meta::child(definition, 0uptr), "function_header")) {
        $::syntax::error($::syntax::span(input), "composed function lost its header root");
    }
    return $::quote { $::unquote(definition) };
}
syntax Compose : item { prefix "compose"; match header:function_header body:block; expand compose; }
syntax Compose;
]=])
accept_splice(function_header_reparse "${header_reparse_expander}
compose static T composed<T>(in T value) { T copy = value; return copy; }
global u32 entry() { return composed(4u32); }")
accept_splice(function_header_deferred_reparse "${header_reparse_expander}
[[macro]] static $::meta::tokens parameter(in $::meta::tokens name) {
    return $::quote { in u32 $::unquote(name) };
}
compose static u32 composed(parameter!(value)) { return value + 1u32; }
global u32 entry() { return composed(4u32); }")
accept_splice(function_header_discard_deferred [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    $::meta::syntax header = $::meta::parse("function_header", $::quote {
        [[noinline]] $::unquote($::syntax::node(input, "header")) [[aligned(16)]]
    }, $::syntax::context(input));
    $::meta::syntax definition = $::meta::parse("function_def", $::quote {
        $::unquote(header)
        $::unquote($::syntax::capture(input, "body"))
    }, $::syntax::context(input));
    if (!$::meta::is_kind(definition, "deferred"))
        $::syntax::error($::syntax::span(input), "composed function must defer");
    return $::quote {};
}
syntax Discard : item { prefix "discard"; match header:function_header body:block; expand discard; }
syntax Discard;
discard static u32 unused(undefined_parameter!()) { NotYetTyped value; return value; }
global u32 entry() { return 1u32; }
]=])
accept_splice(function_header_prototype_reparse [=[
[[syntax_expander]] static $::meta::tokens declare(in $::meta::syntax_match input) {
    $::meta::syntax node = $::meta::parse("function_decl", $::quote {
        [[noinline]] $::unquote($::syntax::node(input, "header"));
    }, $::syntax::context(input));
    if (!$::meta::is_production(node, "declaration") ||
        $::meta::child_count(node) != 3uptr ||
        !$::meta::is_production($::meta::child(node, 0uptr), "attribute_specifier") ||
        !$::meta::is_production($::meta::child(node, 1uptr), "function_header"))
        $::syntax::error($::syntax::span(input), "prototype lost its header root");
    return $::quote { $::unquote(node) };
}
syntax Declare : item { prefix "declare"; match header:function_header ";"; expand declare; }
syntax Declare;
declare global u32 composed(in u32 value);
global u32 composed(in u32 value) { return value; }
]=])
accept_splice(function_header_namespace_hygiene [=[
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    return $::quote {
        namespace Destination {
            typedef u8 Result;
            $::unquote($::syntax::node(input, "header"))
            $::unquote($::syntax::capture(input, "body"))
        }
    };
}
syntax Move : item { prefix "move"; match header:function_header body:block; expand move; }
namespace Source {
    typedef u16 Result;
    syntax Move;
    move global Result composed(in Result value) { return value; }
}
global u32 entry() {
    $::static_assert(sizeof(Source::Destination::composed(4u16)) == sizeof(u16),
        "header lookup must remain in its captured context");
    return Source::Destination::composed(4u16);
}
]=])
reject_splice(function_header_missing_tail "structured function header requires a body or ';'" [=[
[[syntax_expander]] static $::meta::tokens incomplete(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "header")) };
}
syntax Incomplete : item { prefix "incomplete"; match header:function_header ";"; expand incomplete; }
syntax Incomplete;
incomplete static u32 composed();
]=])
reject_splice(function_header_cannot_escape
    "structured function header must contain one complete direct-function header" [=[
[[macro]] static $::meta::tokens escape(in $::meta::tokens ignored) {
    return $::quote { in u32 value; u32 injected };
}
[[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "header")) { return 1u32; } };
}
syntax Compose : item { prefix "compose"; match header:function_header ";"; expand compose; }
syntax Compose;
compose static u32 function(escape!());
]=])
accept_splice(declaration_block "${declaration_expander}
syntax Copy : statement { prefix \"copy\"; match body:declaration; expand copy_decl; }
global u32 entry() { syntax Copy; copy u32 copied = 5u32; return copied; }")
accept_splice(declaration_block_storage "${declaration_expander}
syntax Copy : statement { prefix \"copy\"; match body:declaration; expand copy_decl; }
global u32 entry() { syntax Copy; copy register u32 copied = 5u32; return copied; }")
accept_splice(declaration_block_typedef "${declaration_expander}
syntax Copy : statement { prefix \"copy\"; match body:declaration; expand copy_decl; }
global u32 entry() { syntax Copy; copy typedef u16 Copied; Copied value = 5u16; return value; }")
accept_splice(declaration_external "${declaration_expander}
syntax Copy : item { prefix \"copy\"; match body:declaration; expand copy_decl; }
syntax Copy;
copy global u32 copied = 5u32;
global u32 entry() { return copied; }")
accept_splice(declaration_external_typedef "${declaration_expander}
syntax Copy : item { prefix \"copy\"; match body:declaration; expand copy_decl; }
syntax Copy;
copy typedef u16 Copied;
global Copied copied = 5u16;
global u32 entry() { return copied; }")
accept_splice(declaration_external_namespace "${declaration_expander}
syntax Copy : item { prefix \"copy\"; match body:declaration; expand copy_decl; }
syntax Copy;
namespace Destination { copy global u32 copied = 5u32; }
global u32 entry() { return Destination::copied; }")
set(reparse_declaration_expander [=[
[[syntax_expander]] static $::meta::tokens reparse_decl(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax parsed = $::meta::parse("declaration",
        $::quote { $::unquote(body) }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "declaration"))
        return $::quote { invalid_declaration(); };
    return $::quote { $::unquote(parsed) };
}
]=])
accept_splice(declaration_reparse_block "${reparse_declaration_expander}
syntax Reparse : statement { prefix \"reparse\"; match body:declaration; expand reparse_decl; }
global u32 entry() { syntax Reparse; reparse register u32 copied = 5u32; return copied; }")
accept_splice(declaration_public_statement_wrapper [=[
[[syntax_expander]] static $::meta::tokens inspect_decl(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax statement = $::meta::parse("stmt",
        $::quote { $::unquote(body) }, $::syntax::context(input));
    if (!$::meta::is_production(statement, "statement") ||
        !$::meta::is_production($::meta::child(statement, 0uptr), "unattributed_statement") ||
        !$::meta::is_production(
            $::meta::child($::meta::child(statement, 0uptr), 0uptr), "declaration"))
        return $::quote { invalid_declaration(); };
    return $::quote { $::unquote(statement) };
}
syntax Inspect : statement { prefix "inspect"; match body:declaration; expand inspect_decl; }
global u32 entry() { syntax Inspect; inspect u32 copied = 5u32; return copied; }
]=])
accept_splice(declaration_reparse_external "${reparse_declaration_expander}
syntax Reparse : item { prefix \"reparse\"; match body:declaration; expand reparse_decl; }
syntax Reparse;
reparse global u32 copied = 5u32;
global u32 entry() { return copied; }")
accept_splice(function_declaration_reparse [=[
[[syntax_expander]] static $::meta::tokens reparse(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax parsed = $::meta::parse("function_decl",
        $::quote { $::unquote(body) }, $::syntax::context(input));
    return $::quote { $::unquote(parsed) };
}
syntax Reparse : item { prefix "reparse"; match body:function_decl; expand reparse; }
syntax Reparse;
reparse global u32 declared(in u32 value);
global u32 declared(in u32 value) { return value; }
]=])
accept_splice(function_definition_deferred_body [=[
[[macro]] static $::meta::tokens introduce(in $::meta::tokens name) {
    return $::quote { typedef u32 $::unquote(name); };
}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Copy : item { prefix "copy"; match body:function_def; expand copy; }
syntax Copy;
copy static u32 function(in u32 value) {
    introduce!(Local);
    Local result = value + 1u32;
    return result;
}
global u32 entry() { return function(4u32); }
]=])
reject_splice(function_declaration_reparse_object "could not recognize complete bounded input" [=[
[[syntax_expander]] static $::meta::tokens reparse(in $::meta::syntax_match input) {
    $::meta::syntax parsed = $::meta::parse("function_decl",
        $::quote { $::unquote($::syntax::node(input, "body")) }, $::syntax::context(input));
    return $::quote { $::unquote(parsed) };
}
syntax Reparse : item { prefix "reparse"; match body:declaration; expand reparse; }
syntax Reparse;
reparse global u32 object;
]=])
reject_splice(function_definition_deferred_assertion "copied generic assertion" [=[
[[macro]] static $::meta::tokens introduce(in $::meta::tokens name) {
    return $::quote { typedef u32 $::unquote(name); };
}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Copy : item { prefix "copy"; match body:function_def; expand copy; }
syntax Copy;
copy static u32 function<u32 N>(in u32 value) {
    introduce!(Local);
    Local result = value;
    $::static_assert(N == 1u32, "copied generic assertion");
    return result;
}
global u32 entry() { return function<2u32>(4u32); }
]=] note)
reject_splice(declaration_external_wrong_category
    "structured syntax splice requires a declaration or function-definition node at external position" [=[
[[syntax_expander]] static $::meta::tokens wrong(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Wrong : item { prefix "wrong"; match value:expr ";"; expand wrong; }
syntax Wrong;
wrong 5u32;
]=])
reject_splice(declaration_external_typedef_collision
    "spliced typedef 'Clash' has a different destination type" [=[
[[syntax_expander]] static $::meta::tokens collide(in $::meta::syntax_match input) {
    return $::quote { typedef u16 Clash;
        $::unquote($::syntax::node(input, "body")) };
}
syntax Collide : item { prefix "collide"; match body:declaration; expand collide; }
syntax Collide;
collide typedef u32 Clash;
]=])
reject_splice(declaration_block_collision
    "spliced local value 'copied' was declared more than once" [=[
[[syntax_expander]] static $::meta::tokens collide(in $::meta::syntax_match input) {
    $::meta::tokens name = $::meta::call_site($::meta::parse("copied"));
    return $::quote { { u32 $::unquote(name) = 0u32;
        $::unquote($::syntax::node(input, "body")) } };
}
syntax Collide : statement { prefix "collide"; match body:declaration; expand collide; }
global u32 entry() { syntax Collide; collide u32 copied = 1u32; return 0u32; }
]=])

set(source [=[
[[syntax_expander]] static $::meta::tokens wrong(in $::meta::syntax_match input) {
    return $::quote { return $::unquote($::syntax::node(input, "body")); };
}
syntax Wrong : statement {
    prefix "wrong"; match body:stmt; expand wrong;
}
global u32 entry() {
    syntax Wrong;
    wrong return 1u32;
}
]=])
set(input "${OUTPUT}/category.x")
file(WRITE "${input}" "${source}")
execute_process(COMMAND "${CC}" -S "${input}" -o "${OUTPUT}/category.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 1 OR
   NOT err MATCHES "structured syntax splice requires an expression node" OR
   NOT err MATCHES "category.x:[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR "incompatible splice category was not diagnosed\n${out}\n${err}")
endif()

set(reverse_source [=[
[[syntax_expander]] static $::meta::tokens wrong(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Wrong : statement { prefix "wrong"; match value:expr ";"; expand wrong; }
global u32 entry() { syntax Wrong; wrong 1u32; return 0u32; }
]=])
set(reverse_input "${OUTPUT}/reverse-category.x")
file(WRITE "${reverse_input}" "${reverse_source}")
execute_process(COMMAND "${CC}" -S "${reverse_input}" -o "${OUTPUT}/reverse-category.s"
    RESULT_VARIABLE reverse_status OUTPUT_VARIABLE reverse_out ERROR_VARIABLE reverse_err)
if(NOT reverse_status EQUAL 1 OR
   NOT reverse_err MATCHES "structured syntax splice requires a statement node" OR
   NOT reverse_err MATCHES "reverse-category.x:[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR "reverse splice category was not diagnosed\n${reverse_out}\n${reverse_err}")
endif()

set(type_source [=[
[[syntax_expander]] static $::meta::tokens wrong_type(in $::meta::syntax_match input) {
    return $::quote { typedef $::unquote($::syntax::node(input, "body")) Alias; };
}
syntax WrongType : statement { prefix "wrong_type"; match body:stmt; expand wrong_type; }
global u32 entry() { syntax WrongType; wrong_type return 1u32; return 0u32; }
]=])
set(type_input "${OUTPUT}/wrong-type-category.x")
file(WRITE "${type_input}" "${type_source}")
execute_process(COMMAND "${CC}" -S "${type_input}" -o "${OUTPUT}/wrong-type-category.s"
    RESULT_VARIABLE type_status OUTPUT_VARIABLE type_out ERROR_VARIABLE type_err)
if(NOT type_status EQUAL 1 OR
   NOT type_err MATCHES "structured syntax splice requires a type node at type position" OR
   NOT type_err MATCHES "wrong-type-category.x:[0-9]+:[0-9]+:")
    message(FATAL_ERROR "wrong type splice category was not diagnosed\n${type_out}\n${type_err}")
endif()

set(type_expression_source [=[
[[syntax_expander]] static $::meta::tokens wrong_expression(in $::meta::syntax_match input) {
    return $::quote { return $::unquote($::syntax::node(input, "value")); };
}
syntax WrongExpression : statement {
    prefix "wrong_expression"; match value:type ";"; expand wrong_expression;
}
global u32 entry() { syntax WrongExpression; wrong_expression u32; return 0u32; }
]=])
set(type_expression_input "${OUTPUT}/type-at-expression.x")
file(WRITE "${type_expression_input}" "${type_expression_source}")
execute_process(COMMAND "${CC}" -S "${type_expression_input}" -o "${OUTPUT}/type-at-expression.s"
    RESULT_VARIABLE type_expression_status OUTPUT_VARIABLE type_expression_out ERROR_VARIABLE type_expression_err)
if(NOT type_expression_status EQUAL 1 OR
   NOT type_expression_err MATCHES "structured syntax splice requires an expression node at expression position" OR
   NOT type_expression_err MATCHES "type-at-expression.x:[0-9]+:[0-9]+:")
    message(FATAL_ERROR "type at expression position was not diagnosed\n${type_expression_out}\n${type_expression_err}")
endif()

set(type_tag_source [=[
[[syntax_expander]] static $::meta::tokens duplicate_type_tag(in $::meta::syntax_match input) {
    return $::quote { { struct TypeTag { u32 first; } earlier;
                       $::unquote($::syntax::node(input, "value")) later; } };
}
syntax DuplicateTypeTag : statement {
    prefix "duplicate_type_tag"; match value:type ";"; expand duplicate_type_tag;
}
global u32 entry() {
    syntax DuplicateTypeTag;
    duplicate_type_tag struct TypeTag { u32 second; };
    return 0u32;
}
]=])
set(type_tag_input "${OUTPUT}/destination-type-tag-collision.x")
file(WRITE "${type_tag_input}" "${type_tag_source}")
execute_process(COMMAND "${CC}" -S "${type_tag_input}" -o "${OUTPUT}/destination-type-tag-collision.s"
    RESULT_VARIABLE type_tag_status OUTPUT_VARIABLE type_tag_out ERROR_VARIABLE type_tag_err)
if(NOT type_tag_status EQUAL 1 OR
   NOT type_tag_err MATCHES "spliced record tag 'TypeTag' duplicates a destination definition" OR
   NOT type_tag_err MATCHES "destination-type-tag-collision.x:[0-9]+:[0-9]+:")
    message(FATAL_ERROR "type splice destination tag collision was not diagnosed\n${type_tag_out}\n${type_tag_err}")
endif()

set(collision_source [=[
[[syntax_expander]] static $::meta::tokens duplicate(in $::meta::syntax_match input) {
    $::meta::tokens name = $::meta::call_site($::meta::parse("copied"));
    $::meta::syntax body = $::syntax::node(input, "body");
    return $::quote { { u32 $::unquote(name) = 0u32; $::unquote(body) } };
}
syntax Duplicate : statement { prefix "duplicate"; match body:stmt; expand duplicate; }
global u32 entry() { syntax Duplicate; duplicate u32 copied = 1u32; return 0u32; }
]=])
set(collision_input "${OUTPUT}/destination-collision.x")
file(WRITE "${collision_input}" "${collision_source}")
execute_process(COMMAND "${CC}" -S "${collision_input}" -o "${OUTPUT}/destination-collision.s"
    RESULT_VARIABLE collision_status OUTPUT_VARIABLE collision_out ERROR_VARIABLE collision_err)
if(NOT collision_status EQUAL 1 OR
   NOT collision_err MATCHES "spliced local value 'copied' was declared more than once" OR
   NOT collision_err MATCHES "destination-collision.x:[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR "destination collision was not diagnosed\n${collision_out}\n${collision_err}")
endif()

set(tag_source [=[
[[syntax_expander]] static $::meta::tokens duplicate_tag(in $::meta::syntax_match input) {
    return $::quote { { struct Tag { u32 first; } earlier; $::unquote($::syntax::node(input, "body")) } };
}
syntax DuplicateTag : statement { prefix "duplicate_tag"; match body:stmt; expand duplicate_tag; }
global u32 entry() { syntax DuplicateTag; duplicate_tag struct Tag { u32 second; } later; return 0u32; }
]=])
set(tag_input "${OUTPUT}/destination-tag-collision.x")
file(WRITE "${tag_input}" "${tag_source}")
execute_process(COMMAND "${CC}" -S "${tag_input}" -o "${OUTPUT}/destination-tag-collision.s"
    RESULT_VARIABLE tag_status OUTPUT_VARIABLE tag_out ERROR_VARIABLE tag_err)
if(NOT tag_status EQUAL 1 OR
   NOT tag_err MATCHES "spliced record tag 'Tag' duplicates a destination definition" OR
   NOT tag_err MATCHES "destination-tag-collision.x:[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR "destination record collision was not diagnosed\n${tag_out}\n${tag_err}")
endif()

set(enum_source [=[
[[syntax_expander]] static $::meta::tokens duplicate_enum(in $::meta::syntax_match input) {
    return $::quote { { enum Tag [[underlying(u32)]] { earlier = 1u32 } earlier_value;
                       $::unquote($::syntax::node(input, "body")) } };
}
syntax DuplicateEnum : statement { prefix "duplicate_enum"; match body:stmt; expand duplicate_enum; }
global u32 entry() {
    syntax DuplicateEnum;
    duplicate_enum enum Tag [[underlying(u16)]] { later = 2u16 } later_value;
    return 0u32;
}
]=])
set(enum_input "${OUTPUT}/destination-enum-collision.x")
file(WRITE "${enum_input}" "${enum_source}")
execute_process(COMMAND "${CC}" -S "${enum_input}" -o "${OUTPUT}/destination-enum-collision.s"
    RESULT_VARIABLE enum_status OUTPUT_VARIABLE enum_out ERROR_VARIABLE enum_err)
if(NOT enum_status EQUAL 1 OR
   NOT enum_err MATCHES "spliced enumeration 'Tag' conflicts with the destination underlying type" OR
   NOT enum_err MATCHES "destination-enum-collision.x:[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR "destination enum collision was not diagnosed\n${enum_out}\n${enum_err}")
endif()

set(switch_source [=[
[[syntax_expander]] static $::meta::tokens duplicate_default(in $::meta::syntax_match input) {
    return $::quote { { default: return 1u32; $::unquote($::syntax::node(input, "body")) } };
}
syntax DuplicateDefault : statement { prefix "duplicate_default"; match body:stmt; expand duplicate_default; }
global u32 entry() {
    syntax DuplicateDefault;
    switch (1u32) { duplicate_default default: return 2u32; }
    return 0u32;
}
]=])
set(switch_input "${OUTPUT}/destination-switch-collision.x")
file(WRITE "${switch_input}" "${switch_source}")
execute_process(COMMAND "${CC}" -S "${switch_input}" -o "${OUTPUT}/destination-switch-collision.s"
    RESULT_VARIABLE switch_status OUTPUT_VARIABLE switch_out ERROR_VARIABLE switch_err)
if(NOT switch_status EQUAL 1 OR
   NOT switch_err MATCHES "duplicate default label in switch" OR
   NOT switch_err MATCHES "destination-switch-collision.x:[0-9]+:[0-9]+: note: token supplied from here")
    message(FATAL_ERROR "destination switch collision was not diagnosed\n${switch_out}\n${switch_err}")
endif()

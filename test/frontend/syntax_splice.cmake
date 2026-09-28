# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

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

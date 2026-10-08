# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")
set(definitions [=[
namespace Imported { static u32 hidden = 7u32; }
global void owner();
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }
syntax KeepItem : item { prefix "keep_item"; match value:declaration; expand keep; }
syntax KeepBlock : statement { prefix "keep_block"; match value:declaration; expand keep; }
syntax DropItem : item { prefix "drop_item"; match value:declaration; expand drop; }
syntax KeepItem, DropItem;
]=])
function(reject name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${definitions}\n${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
           NOT err MATCHES ":[0-9]+:[0-9]+: error:" OR
           NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
foreach(control case default duplicate_default)
    if(control STREQUAL case)
        set(statement "case 1u32: ;")
        set(expected "case label is not inside a switch")
    elseif(control STREQUAL default)
        set(statement "default: ;")
        set(expected "default label is not inside a switch")
    else()
        set(statement "switch (0u32) { default: ; default: ; }")
        set(expected "duplicate default label in switch")
    endif()
    foreach(mode keep project)
        set(output "$::quote { $::unquote($::syntax::node(input, \"value\")) }")
        if(mode STREQUAL project)
            set(output "$::meta::tokens($::syntax::node(input, \"value\"))")
        endif()
        # A parsed function body can be inspected, but emitting it restores
        # source checks even for an unused helper and an untaken branch.
        reject(captured_control_${control}_${mode} "${expected}" "
[[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) {
    return ${output};
}
syntax Emit : item { prefix \"emit\"; match value:function_def; expand emit; }
syntax Emit;
emit static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) { ${statement} }
    return input;
}
global u32 entry() { return 9u32; }
")
    endforeach()
endforeach()

# An opaque parameter/declarator is not permission to swallow a brace body and
# continue through the next declaration's semicolon. No owner may run first.
foreach(scope item block)
    set(categories declaration)
    if(scope STREQUAL block)
        list(APPEND categories stmt)
    endif()
    foreach(category IN LISTS categories)
        foreach(header "static u32 ignored(unknown!())" "u32 name!()"
                       "u32 (*ignored(unknown!()))(in u32)"
                       "unknown!() (*ignored())(in u32)" "unknown!() (condition)"
                       "unknown!() + value")
            foreach(tail ";" "u32 neighbor;" "struct Neighbor { u32 member; };")
                string(MD5 name "${scope}-${category}-${header}-${tail}")
                set(kind item)
                set(body "syntax Boundary; boundary ${header} { unknown!(); } ${tail}")
                if(scope STREQUAL block)
                    set(kind statement)
                    set(body "global u32 entry() { ${body} return 9u32; }")
                endif()
                reject(boundary_${name} "syntax-match error for active prefix" "
[[syntax_expander]] static $::meta::tokens boundary_owner(in $::meta::syntax_match input) {
    $::syntax::error($::syntax::span(input), \"boundary owner must not execute\");
    return $::quote {};
}
syntax Boundary : ${kind} { prefix \"boundary\"; match value:${category}; expand boundary_owner; }
${body}")
            endforeach()
        endforeach()
    endforeach()
endforeach()

# An active expression prefix can also spell a declarator name. Until written
# source establishes an expression/initializer, it cannot claim that body's
# braces on behalf of unknown leading macro output.
foreach(tail ";" "u32 neighbor;")
    string(MD5 name "${tail}")
    reject(boundary_expression_name_${name} "syntax-match error for active prefix" "
[[syntax_expander]] static $::meta::tokens boundary_owner(in $::meta::syntax_match input) {
    $::syntax::error($::syntax::span(input), \"boundary owner must not execute\");
    return $::quote { ; };
}
[[syntax_expander]] static $::meta::tokens expression_owner(in $::meta::syntax_match input) {
    $::syntax::error($::syntax::span(input), \"expression owner must not execute\");
    return $::quote { 0u32 };
}
syntax Boundary : statement { prefix \"boundary\"; match value:stmt; expand boundary_owner; }
syntax Expression : expression { prefix \"possible_name\"; match parameters:paren body:block; expand expression_owner; }
global u32 entry() {
    syntax Boundary, Expression;
    boundary unknown!() *possible_name(in u32) { unknown!(); } ${tail}
    return 9u32;
}")
endforeach()

# A retained declaration keeps its identifiers, not permission to use a
# block-only grammar form at file scope (or the reverse). Inspection must reject
# the same scope mismatch for raw, projected and structured input.
set(context_probe [=[
static $::meta::syntax find_return(in $::meta::syntax node) {
    if ($::meta::is_production(node, "jump_statement")) return node;
    if ($::meta::is_kind(node, "core"))
        for (uptr i = 0uptr; i < $::meta::child_count(node); ++i) {
            $::meta::syntax result = find_return($::meta::child(node, i));
            if ($::meta::is_production(result, "jump_statement")) return result;
        }
    return node;
}
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::context file_context = $::syntax::context(input);
    $::meta::context block_context = $::syntax::context(find_return($::syntax::node(input, "function")));
    $::meta::syntax source = $::meta::parse("declaration", $::quote { @FORM@ }, @ORIGINAL@);
    $::meta::syntax parsed = $::meta::parse("@CATEGORY@", @TOKENS@, @DESTINATION@);
    return $::quote {};
}
syntax Inspect : item { prefix "inspect"; match function:function_def; expand inspect; }
syntax Inspect;
inspect static void context_source() { return; }
global u32 entry() { return 9u32; }
]=])
foreach(FORM "register u32 object;" "stack u32 object;" "global u32 object;" "inline u32 function();")
    string(MAKE_C_IDENTIFIER "${FORM}" name)
    set(ORIGINAL block_context)
    set(DESTINATION file_context)
    set(categories declaration)
    if(FORM MATCHES "^(global|inline)")
        set(ORIGINAL file_context)
        set(DESTINATION block_context)
        list(APPEND categories stmt)
    endif()
    foreach(CATEGORY IN LISTS categories)
        foreach(projection raw structured tokens)
            if(projection STREQUAL raw)
                set(TOKENS "$::quote { ${FORM} }")
            elseif(projection STREQUAL structured)
                set(TOKENS "$::quote { $::unquote(source) }")
            else()
                set(TOKENS "$::meta::tokens(source)")
            endif()
            string(CONFIGURE "${context_probe}" source @ONLY)
            reject(context_${name}_${CATEGORY}_${projection}
                "could not recognize complete bounded input for '${CATEGORY}'" "${source}")
        endforeach()
    endforeach()
endforeach()

reject(item_assert "surviving item assertion"
    "keep_item $::static_assert(0u32, \"surviving item assertion\"); global u32 entry() { return 9u32; }")
reject(block_assert "surviving block assertion"
    "global u32 entry() { syntax KeepBlock; keep_block $::static_assert(0u32, \"surviving block assertion\"); return 9u32; }")
reject(generic_assert "surviving generic assertion" [=[
static u32 check<T>() {
    syntax KeepBlock;
    T value = 0;
    keep_block $::static_assert(sizeof(value) == 2uptr, "surviving generic assertion");
    return 9u32;
}
global u32 entry() { return check<u32>(); }
]=])
reject(discarded_import "unresolved name 'hidden'"
    "drop_item using Imported; global u32 entry() { return hidden; }")
reject(block_import_scope "unresolved name 'hidden'"
    "global u32 entry() { syntax KeepBlock; { keep_block using Imported; } return hidden; }")
reject(private_label_owner "global label declaration requires a global stable-ABI function" [=[
static void private_owner() { point: ; }
keep_item global label private_owner::point;
global u32 entry() { return 9u32; }
]=])
reject(unbraced_import "using declaration splice requires a compound-statement block item"
    "global u32 entry() { syntax KeepBlock; if (1u32) keep_block using Imported; return 9u32; }")
reject(loop_import "using declaration splice requires a compound-statement block item"
    "global u32 entry() { syntax KeepBlock; while (0u32) keep_block using Imported; return 9u32; }")
reject(label_in_block "global-label declaration splice requires item scope" [=[
[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    return $::quote { global u32 entry() { $::unquote($::syntax::node(input, "value")) return 9u32; } };
}
syntax Move : item { prefix "move"; match value:declaration; expand move; }
syntax Move;
global void owner() { global label point: ; }
move global label owner::point;
]=])
reject(parsed_import_isolation "unresolved name 'hidden'" [=[
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::syntax parsed = $::meta::parse("declaration", $::quote { using Imported; }, $::syntax::context(input));
    return $::quote {};
}
syntax Inspect : item { prefix "inspect"; match ";"; expand inspect; }
syntax Inspect;
inspect;
global u32 entry() { return hidden; }
]=])
reject(parsed_label_block_context "could not recognize complete bounded input for 'declaration'" [=[
static $::meta::syntax find_return(in $::meta::syntax node) {
    if ($::meta::is_production(node, "jump_statement")) return node;
    if ($::meta::is_kind(node, "core"))
        for (uptr i = 0uptr; i < $::meta::child_count(node); ++i) {
            $::meta::syntax result = find_return($::meta::child(node, i));
            if ($::meta::is_production(result, "jump_statement")) return result;
        }
    return node;
}
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax block_node = find_return($::syntax::node(input, "function"));
    $::meta::syntax parsed = $::meta::parse("declaration", $::quote { $::unquote(value) }, $::syntax::context(block_node));
    return $::quote {};
}
syntax Inspect : item { prefix "inspect"; match value:declaration function:function_def; expand inspect; }
syntax Inspect;
inspect global label owner::point; static void context_source() { return; }
global u32 entry() { return 9u32; }
]=])
reject(using_as_statement "could not recognize complete bounded input for 'stmt'" [=[
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("stmt", $::quote { $::unquote(value) }, $::syntax::context(input));
    return $::quote {};
}
syntax Inspect : item { prefix "inspect"; match value:declaration; expand inspect; }
syntax Inspect;
inspect using Imported;
global u32 entry() { return 9u32; }
]=])
foreach(form "using Imported;" "global label owner::point;" "$::static_assert(1u32, \"ok\");")
    string(MAKE_C_IDENTIFIER "${form}" name)
    reject("not_function_${name}" "could not recognize complete bounded input for 'function_decl'" "
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::syntax parsed = $::meta::parse(\"function_decl\", $::quote { $::unquote($::syntax::node(input, \"value\")) }, $::syntax::context(input));
    return $::quote {};
}
syntax Inspect : item { prefix \"inspect\"; match value:declaration; expand inspect; }
syntax Inspect;
inspect ${form}
global u32 entry() { return 9u32; }
")
endforeach()

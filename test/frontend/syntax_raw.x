// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if !$::has_attribute(syntax_expander) || !$::has_builtin($::syntax::capture) || !$::has_builtin($::syntax::is_variant) || !$::has_intrinsic($::syntax::is_variant) || !$::has_builtin($::syntax::node) || !$::has_intrinsic($::meta::child)
#error implemented syntax operations must be discoverable
#endif
#if $::has_feature($::feature::syntax_extensions)
#error full public-tree syntax support is not implemented yet
#endif

[[macro]] static $::meta::tokens copied(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens forwarded(in $::meta::tokens input) {
    return $::quote { copied! { $::unquote(input) } };
}
namespace flow {
    [[syntax_expander]] static $::meta::tokens five(in $::meta::syntax_match input) {
        return $::quote { 5u32 };
    }
    syntax Base : expression { prefix "base"; match body:paren; expand five; }
    syntax Base;
    [[syntax_expander]] static $::meta::tokens chain(in $::meta::syntax_match input) {
        return $::quote { base () + $::unquote($::syntax::capture(input, "body")) };
    }
    syntax Chain : expression { prefix "chain"; match body:paren; expand chain; }
    [[syntax_expander]] static $::meta::tokens copy_body(in $::meta::syntax_match input) {
        return $::syntax::capture(input, "body");
    }
    syntax Run : statement { prefix "run"; match body:block; expand copy_body; }
    [[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
        return $::quote { ; };
    }
    syntax Discard : statement { prefix "discard"; match body:group; expand discard; }
    [[syntax_expander]] static $::meta::tokens returning(in $::meta::syntax_match input) {
        return $::quote { return $::unquote($::syntax::capture(input, "value")); };
    }
    syntax Ret : statement { prefix "ret"; match value:tokens_until(";") ";"; expand returning; }
    syntax Pair : rule { match left:literal ":" right:literal; }
    [[syntax_expander]] static $::meta::tokens pair(in $::meta::syntax_match input) {
        if ($::syntax::count(input, "values") != 1uptr) return $::quote { 0u32 };
        $::meta::syntax_match values = $::syntax::at(input, "values", 0uptr);
        return $::quote {
            $::unquote($::syntax::capture(values, "left")) +
            $::unquote($::syntax::capture(values, "right"))
        };
    }
    syntax Sum : expression { prefix "sum"; match "(" values:rule(Pair) ")"; expand pair; }
    syntax Tools : bundle { use Run; use Discard; use Chain as plus; use Sum; use Ret; }
    [[syntax_expander]] static $::meta::tokens make(in $::meta::syntax_match input) {
        return $::quote {
            static u32 $::unquote($::syntax::capture(input, "name"))() {
                return $::unquote($::syntax::capture(input, "value"));
            }
        };
    }
    syntax Make : item { prefix "make"; match name:ident value:literal ";"; expand make; }
    syntax CopyFunction : item { prefix "copy_fn"; match body:function_raw; expand copy_body; }
    [[syntax_expander]] static $::meta::tokens target_size(in $::meta::syntax_match input) {
        if (sizeof(uptr) == 4uptr) return $::quote { 4u32 };
        return $::quote { 8u32 };
    }
    syntax Width : expression { prefix "width"; match body:paren; expand target_size; }
    [[syntax_expander]] static $::meta::tokens primitive(in $::meta::syntax_match input) {
        return $::syntax::capture(input, "body");
    }
    syntax Lit : expression { prefix "lit"; match body:literal; expand primitive; }
    syntax Name : expression { prefix "named"; match body:name; expand primitive; }
    syntax Bracket : expression { prefix "bracket"; match body:bracket; expand five; }
    [[syntax_expander]] static $::meta::tokens group_shape(in $::meta::syntax_match input) {
        $::meta::syntax root = $::syntax::node(input, "body");
        if (!$::meta::is_kind(root, "group") || $::meta::child_count(root) != 6uptr)
            return $::quote { 0u32 };
        if (!$::meta::is_kind($::meta::child(root, 0uptr), "token") ||
            !$::meta::is_kind($::meta::child(root, 5uptr), "token"))
            return $::quote { 0u32 };
        $::meta::syntax body = $::meta::child(root, 3uptr);
        if (!$::meta::is_kind(body, "group") || $::meta::child_count(body) != 5uptr)
            return $::quote { 0u32 };
        for (uptr at = 1uptr; at < 4uptr; ++at)
            if (!$::meta::is_kind($::meta::child(body, at), "group")) return $::quote { 0u32 };
        if ($::meta::child_count($::meta::child(body, 1uptr)) != 3uptr ||
            $::meta::child_count($::meta::child(body, 2uptr)) != 3uptr ||
            $::meta::child_count($::meta::child(body, 3uptr)) != 2uptr)
            return $::quote { 0u32 };
        // Token-sequence len counts token trees, so the complete balanced
        // group is one element even though its projection owns 15 tokens.
        if ($::meta::len($::meta::tokens(root)) != 1uptr ||
            $::meta::len($::syntax::capture(input, "body")) != 1uptr)
            return $::quote { 0u32 };
        return $::quote { 31u32 };
    }
    syntax RawParen : expression { prefix "raw_paren"; match body:paren; expand group_shape; }
    syntax RawBracket : expression { prefix "raw_bracket"; match body:bracket; expand group_shape; }
    syntax RawBlock : expression { prefix "raw_block"; match body:block; expand group_shape; }
    syntax RawGroup : expression { prefix "raw_group"; match body:group; expand group_shape; }
    [[syntax_expander]] static $::meta::tokens group_project(in $::meta::syntax_match input) {
        return $::meta::tokens($::syntax::node(input, "body"));
    }
    syntax RawProject : expression { prefix "raw_project"; match body:paren; expand group_project; }
    [[syntax_expander]] static $::meta::tokens group_record(in $::meta::syntax_match input) {
        $::meta::syntax_match record = $::syntax::at(input, "record", 0uptr);
        if (!$::syntax::is_variant(record, "group")) return $::quote { 0u32 };
        if (!$::meta::is_kind($::syntax::node(record, "body"), "group") ||
            $::meta::len($::syntax::capture(record, "body")) != 1uptr) return $::quote { 0u32 };
        return $::meta::tokens($::syntax::node(record, "body"));
    }
    syntax RawRecord : expression {
        prefix "raw_record"; match record:choice(group:(body:group)); expand group_record;
    }
    syntax Bang : expression { prefix "bang"; match "!" body:literal; expand primitive; }
    [[syntax_expander]] static $::meta::tokens empty(in $::meta::syntax_match input) {
        return $::quote {};
    }
    syntax Drop : item { prefix "drop"; match body:block; expand empty; }
    syntax DropTokens : item { prefix "drop_tokens"; match body:tokens_until(";") ";"; expand empty; }
    syntax DropFunction : item { prefix "drop_fn"; match body:function; expand empty; }
    [[syntax_expander]] static $::meta::tokens greater(in $::meta::syntax_match input) {
        return $::quote { 3u32 > 2u32 ? 9u32 : 0u32 };
    }
    syntax Greater : expression { prefix "greater"; match body:paren; expand greater; }
    [[syntax_expander]] static $::meta::tokens maybe_value(in $::meta::syntax_match input) {
        if ($::syntax::count(input, "value") == 0uptr) return $::quote { 10u32 };
        $::meta::syntax_match child = $::syntax::at(input, "value", 0uptr);
        return $::syntax::capture(child, "number");
    }
    syntax Maybe : expression {
        prefix "maybe"; match "(" value:optional(number:literal) ")"; expand maybe_value;
    }
    [[syntax_expander]] static $::meta::tokens list_sum(in $::meta::syntax_match input) {
        if ($::syntax::count(input, "values") == 0uptr) return $::quote { 0u32 };
        $::meta::syntax_match first = $::syntax::at(input, "values", 0uptr);
        if ($::syntax::count(input, "values") == 1uptr) return $::syntax::capture(first, "number");
        $::meta::syntax_match second = $::syntax::at(input, "values", 1uptr);
        return $::quote {
            $::unquote($::syntax::capture(first, "number")) +
            $::unquote($::syntax::capture(second, "number"))
        };
    }
    syntax List : expression {
        prefix "list"; match "(" values:separated0(number:literal, ",") ")"; expand list_sum;
    }
    syntax ListOne : expression {
        prefix "list_one"; match "(" values:separated1(number:literal, ",") ")"; expand list_sum;
    }
    [[syntax_expander]] static $::meta::tokens repeated(in $::meta::syntax_match input) {
        if ($::syntax::count(input, "parts") == 2uptr) return $::quote { 2u32 };
        return $::quote { 0u32 };
    }
    syntax Repeat : expression {
        prefix "repeat"; match "(" parts:repeat1("a") ")"; expand repeated;
    }
    syntax RepeatZero : expression {
        prefix "repeat_zero"; match "(" parts:repeat0("a") ")"; expand repeated;
    }
    [[syntax_expander]] static $::meta::tokens selected(in $::meta::syntax_match input) {
        $::meta::syntax_match branch = $::syntax::at(input, "branch", 0uptr);
        if ($::syntax::is_variant(branch, "left")) return $::syntax::capture(branch, "number");
        return $::quote { $::unquote($::syntax::capture(branch, "number")) + 1u32 };
    }
    syntax Select : expression {
        prefix "select";
        match "(" branch:choice(left:("left" number:literal) | right:("right" number:literal)) ")";
        expand selected;
    }
    syntax Tree : rule {
        match branch:choice(leaf:(value:literal) |
                            pair:("(" left:rule(Tree) "+" right:rule(Tree) ")"));
    }
    [[syntax_expander]] static $::meta::tokens tree_shape(in $::meta::syntax_match input) {
        $::meta::syntax_match root = $::syntax::at(input, "root", 0uptr);
        $::meta::syntax_match branch = $::syntax::at(root, "branch", 0uptr);
        if (!$::syntax::is_variant(branch, "pair")) return $::quote { 0u32 };
        $::meta::syntax_match left = $::syntax::at(branch, "left", 0uptr);
        $::meta::syntax_match left_branch = $::syntax::at(left, "branch", 0uptr);
        if (!$::syntax::is_variant(left_branch, "pair")) return $::quote { 0u32 };
        $::meta::syntax_match right = $::syntax::at(branch, "right", 0uptr);
        $::meta::syntax_match right_branch = $::syntax::at(right, "branch", 0uptr);
        if (!$::syntax::is_variant(right_branch, "leaf")) return $::quote { 0u32 };
        return $::quote { 13u32 };
    }
    syntax TreeUse : expression { prefix "tree"; match root:rule(Tree); expand tree_shape; }
    syntax MutualA : rule {
        match branch:choice(leaf:(value:literal) | nested:("(" child:rule(MutualB) ")"));
    }
    syntax MutualB : rule { match "[" child:rule(MutualA) "]"; }
    [[syntax_expander]] static $::meta::tokens mutual_shape(in $::meta::syntax_match input) {
        $::meta::syntax_match root = $::syntax::at(input, "root", 0uptr);
        $::meta::syntax_match branch = $::syntax::at(root, "branch", 0uptr);
        if (!$::syntax::is_variant(branch, "nested")) return $::quote { 0u32 };
        $::meta::syntax_match middle = $::syntax::at(branch, "child", 0uptr);
        $::meta::syntax_match leaf = $::syntax::at(middle, "child", 0uptr);
        $::meta::syntax_match leaf_branch = $::syntax::at(leaf, "branch", 0uptr);
        if (!$::syntax::is_variant(leaf_branch, "leaf")) return $::quote { 0u32 };
        return $::quote { 14u32 };
    }
    syntax MutualUse : expression { prefix "mutual"; match root:rule(MutualA); expand mutual_shape; }
    [[syntax_expander]] static $::meta::tokens parsed_expression(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        if (!$::meta::is_kind(body, "core") ||
            !$::meta::is_production(body, "assignment_expression") ||
            $::meta::child_count(body) != 1uptr) return $::quote { 0u32 };
        $::meta::syntax conditional = $::meta::child(body, 0uptr);
        if (!$::meta::is_production(conditional, "conditional_expression"))
            return $::quote { 0u32 };
        return $::meta::tokens(body);
    }
    syntax ParsedExpr : expression { prefix "parsed"; match body:expr; expand parsed_expression; }
    [[syntax_expander]] static $::meta::tokens parsed_type(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        if (!$::meta::is_production(body, "type_name")) return $::quote { typedef u16 CapturedType; };
        return $::quote { typedef $::unquote($::meta::tokens(body)) CapturedType; };
    }
    syntax ParsedType : item { prefix "emit_type"; match body:type ";"; expand parsed_type; }
    [[syntax_expander]] static $::meta::tokens parsed_statement(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        if (!$::meta::is_production(body, "statement")) return $::quote { ; };
        $::meta::syntax ordinary = $::meta::child(body, 0uptr);
        if (!$::meta::is_production(ordinary, "unattributed_statement")) return $::quote { ; };
        $::meta::syntax form = 1u32 ? $::meta::child(ordinary, 0uptr) : ordinary;
        if ($::meta::is_production(form, "selection_statement") &&
            $::meta::child_count(form) != 7uptr) return $::quote { ; };
        if ($::meta::is_production(form, "iteration_statement")) {
            for (uptr at = 0uptr; at < $::meta::child_count(form); ++at) {
                $::meta::syntax part = $::meta::child(form, at);
                if ($::meta::is_production(part, "for_initializer")) {
                    if ($::meta::child_count(part) > 1uptr)
                        return $::quote { public_schema_failure(); };
                    if ($::meta::child_count(part) == 1uptr) {
                        $::meta::syntax init = $::meta::child(part, 0uptr);
                        if (!$::meta::is_production(init, "expression") &&
                            !$::meta::is_production(init, "declaration_without_final_semicolon"))
                            return $::quote { public_schema_failure(); };
                    }
                }
            }
        }
        return $::meta::tokens(body);
    }
    syntax ParsedStmt : statement { prefix "parsed_stmt"; match body:stmt; expand parsed_statement; }
    [[syntax_expander]] static $::meta::tokens parsed_declaration(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        if (!$::meta::is_production(body, "declaration")) return $::quote {};
        return $::meta::tokens(body);
    }
    syntax ParsedDecl : item { prefix "parsed_decl"; match body:declaration; expand parsed_declaration; }
    syntax ParsedPrototype : item { prefix "parsed_prototype"; match body:function_decl; expand parsed_declaration; }
    [[syntax_expander]] static $::meta::tokens parsed_definition(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        if (!$::meta::is_production(body, "function_definition")) return $::quote {};
        return $::meta::tokens(body);
    }
    syntax ParsedDefinition : item { prefix "parsed_definition"; match body:function_def; expand parsed_definition; }
    [[syntax_expander]] static $::meta::tokens parsed_header(in $::meta::syntax_match input) {
        $::meta::syntax header = $::syntax::node(input, "header");
        if (!$::meta::is_production(header, "function_header")) return $::quote {};
        return $::quote {
            $::unquote($::meta::tokens(header))
            $::unquote($::syntax::capture(input, "body"))
        };
    }
    syntax ParsedHeader : item { prefix "parsed_header"; match header:function_header body:block; expand parsed_header; }
    syntax DropParsed : item { prefix "drop_parsed"; match body:expr ";"; expand empty; }
    [[syntax_expander]] static $::meta::tokens opaque_macro(in $::meta::syntax_match input) {
        $::meta::syntax root = $::syntax::node(input, "body");
        $::meta::syntax leaf = root;
        while ($::meta::is_kind(leaf, "core") && $::meta::child_count(leaf) == 1uptr)
            leaf = $::meta::child(leaf, 0uptr);
        if (!$::meta::is_kind(leaf, "macro") || $::meta::child_count(leaf) != 0uptr)
            return $::quote { 0u32 };
        return $::meta::tokens(root);
    }
    syntax ParsedMacro : expression { prefix "parsed_macro"; match body:expr; expand opaque_macro; }
    [[syntax_expander]] static $::meta::tokens explode(in $::meta::syntax_match input) {
        uptr invalid = 1uptr / 0uptr;
        return $::quote { 0u32 };
    }
    syntax Explode : expression { prefix "explode"; match body:paren; expand explode; }
    [[syntax_expander]] static $::meta::tokens parsed_list(in $::meta::syntax_match input) {
        $::meta::syntax_match left = $::syntax::at(input, "values", 0uptr);
        $::meta::syntax_match right = $::syntax::at(input, "values", 1uptr);
        return $::quote {
            ($::unquote($::meta::tokens($::syntax::node(left, "value")))) +
            ($::unquote($::meta::tokens($::syntax::node(right, "value"))))
        };
    }
    syntax ParsedList : expression { prefix "parsed_list"; match "(" values:separated1(value:expr, ",") ")"; expand parsed_list; }
    [[syntax_expander]] static $::meta::tokens generic_shape(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::meta::syntax postfix = body;
        while ($::meta::is_kind(postfix, "core") && $::meta::child_count(postfix) == 1uptr)
            postfix = $::meta::child(postfix, 0uptr);
        if (!$::meta::is_production(postfix, "postfix_expression")) return $::quote { 0u32 };
        $::meta::syntax arguments = $::meta::child(postfix, 1uptr);
        if (!$::meta::is_production(arguments, "generic_arguments") ||
            $::meta::child_count(arguments) != 4uptr) return $::quote { 0u32 };
        $::meta::syntax argument = $::meta::child(arguments, 2uptr);
        if (!$::meta::is_production(argument, "generic_argument") ||
            $::meta::child_count(argument) != 1uptr) return $::quote { 0u32 };
        if (!$::meta::is_production($::meta::child(argument, 0uptr), "constant_expression"))
            return $::quote { 0u32 };
        uptr length = $::meta::len($::meta::tokens(body));
        if (length == 8uptr || length == 12uptr) return $::quote { 23u32 };
        return $::quote { 0u32 };
    }
    syntax GenericShape : expression { prefix "generic_shape"; match body:expr; expand generic_shape; }
    syntax DropStmt : statement { prefix "drop_stmt"; match body:stmt; expand discard; }
    syntax ExplodeStmt : statement { prefix "explode_stmt"; match body:block; expand explode; }
    [[syntax_expander]] static $::meta::tokens parsed_array(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        uptr found = 0uptr;
        for (uptr at = 0uptr; at < $::meta::child_count(body); ++at) {
            $::meta::syntax part = $::meta::child(body, at);
            if ($::meta::is_production(part, "initializer")) {
                if ($::meta::child_count(part) != 5uptr) return $::quote { public_schema_failure; };
                $::meta::syntax entry = $::meta::child(part, 1uptr);
                if (!$::meta::is_production(entry, "initializer_entry") ||
                    $::meta::child_count(entry) != 3uptr) return $::quote { public_schema_failure; };
                $::meta::syntax designator = $::meta::child(entry, 0uptr);
                if (!$::meta::is_production(designator, "designator") ||
                    $::meta::child_count(designator) != 3uptr ||
                    !$::meta::is_production($::meta::child(designator, 1uptr), "constant_expression"))
                    return $::quote { public_schema_failure; };
                ++found;
            }
            if ($::meta::is_production(part, "array_suffix")) {
                if ($::meta::child_count(part) != 3uptr ||
                    !$::meta::is_production($::meta::child(part, 1uptr), "assignment_expression"))
                    return $::quote { public_schema_failure; };
                ++found;
            }
        }
        if (found != 2uptr) return $::quote { public_schema_failure; };
        return $::meta::tokens(body);
    }
    syntax ParsedArray : item { prefix "parsed_array"; match body:declaration; expand parsed_array; }
    [[syntax_expander]] static $::meta::tokens parsed_generic(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::meta::syntax attributes = $::meta::child(body, 0uptr);
        if (!$::meta::is_production(attributes, "attribute_specifier") ||
            $::meta::child_count(attributes) != 5uptr) return $::quote { public_schema_failure; };
        $::meta::syntax attribute = $::meta::child(attributes, 1uptr);
        if (!$::meta::is_production(attribute, "attribute") ||
            $::meta::child_count(attribute) != 4uptr) return $::quote { public_schema_failure; };
        $::meta::syntax arguments = $::meta::child(attribute, 2uptr);
        if (!$::meta::is_production(arguments, "balanced_token_sequence") ||
            $::meta::child_count(arguments) != 4uptr) return $::quote { public_schema_failure; };
        for (uptr at = 0uptr; at < 4uptr; ++at)
            if (!$::meta::is_kind($::meta::child(arguments, at), "token"))
                return $::quote { public_schema_failure; };
        return $::meta::tokens(body);
    }
    syntax ParsedGeneric : item { prefix "parsed_generic"; match body:function_def; expand parsed_generic; }
    [[syntax_expander]] static $::meta::tokens drop_generic(in $::meta::syntax_match input) {
        $::meta::syntax attributes = $::meta::child($::syntax::node(input, "body"), 0uptr);
        $::meta::syntax attribute = $::meta::child(attributes, 1uptr);
        $::meta::syntax arguments = $::meta::child(attribute, 2uptr);
        if (!$::meta::is_production(arguments, "balanced_token_sequence") ||
            $::meta::child_count(arguments) != 5uptr) return $::quote { public_schema_failure; };
        $::meta::syntax group = $::meta::child(arguments, 3uptr);
        if (!$::meta::is_production(group, "balanced_token_tree") ||
            $::meta::child_count(group) != 3uptr) return $::quote { public_schema_failure; };
        $::meta::syntax contents = $::meta::child(group, 1uptr);
        if (!$::meta::is_production(contents, "balanced_tokens") ||
            $::meta::child_count(contents) != 2uptr) return $::quote { public_schema_failure; };
        return $::quote {};
    }
    syntax DropGeneric : item { prefix "drop_generic"; match body:function_def; expand drop_generic; }
    [[syntax_expander]] static $::meta::tokens parsed_label(in $::meta::syntax_match input) {
        $::meta::syntax ordinary = $::meta::child($::syntax::node(input, "body"), 0uptr);
        $::meta::syntax form = $::meta::child(ordinary, 0uptr);
        if (!$::meta::is_production(form, "labeled_statement") ||
            $::meta::child_count(form) != 4uptr ||
            !$::meta::is_production($::meta::child(form, 3uptr), "statement"))
            return $::quote { public_schema_failure(); };
        return $::meta::tokens($::syntax::node(input, "body"));
    }
    syntax ParsedLabel : statement { prefix "parsed_label"; match body:stmt; expand parsed_label; }
    [[syntax_expander]] static $::meta::tokens parsed_goto(in $::meta::syntax_match input) {
        $::meta::syntax ordinary = $::meta::child($::syntax::node(input, "body"), 0uptr);
        $::meta::syntax form = $::meta::child(ordinary, 0uptr);
        if (!$::meta::is_production(form, "jump_statement") ||
            $::meta::child_count(form) != 3uptr ||
            !$::meta::is_production($::meta::child(form, 1uptr), "assignment_expression"))
            return $::quote { public_schema_failure(); };
        return $::meta::tokens($::syntax::node(input, "body"));
    }
    syntax ParsedGoto : statement { prefix "parsed_goto"; match body:stmt; expand parsed_goto; }
}
namespace caller {
    global u32 number = 13u32;
    [[syntax_expander]] static $::meta::tokens hundred(in $::meta::syntax_match input) {
        return $::quote { 100u32 };
    }
    syntax Base : expression { prefix "base"; match body:paren; expand hundred; }
}
namespace forwarding {
    syntax flow::Discard;
    [[macro]] static $::meta::tokens opaque(in $::meta::tokens input) {
        return $::quote { discard { syntax is DSL; [[syntax_expander]] not_a_declaration; } };
    }
}

syntax(flow::Make, flow::Drop, flow::DropTokens) {
    make generated 7u32;
    drop { no_such_macro!(); [[syntax_expander]] not_a_declaration; }
    drop_tokens [[syntax_expander]] not_a_declaration;
}
typedef u32 RawResult;
syntax flow::CopyFunction, flow::DropFunction;
syntax flow::ParsedType;
emit_type u32;
syntax flow::ParsedDecl, flow::ParsedPrototype, flow::ParsedDefinition, flow::ParsedHeader, flow::ParsedArray;
syntax flow::ParsedGeneric, flow::DropGeneric;
parsed_decl global u32 parsed_state = 19u32;
parsed_array global u32 parsed_array[3] = { [0] = 5u32, [2] = 7u32 };
parsed_generic [[generic(T, u32 count), noinline]] static T parsed_generic(in T value) {
    return value + count;
}
drop_generic [[generic(T, u32 (*callback)(in u32 value))]] static T discarded_generic(in T value) {
    return value;
}
parsed_prototype static RawResult parsed_function(in RawResult value);
parsed_definition [[noinline]] static RawResult parsed_function(in RawResult value) {
    RawResult total = 0u32;
    for (u32 at = 0u32; at < value; ++at) total += 3u32;
    return total;
}
parsed_header [[noinline]] static RawResult header_function(in RawResult value) {
    return copied! (value) + 2u32;
}
syntax flow::DropParsed, flow::Explode;
drop_parsed no_such_macro! { owner drops this before lookup; };
drop_parsed explode ();
drop_parsed unexecuted::<nested::<3u32>>;
copy_fn [[noinline]] static RawResult copied_function(in u32 value) { return forwarded! (value) + 17u32; }
drop_fn static u32 discarded_function(in u32 value) { no_such_macro!(); this is a foreign body; }
namespace reopened {
    syntax flow::Chain;
    static u32 first() { return chain (2u32); }
}
namespace reopened {
    static u32 second() { u32 chain = 3u32; return chain + 1u32; }
}
namespace imported_one {
    syntax Choice : expression { prefix "choose"; match body:paren; expand flow::five; }
}
namespace imported_two {
    syntax Choice : expression { prefix "choose"; match body:paren; expand caller::hundred; }
}
using imported_one;
[[noinline]] static u32 imports() {
    syntax Choice;
    { using imported_two;
      syntax Choice as other;
      return choose () + other ();
    }
}
[[syntax_expander]] static $::meta::tokens stable(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Scalar : rule { match value:literal; }
namespace frozen {
    syntax Value : expression { prefix "hold"; match "(" child:rule(Scalar) ")"; expand stable; }
    syntax Value;
    syntax Scalar : rule { match word:ident; }
    [[syntax_expander]] static $::meta::tokens stable(in $::meta::syntax_match input) {
        return $::quote { 2u32 };
    }
    [[noinline]] static u32 result() { return hold (3u32); }
}
[[noinline]] static u32 dangling(in u32 value) {
    syntax flow::Run;
    if (value) run { if (value == 1u32) return 3u32; }
    else return 5u32;
    return 7u32;
}
[[noinline]] static u32 lexical(in u32 input) {
    u32 plus = input;
    u32 total = 0u32;
    { syntax flow::Tools, caller::Base;
      discard { no_such_macro!(); [[syntax_expander]] not_a_declaration; }
      discard [ ( { } ) ];
      forwarding::opaque!();
      run { total += plus (2u32) * 3u32; }
      total += plus (base ()) + copied! (2u32);
      total += sum (3u32 : 4u32);
    }
    return total + plus;
}
[[noinline]] static u32 returned() {
    syntax flow::Ret;
    ret copied! (3u32) + (2u32 * 4u32);
}
[[noinline]] static u32 primitives() {
    syntax flow::Lit, flow::Name, flow::Bracket, flow::Bang;
    return lit 3u32 + named caller::number + bracket [raw, (data)] +
        (lit 1.5f32 == 1.5f32 ? 1u32 : 0u32) + (lit 'a' == 97 ? 1u32 : 0u32) +
        (lit "hi"[0] == 104 ? 1u32 : 0u32) + bang ! 2u32;
}
[[noinline]] static u32 macro_statements() {
    u32 value = 1u32;
    copied! (value) += 2u32;
    forwarded! (value) += 3u32;
    forwarded! { value += 4u32; }
    forwarded! { return value; }
}
[[noinline]] static u32 raw_groups() {
    syntax flow::RawParen, flow::RawBracket, flow::RawBlock, flow::RawGroup, flow::RawProject, flow::RawRecord;
    if (raw_paren (alien! { [[arbitrary]] [x] () } tail) != 31u32) return 0u32;
    if (raw_bracket [alien! { [[arbitrary]] [x] () } tail] != 31u32) return 0u32;
    if (raw_block {alien! { [[arbitrary]] [x] () } tail} != 31u32) return 0u32;
    if (raw_group [[alien! { [[arbitrary]] [x] () } tail]] != 31u32) return 0u32;
    if (raw_record (5u32 + 6u32) != 11u32) return 0u32;
    return raw_project (3u32 + 4u32) * 2u32;
}
[[generic(u32 number), noinline]] static u32 actual() { return number; }
[[noinline]] static u32 angle_boundary() {
    syntax flow::Greater;
    return actual<greater ()>();
}
[[noinline]] static u32 combinators() {
    syntax flow::Maybe, flow::List, flow::ListOne, flow::Repeat, flow::RepeatZero, flow::Select;
    return maybe () + maybe (3u32) + list () + list (4u32) +
           list (2u32, 5u32) + list_one (8u32) + repeat (a a) +
           repeat_zero () + repeat_zero (a a) +
           select (left 6u32) + select (right 7u32);
}
[[noinline]] static u32 recursive_rules() {
    syntax flow::TreeUse, flow::MutualUse;
    return tree ((1u32 + 2u32) + 3u32) + mutual ([1u32]);
}
[[noinline]] static u32 parsed_expression() {
    syntax flow::ParsedExpr, flow::ParsedMacro, flow::ParsedList, flow::Base, flow::GenericShape;
    if ((parsed_macro copied! (9u32)) != 9u32) return 0u32;
    if ((parsed base () + 3u32) != 8u32) return 0u32;
    if (parsed_list (2u32 + 3u32, 4u32 * 5u32) != 25u32) return 0u32;
    if ((generic_shape unexecuted::<nested::<3u32>>) != 23u32) return 0u32;
    if ((generic_shape unexecuted::<nested::<inner::<3u32>>>) != 23u32) return 0u32;
    return parsed (2u32 + 3u32) * 4u32;
}
[[noinline]] static u32 parsed_statements(in u32 value) {
    syntax flow::ParsedStmt, flow::Run, flow::DropStmt, flow::ExplodeStmt, flow::ParsedLabel, flow::ParsedGoto;
    u32 total = 0u32;
    drop_stmt no_such_macro! { opaque foreign body; }
    drop_stmt explode_stmt { deliberately not executed; }
    parsed_stmt run { total += 0u32; }
    parsed_stmt forwarded! { total += 0u32; }
    parsed_stmt { copied! (total) += 0u32; }
    parsed_stmt for (u32 at = 0u32; at < 2u32; ++at) total += 0u32;
    parsed_stmt for ([[aligned(4)]] u32 at = 0u32; at < 2u32; ++at) total += 0u32;
    parsed_stmt for (total += 0u32; total < 0u32;) ++total;
    parsed_stmt for (; total < 0u32;) ++total;
    parsed_goto goto after_capture;
    total = 99u32;
    parsed_label label after_capture: total += 0u32;
    parsed_label label declare_cell: u32 from_label = value + 1u32;
    total += from_label - value - 1u32;
    parsed_stmt if (value) { total += 3u32; } else { total += 5u32; }
    parsed_stmt { label next: total += 7u32; }
    parsed_stmt while (total < 10u32) ++total;
    parsed_stmt return total;
}
syntax flow::Width;
global uptr syntax_width = width ();
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
#ifndef SYNTAX_COMPILE_ONLY
    if (generated() != 7u32) return 0u32;
    if (copied_function(4u32) != 21u32) return 0u32;
    if (reopened::first() != 7u32 || reopened::second() != 4u32) return 0u32;
    if (dangling(0u32) != 5u32 || dangling(1u32) != 3u32 || dangling(2u32) != 7u32) return 0u32;
    if (lexical(1u32) != 136u32 || returned() != 11u32) return 0u32;
    if (imports() != 105u32 || frozen::result() != 1u32) return 0u32;
    if (primitives() != 26u32) return 0u32;
    if (macro_statements() != 10u32) return 0u32;
    if (raw_groups() != 14u32) return 0u32;
    if (angle_boundary() != 9u32) return 0u32;
    if (combinators() != 50u32) return 0u32;
    if (recursive_rules() != 27u32) return 0u32;
    if (parsed_expression() != 20u32 || sizeof(CapturedType) != 4uptr) return 0u32;
    if (parsed_function(4u32) != 12u32 || parsed_state != 19u32 ||
        parsed_generic<u32, 4u32>(3u32) != 7u32 ||
        parsed_array[0] != 5u32 || parsed_array[1] != 0u32 || parsed_array[2] != 7u32 ||
        header_function(4u32) != 6u32 || parsed_statements(0u32) != 12u32 ||
        parsed_statements(1u32) != 10u32) return 0u32;
    if (syntax_width != sizeof(uptr)) return 0u32;
#endif
    return 61u32;
}

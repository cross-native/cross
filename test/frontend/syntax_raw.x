// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if !$::has_attribute(syntax_expander) || !$::has_builtin($::syntax::capture) || !$::has_builtin($::syntax::is_variant) || !$::has_intrinsic($::syntax::is_variant) || !$::has_builtin($::syntax::node) || !$::has_intrinsic($::meta::child) || !$::has_builtin($::meta::is_extension) || !$::has_intrinsic($::meta::is_extension) || !$::has_builtin($::syntax::span) || !$::has_intrinsic($::syntax::capture_span) || !$::has_builtin($::meta::node_span) || !$::has_intrinsic($::syntax::warning) || !$::has_builtin($::syntax::error) || !$::has_builtin($::syntax::note)
#error implemented syntax operations must be discoverable
#endif
#if $::has_feature($::feature::syntax_extensions)
#error full public-tree syntax support is not implemented yet
#endif
#if !$::has_builtin($::meta::replace_child) || !$::has_intrinsic($::meta::replace_child)
#error syntax child replacement must be discoverable
#endif
#if !$::has_builtin($::meta::extension_match) || !$::has_intrinsic($::meta::extension_match)
#error preserved extension matches must be discoverable
#endif
#if !$::has_builtin($::syntax::context) || !$::has_intrinsic($::syntax::context)
#error opaque context access must be discoverable
#endif

[[macro]] static $::meta::tokens copied(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens forwarded(in $::meta::tokens input) {
    return $::quote { copied! { $::unquote(input) } };
}
namespace flow {
    [[syntax_expander]] static $::meta::tokens five(in $::meta::syntax_match input) {
        $::meta::syntax parsed = $::meta::parse("expr", $::quote { 5u32 },
                                               $::syntax::context(input));
        return $::meta::tokens(parsed);
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
        $::meta::syntax width_type = $::meta::parse("type", $::quote { uptr },
                                                   $::syntax::context(input));
        if (!$::meta::is_production(width_type, "type_name")) return $::quote { 0u32 };
        typedef uptr WidthVector [[vector_size(16)]];
        if (sizeof(WidthVector) != 16uptr) return $::quote { 0u32 };
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
        const $::meta::span original = $::syntax::span(input);
        const $::meta::context original_context = $::syntax::context(input);
        $::meta::context selected_context = 0u32 ? original_context : $::syntax::context(root);
        selected_context = 1u32 ? selected_context : original_context;
        $::meta::span selected = 1u32 ? $::syntax::capture_span(input, "body") : original;
        selected = $::meta::node_span(root);
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
        $::meta::context record_context = $::syntax::context(record);
        $::meta::span record_span = $::syntax::span(record);
        record_span = $::syntax::capture_span(input, "record");
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
        if ($::meta::child_count(body) != 3uptr ||
            !$::meta::is_production($::meta::child(body, 0uptr), "declaration_specifiers"))
            return $::quote { public_schema_failure; };
        $::meta::syntax list = $::meta::child(body, 1uptr);
        if (!$::meta::is_production(list, "init_declarator_list") ||
            $::meta::child_count(list) != 1uptr) return $::quote { public_schema_failure; };
        $::meta::syntax item = $::meta::child(list, 0uptr);
        if (!$::meta::is_production(item, "init_declarator") ||
            $::meta::child_count(item) != 3uptr) return $::quote { public_schema_failure; };
        $::meta::syntax declarator = $::meta::child(item, 0uptr);
        if (!$::meta::is_production(declarator, "declarator") ||
            $::meta::child_count(declarator) != 1uptr) return $::quote { public_schema_failure; };
        $::meta::syntax direct = $::meta::child(declarator, 0uptr);
        if (!$::meta::is_production(direct, "direct_declarator") ||
            $::meta::child_count(direct) != 2uptr) return $::quote { public_schema_failure; };
        $::meta::syntax suffix = $::meta::child(direct, 1uptr);
        if (!$::meta::is_production(suffix, "array_suffix") ||
            $::meta::child_count(suffix) != 3uptr ||
            !$::meta::is_production($::meta::child(suffix, 1uptr), "assignment_expression"))
            return $::quote { public_schema_failure; };
        $::meta::syntax initializer = $::meta::child(item, 2uptr);
        if (!$::meta::is_production(initializer, "initializer") ||
            $::meta::child_count(initializer) != 5uptr) return $::quote { public_schema_failure; };
        $::meta::syntax entry = $::meta::child(initializer, 1uptr);
        if (!$::meta::is_production(entry, "initializer_entry") ||
            $::meta::child_count(entry) != 3uptr) return $::quote { public_schema_failure; };
        $::meta::syntax designator = $::meta::child(entry, 0uptr);
        if (!$::meta::is_production(designator, "designator") ||
            $::meta::child_count(designator) != 3uptr ||
            !$::meta::is_production($::meta::child(designator, 1uptr), "constant_expression"))
            return $::quote { public_schema_failure; };
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

[[syntax_expander]] static $::meta::tokens replace_expression_child(in $::meta::syntax_match input) {
    $::meta::syntax before = $::syntax::node(input, "before");
    $::meta::syntax after = $::syntax::node(input, "after");
    $::meta::syntax updated = $::meta::replace_child(before, 0uptr,
        $::meta::child(after, 0uptr));
    return $::meta::tokens(updated);
}
syntax Rebuild : expression {
    prefix "rebuild"; match "(" before:expr "," after:expr ")";
    expand replace_expression_child;
}
syntax Rebuild;
[[syntax_expander]] static $::meta::tokens inspect_extension(in $::meta::syntax_match input) {
    $::meta::syntax root = $::syntax::node(input, "body");
    $::meta::syntax leaf = root;
    while ($::meta::is_kind(leaf, "core") && $::meta::child_count(leaf) == 1uptr)
        leaf = $::meta::child(leaf, 0uptr);
    if (!$::meta::is_extension(leaf, "flow::Base") ||
        $::meta::is_extension(leaf, "caller::Base"))
        return $::quote { public_schema_failure(); };
    $::meta::syntax_match nested = $::meta::extension_match(leaf);
    if ($::meta::len($::syntax::input(nested)) != 2uptr ||
        $::meta::len($::syntax::capture(nested, "body")) != 1uptr)
        return $::quote { public_schema_failure(); };
    return $::meta::tokens(root);
}
syntax InspectExtension : expression {
    prefix "inspect_extension"; match body:expr; expand inspect_extension;
}
syntax InspectExtension;

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
parsed_decl global u32 parsed_first = 3u32, parsed_second = 4u32;
parsed_decl enum InlineMode [[underlying(u16)]] { mode_first = 3u16, mode_second = 4u16 }
    inline_mode = mode_second, inline_mode_copy = mode_first;
parsed_decl typedef enum InlineAlias [[underlying(u16)]] { inline_alias_enumerator = 9u16 } InlineAliasType;
parsed_decl global InlineAliasType inline_alias_value = inline_alias_enumerator;
parsed_decl struct InlineRecord { u32 left, right; } inline_record = { 5u32, 6u32 };
parsed_decl typedef struct InlineAliasRecord { u16 value; } InlineRecordAlias;
parsed_decl global InlineRecordAlias inline_record_alias = { 7u16 };
parsed_decl union InlineUnion { u32 value; u16 halves[2]; } inline_union = { .value = 8u32 };
parsed_decl struct OuterInline { struct InnerInline { u16 value; } inner; } outer_inline = { { 9u16 } };
parsed_decl struct ParsedMembers { u32 *pointer, scalar; };
parsed_array global u32 parsed_array[3] = { [0] = 5u32, [2] = 7u32 };
parsed_generic [[generic(T, u32 count), noinline]] static T parsed_generic(in T value) {
    return value + count;
}
drop_generic [[generic(T, u32 (*callback)(in u32 value))]] static T discarded_generic(in T value) {
    return value;
}
namespace ReorderedGeneric {
    typedef u8 T;
    parsed_definition static T [[noinline, generic(T)]] interleaved(in T value) {
        return value + 1u32;
    }
    parsed_prototype static T trailing(in T value) [[generic(T), noinline]];
    parsed_definition static T trailing(in T value) [[generic(T), noinline]] {
        return value + 2u32;
    }
    parsed_header static T recursive(in T value, in u32 depth) [[noinline, generic(T)]] {
        if (depth == 0u32) return value;
        return recursive<T>(value + 1u32, depth - 1u32);
    }
    T after = 5u8;
}
static T discard_parsed_assertion<T>(in T value) {
    syntax flow::DropStmt;
    drop_stmt { $::static_assert(0u32, "discarded capture must not escape"); }
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
drop_parsed $::quote { syntax is a DSL; $::unquote(no_such_macro! { foreign input; }) };
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
[[noinline]] static u32 parsed_members() {
    u32 target = 6u32;
    u32 * [[address_space(0)]] const pointer = &target;
    struct ParsedMembers value;
    value.pointer = pointer;
    value.scalar = 9u32;
    return *value.pointer + value.scalar;
}
[[noinline]] static u32 parsed_declaration_lists(in u32 input) {
    syntax flow::ParsedStmt;
    parsed_stmt typedef u32 Word, *Pointer;
    parsed_stmt Word first = input + 1u32, second = first + 2u32;
    parsed_stmt Pointer p = &first, q = &second;
    *q += *p;
    for (Word at = 0u32, limit = 2u32; at < limit; ++at) second += at;
    { typedef u16 Word; Word inner = 7u16, next = inner + 1u16; second += next; }
    Word after = second;
    for (typedef u16 LoopWord; 0u32;) { LoopWord ignored; }
    return after;
}
[[noinline]] static u32 static_declaration_lists() {
    static u32 first = 1u32, second = 3u32;
    ++first;
    second += first;
    return second;
}
[[noinline]] static u32 declaration_list_vlas() {
    u32 bound = 1u32;
    u32 first[++bound], second[++bound];
    first[0] = 7u32;
    second[0] = 11u32;
    goto after_arrays;
    first[0] = 99u32;
    label after_arrays: ;
    return (u32)(sizeof(first) / sizeof(u32) + sizeof(second) / sizeof(u32)) +
        bound + first[0] + second[0];
}
[[eval_only]] static u32 evaluate_declaration_lists() {
    typedef u32 Word, *Pointer;
    Word first = 3u32, second = first + 4u32;
    Pointer p = &first, q = &second;
    *q += *p;
    return second;
}
global u32 evaluated_declaration_lists = evaluate_declaration_lists();
u32 global reordered_storage_value = 6u32;
u32 [[aligned(16)]] global interleaved_aligned = 3u32;
u32 typedef ReorderedWord, *ReorderedPointer;
typedef uptr WidthVector [[vector_size(16)]];
typedef iptr SignedWidthVector [[vector_size(16)]];
$::static_assert(sizeof(WidthVector) == 16uptr, "vector_size must use target uptr width");
$::static_assert(sizeof(SignedWidthVector) == 16uptr, "vector_size must use target iptr width");
[[noinline]] u32 static reordered_storage(in u32 input) {
    const static u32 fixed = 4u32;
    u32 register dynamic = input + fixed;
    return dynamic + reordered_storage_value;
}
[[noinline]] static u32 reordered_typedef(in u32 input) {
    u32 typedef Word, *Pointer;
    Word first = input + 1u32, second = first + 2u32;
    Pointer destination = &second;
    *destination += first;
    for (u16 typedef LoopWord; 0u32;) { LoopWord ignored; }
    return second;
}
typedef u32 ShadowName;
[[noinline]] static u32 alias_shadowing() {
    u32 ShadowName = 2u32;
    ShadowName += 3u32;
    { typedef u16 ShadowName; ShadowName middle = 4u16; if (middle != 4u16) return 0u32; }
    return ShadowName;
}
syntax flow::Width;
global uptr syntax_width = width ();
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
#ifndef SYNTAX_COMPILE_ONLY
    syntax flow::Base;
    if (rebuild (2u32 + 3u32, 7u32 * 4u32) != 28u32) return 0u32;
    if (rebuild (unknown_discarded_macro! { not source; }, copied! (9u32)) != 9u32)
        return 0u32;
    if (rebuild (1u32, base ()) != 5u32) return 0u32;
    if ((inspect_extension base ()) != 5u32) return 0u32;
    if (generated() != 7u32) return 0u32;
    if (((uptr)&interleaved_aligned & 15uptr) != 0uptr || interleaved_aligned != 3u32)
        return 0u32;
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
        parsed_first != 3u32 || parsed_second != 4u32 || parsed_members() != 15u32 ||
        inline_mode != mode_second || inline_mode_copy != mode_first ||
        sizeof(enum InlineMode) != 2uptr || sizeof(InlineAliasType) != 2uptr ||
        inline_alias_value != inline_alias_enumerator ||
        inline_record.left != 5u32 || inline_record.right != 6u32 ||
        sizeof(struct InlineRecord) != 8uptr ||
        inline_record_alias.value != 7u16 || sizeof(InlineRecordAlias) != 2uptr ||
        inline_union.value != 8u32 || sizeof(union InlineUnion) != 4uptr ||
        outer_inline.inner.value != 9u16 || sizeof(struct InnerInline) != 2uptr ||
        parsed_declaration_lists(3u32) != 19u32 || declaration_list_vlas() != 26u32 ||
        evaluated_declaration_lists != 10u32 || alias_shadowing() != 5u32 ||
        reordered_storage(3u32) != 13u32 || reordered_typedef(3u32) != 10u32 ||
        static_declaration_lists() != 5u32 || static_declaration_lists() != 8u32 ||
        parsed_generic<u32, 4u32>(3u32) != 7u32 ||
        ReorderedGeneric::interleaved<u32>(300u32) != 301u32 ||
        ReorderedGeneric::trailing(300u32) != 302u32 ||
        ReorderedGeneric::recursive<u32>(300u32, 3u32) != 303u32 ||
        sizeof(ReorderedGeneric::T) != 1uptr || ReorderedGeneric::after != 5u8 ||
        discard_parsed_assertion(311u32) != 311u32 ||
        parsed_array[0] != 5u32 || parsed_array[1] != 0u32 || parsed_array[2] != 7u32 ||
        header_function(4u32) != 6u32 || parsed_statements(0u32) != 12u32 ||
        parsed_statements(1u32) != 10u32) return 0u32;
    if (syntax_width != sizeof(uptr)) return 0u32;
#endif
    return 61u32;
}

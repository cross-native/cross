// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if !$::has_builtin($::meta::call_site) || !$::has_intrinsic($::meta::call_site) || !$::has_builtin($::meta::gensym) || !$::has_intrinsic($::meta::gensym)
#error identifier context and fresh identity operations must be discoverable
#endif

namespace GenericCopies {
    [[macro]] static $::meta::tokens introduce(in $::meta::tokens name) {
        return $::quote { typedef uptr $::unquote(name); };
    }
    [[generic(label Address), noinline]] static label identity() { return Address; }
    [[macro]] static $::meta::tokens fresh_storage(in $::meta::tokens name) {
        $::meta::tokens point = $::meta::gensym("point");
        return $::quote {
            $::unquote(point): static label $::unquote(name) = identity::<$::unquote(point)>();
        };
    }
    [[syntax_expander]] static $::meta::tokens duplicate(in $::meta::syntax_match input) {
        $::meta::syntax function = $::syntax::node(input, "body");
        return $::quote {
            namespace First { $::unquote(function) }
            namespace Second { $::unquote($::meta::tokens(function)) }
        };
    }
    syntax Duplicate : item { prefix "duplicate_generic"; match body:function_def; expand duplicate; }
    syntax Duplicate;
    // Structured and explicitly projected copies retain their generic binders,
    // but concrete instances and lifted storage belong to their own functions.
    duplicate_generic [[noinline]] static uptr value<T, uptr Count>(in uptr seed) {
        introduce!(Word)
        static uptr counter = Count;
        counter += 1uptr;
        if (seed == 0uptr) return Count;
        Word total = value::<T, Count>(0uptr);
        { T Count = (T)seed; total += (uptr)Count; }
        goto done;
        total = 0uptr;
    done:
        return total + counter + sizeof(T);
    }
    duplicate_generic [[noinline]] static label address<T, uptr Count>() {
        introduce!(Word)
        Word keep = Count;
        goto point;
    point:
        static label saved = identity::<point>();
        fresh_storage!(fresh)
        return keep && saved == point ? fresh : (label)0uptr;
    }
    [[noinline]] static u32 check() {
        if (First::value::<u16, 7uptr>(300uptr) != 318uptr ||
            Second::value::<u16, 7uptr>(300uptr) != 318uptr ||
            First::value::<u32, 11uptr>(400uptr) != 428uptr ||
            Second::value::<u32, 11uptr>(400uptr) != 428uptr ||
            First::value::<u16, 7uptr>(300uptr) != 320uptr ||
            Second::value::<u32, 11uptr>(400uptr) != 430uptr) return 0u32;
        label a = First::address::<u16, 7uptr>();
        label b = Second::address::<u16, 7uptr>();
        label c = First::address::<u32, 11uptr>();
        label d = Second::address::<u32, 11uptr>();
        return a != (label)0uptr && b != (label)0uptr && c != (label)0uptr && d != (label)0uptr &&
            a != b && a != c && a != d && b != c && b != d && c != d &&
            a == First::address::<u16, 7uptr>() && d == Second::address::<u32, 11uptr>();
    }
}

[[syntax_expander]] static $::meta::tokens relocate(in $::meta::syntax_match input) {
    return $::quote {
        {
            u32 $::unquote($::syntax::capture(input, "name")) = 99u32;
            return $::unquote($::meta::tokens($::syntax::node(input, "value")));
        }
    };
}
syntax Relocate : statement {
    prefix "relocate"; match name:ident "," value:expr ";"; expand relocate;
}
[[syntax_expander]] static $::meta::tokens surround(in $::meta::syntax_match input) {
    return $::quote {
        {
            u32 $::unquote($::syntax::capture(input, "name")) = 99u32;
            $::unquote($::meta::tokens($::syntax::node(input, "body")))
        }
    };
}
syntax Surround : statement {
    prefix "surround"; match name:ident "," body:stmt; expand surround;
}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::meta::tokens($::syntax::node(input, "body"));
}
syntax Copy : statement { prefix "copied"; match body:stmt; expand copy; }

[[syntax_expander]] static $::meta::tokens choose_call_site(in $::meta::syntax_match input) {
    return $::meta::call_site($::meta::parse("outside"));
}
syntax ChooseCallSite : expression {
    prefix "callsite"; match body:paren; expand choose_call_site;
}
[[syntax_expander]] static $::meta::tokens choose_definition(in $::meta::syntax_match input) {
    return $::meta::parse("outside");
}
syntax ChooseDefinition : expression {
    prefix "defsite"; match body:paren; expand choose_definition;
}
[[syntax_expander]] static $::meta::tokens make_fresh_pair(in $::meta::syntax_match input) {
    $::meta::tokens first = $::meta::gensym("fresh");
    $::meta::tokens second = $::meta::gensym("fresh");
    return $::quote {{
        u32 $::unquote(first) = 7u32;
        u32 $::unquote(second) = 11u32;
        return $::unquote(first) * 10u32 + $::unquote(second);
    }};
}
syntax FreshPair : statement {
    prefix "fresh_pair"; match body:paren; expand make_fresh_pair;
}
[[macro]] static $::meta::tokens choose_macro_site(in $::meta::tokens input) {
    return $::meta::call_site($::meta::parse("outside"));
}

global u32 outside = 23u32;
[[noinline]] static u32 parameter(in u32 value) {
    syntax Relocate;
    relocate value, value;
}
[[noinline]] static u32 local() {
    syntax Relocate;
    u32 value = 31u32;
    relocate value, value;
}
[[noinline]] static u32 nonlocal() {
    syntax Relocate;
    relocate outside, outside;
}
[[noinline]] static u32 scoped() {
    syntax Relocate;
    u32 value = 7u32;
    {
        u32 value = 11u32;
        relocate value, value;
    }
}
[[noinline]] static u32 modified() {
    syntax Surround;
    u32 value = 4u32;
    surround value, value += 3u32;
    return value;
}
[[noinline]] static u32 copyout(inout u32 value) {
    syntax Surround;
    surround value, value += 2u32;
    return value;
}
[[noinline]] static u32 interior() {
    syntax Copy;
    u32 value = 3u32, result = 0u32;
    copied {
        u32 value = 13u32;
        result = value;
    }
    return result + value;
}
[[noinline]] static T generic<T, u32 amount>(in T value) {
    syntax Surround;
    surround value, value += amount;
    return value;
}
static u32 ordinary_shadow() {
    u32 value = 5u32, result = 0u32;
    { u32 value = 17u32; result = value; }
    return result + value;
}
[[noinline]] static u32 fresh_pair_result() {
    syntax FreshPair;
    u32 fresh = 99u32;
    fresh_pair ();
}

// Labels have a function-wide namespace, but separately quoted expansions
// must not collide or capture copied input labels with the same spelling.
[[macro]] static $::meta::tokens quoted_step(in $::meta::tokens input) {
    return $::quote {
        u32 remaining = 2u32;
        goto again;
    again:
        $::unquote(input)
        if (--remaining) goto again;
    };
}
[[macro]] static $::meta::tokens label_surround(in $::meta::tokens input) {
    return $::quote {
        goto start;
    next: return 99u32;
    start: $::unquote(input)
    };
}
[[macro]] static $::meta::tokens label_call_site(in $::meta::tokens input) {
    return $::quote {
        goto $::unquote($::meta::call_site($::meta::parse("next")));
    next: return 99u32;
    };
}
[[macro]] static $::meta::tokens fresh_label_step(in $::meta::tokens input) {
    $::meta::tokens first = $::meta::gensym("point");
    $::meta::tokens second = $::meta::gensym("point");
    return $::quote {
        goto $::unquote(second);
    $::unquote(first): return 99u32;
    $::unquote(second): $::unquote(input)
    };
}
[[syntax_expander]] static $::meta::tokens label_jump(in $::meta::syntax_match input) {
    return $::quote {
        {
            // The owner and final label component have different origins.
            static label target = $::unquote($::syntax::capture(input, "owner"))::next;
            $::unquote($::syntax::node(input, "destination")) = target;
            goto target;
        next:
            $::unquote($::syntax::node(input, "body"))
        }
    };
}
syntax LabelJump : statement {
    prefix "label_jump";
    match owner:ident "," destination:expr "," body:stmt;
    expand label_jump;
}
[[noinline]] static u32 quoted_labels() {
    u32 value = 0u32;
    quoted_step! { value += 1u32; }
    quoted_step! { value += 3u32; }
    fresh_label_step! { value += 5u32; }
    fresh_label_step! { value += 7u32; }
    goto again;
again: return value;
}
[[noinline]] static u32 copied_label_goto() {
    label_surround! { goto next; }
next: return 7u32;
}
[[noinline]] static u32 explicit_label_goto() {
    label_call_site! {}
next: return 11u32;
}
[[noinline]] static u32 label_or_object(in bool computed) {
    label next = label_or_object::other;
    if (computed) goto (next);
    goto next;
next: return 3u32;
other: return 7u32;
}
[[noinline]] static u32 quoted_label_addresses() {
    syntax LabelJump;
    u32 value = 0u32;
    label first, second;
    label_jump quoted_label_addresses, first, value += 3u32;
    label_jump quoted_label_addresses, second, value += 5u32;
    if (first == second) return 99u32;
    return value;
}
namespace LabelNames {
    [[noinline]] static u32 owner() { point: return 13u32; }
    static label saved = owner::point;
    [[noinline]] static u32 check() {
        return saved == owner::point ? owner() : 99u32;
    }
}
[[generic(label Address), noinline]] static label private_label_identity() { return Address; }
struct PrivateLabelBox { label first; label second; };
static struct PrivateLabelBox private_label_box(in label first, in label second) {
    struct PrivateLabelBox result = {first, second};
    return result;
}
static label choose_private_label(in bool choose, in label first, in label second) {
    struct PrivateLabelBox values = private_label_box(first, second);
    label result = choose ? values.first : values.second;
    return result;
}
[[noinline]] static u32 private_label_owner(in bool choose) {
    if (choose) goto second;
first: return 3u32;
second: return 5u32;
}
[[noinline]] static u32 private_label_generics() {
    label first = private_label_identity::<private_label_owner::first>();
    label second = private_label_identity::<private_label_owner::second>();
    return first == private_label_owner::first && second == private_label_owner::second && first != second;
}
[[syntax_expander]] static $::meta::tokens move_labeled_body(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    return $::quote { [[noinline]] static u32 moved_labeled_body() $::unquote(block) };
}
syntax MoveLabeledBody : item {
    prefix "move_labeled_body"; match body:function_def; expand move_labeled_body;
}
syntax MoveLabeledBody;
move_labeled_body static u32 original_labeled_body() { goto point; point: return 7u32; }

[[syntax_expander]] static $::meta::tokens join_labeled_bodies(in $::meta::syntax_match input) {
    $::meta::syntax first = $::syntax::node(input, "first");
    $::meta::syntax second = $::syntax::node(input, "second");
    return $::quote { [[noinline]] static u32 joined_labeled_bodies() {
        u32 value = 0u32;
        $::unquote($::meta::child(first, $::meta::child_count(first) - 1uptr))
        value += 3u32;
        $::unquote($::meta::child(second, $::meta::child_count(second) - 1uptr))
        value += 5u32;
        return value;
    }};
}
syntax JoinLabeledBodies : item {
    prefix "join_labeled_bodies";
    match first:function_def second:function_def;
    expand join_labeled_bodies;
}
syntax JoinLabeledBodies;
join_labeled_bodies static void first_body() { goto point; point: ; }
                    static void second_body() { goto point; point: ; }

[[noinline]] static label qualified_retarget_owner() {
point: return qualified_retarget_owner::point;
}
[[syntax_expander]] static $::meta::tokens join_qualified_labels(in $::meta::syntax_match input) {
    $::meta::syntax a = $::syntax::node(input, "a");
    $::meta::syntax b = $::syntax::node(input, "b");
    $::meta::syntax a_body = $::meta::child(a, $::meta::child_count(a) - 1uptr);
    $::meta::syntax b_body = $::meta::child(b, $::meta::child_count(b) - 1uptr);
    $::meta::syntax a_label = $::meta::child(a_body, 2uptr);
    $::meta::syntax b_label = $::meta::child(b_body, 1uptr);
    $::meta::tokens a_name = $::meta::slice($::meta::tokens($::meta::child(a_body, 1uptr)), 1uptr, 1uptr);
    $::meta::tokens b_name = $::meta::slice($::meta::tokens(b_label), 0uptr, 1uptr);
    return $::quote {
        static label copied_qualified_addresses[2] = {
            joined_qualified_labels::$::unquote(a_name),
            joined_qualified_labels::$::unquote(b_name)
        };
        static struct PrivateLabelBox copied_qualified_box = private_label_box(
            joined_qualified_labels::$::unquote(a_name), joined_qualified_labels::$::unquote(b_name));
        [[noinline]] static u32 joined_qualified_labels() {
            volatile u32 position = 0u32;
            // Both source labels spell point without an expansion mark. Only
            // their retained source scopes/declaration identities differ.
            $::unquote(a_label)
            position = 1u32;
            $::unquote($::meta::tokens(b_label))
            position = 2u32;
            label a = joined_qualified_labels::$::unquote(a_name);
            label b = joined_qualified_labels::$::unquote(b_name);
            label ga = private_label_identity::<joined_qualified_labels::$::unquote(a_name)>();
            label gb = private_label_identity::<joined_qualified_labels::$::unquote(b_name)>();
            label conditional = private_label_identity::<(1u32 ? joined_qualified_labels::$::unquote(a_name)
                : joined_qualified_labels::$::unquote(b_name))>();
            label helper = private_label_identity::<choose_private_label((bool)0u32,
                joined_qualified_labels::$::unquote(a_name), joined_qualified_labels::$::unquote(b_name))>();
            label retargeted = qualified_retarget_owner::$::unquote($::meta::call_site(b_name));
            return position == 2u32 && a != b && ga == a && gb == b && conditional == a && helper == b &&
                copied_qualified_addresses[0] == a && copied_qualified_addresses[1] == b &&
                copied_qualified_box.first == a && copied_qualified_box.second == b &&
                retargeted == qualified_retarget_owner();
        }
    };
}
syntax JoinQualifiedLabels : item {
    prefix "join_qualified_labels"; match a:function_def b:function_def; expand join_qualified_labels;
}
syntax JoinQualifiedLabels;
join_qualified_labels static void first_qualified_body() { goto point; point: ; }
                      static void second_qualified_body() { point: ; }

[[syntax_expander]] static $::meta::tokens retarget_label(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    $::meta::tokens jump = $::meta::tokens($::meta::child(block, 1uptr));
    $::meta::tokens name = $::meta::call_site($::meta::slice(jump, 1uptr, 1uptr));
    return $::quote { [[noinline]] static u32 retargeted_label() {
        goto $::unquote(name); $::unquote(name): return 13u32;
    }};
}
syntax RetargetLabel : item {
    prefix "retarget_label"; match body:function_def; expand retarget_label;
}
syntax RetargetLabel;
retarget_label static u32 original_target() { goto point; point: return 99u32; }

[[syntax_expander]] static $::meta::tokens reparse_label(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "source");
    $::meta::syntax target = $::syntax::node(input, "target");
    $::meta::syntax source_block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    $::meta::syntax target_block = $::meta::child(target, $::meta::child_count(target) - 1uptr);
    $::meta::syntax target_label = $::meta::child(target_block, 1uptr);
    $::meta::syntax jump = $::meta::parse("stmt",
        $::meta::tokens($::meta::child(source_block, 1uptr)),
        $::syntax::context(target_label));
    return $::quote { [[noinline]] static u32 reparsed_label() {
        $::unquote(jump) $::unquote(target_label)
    }};
}
syntax ReparseLabel : item {
    prefix "reparse_label"; match source:function_def target:function_def; expand reparse_label;
}
syntax ReparseLabel;
reparse_label static u32 source_target() { goto point; point: return 99u32; }
              static u32 destination_target() { point: return 17u32; }

[[syntax_expander]] static $::meta::tokens hold_labeled_stmt(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax HoldLabeledStmt : statement {
    prefix "hold_labeled_stmt"; match body:stmt; expand hold_labeled_stmt;
}
syntax HoldLabeledStmt;
[[syntax_expander]] static $::meta::tokens move_nested_label(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    $::meta::syntax statement = $::meta::child(block, 1uptr);
    $::meta::syntax extension = $::meta::child($::meta::child(statement, 0uptr), 0uptr);
    $::meta::syntax jump = $::syntax::node($::meta::extension_match(extension), "body");
    return $::quote { [[noinline]] static u32 moved_nested_label() {
        $::unquote(jump) $::unquote($::meta::child(block, 2uptr))
    }};
}
syntax MoveNestedLabel : item {
    prefix "move_nested_label"; match body:function_def; expand move_nested_label;
}
syntax MoveNestedLabel;
move_nested_label static u32 original_nested() {
    hold_labeled_stmt goto point;
point: return 19u32;
}
[[macro]] static $::meta::tokens hold_labeled_tokens(in $::meta::tokens input) {
    return input;
}
[[syntax_expander]] static $::meta::tokens move_raw_labeled_body(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    return $::quote { [[noinline]] static u32 moved_raw_labeled_body()
        $::unquote($::meta::child(source, $::meta::child_count(source) - 1uptr)) };
}
syntax MoveRawLabeledBody : item {
    prefix "move_raw_labeled_body"; match body:function_def; expand move_raw_labeled_body;
}
syntax MoveRawLabeledBody;
move_raw_labeled_body static u32 original_raw_labels() {
    hold_labeled_tokens! { goto point; }
point: return 23u32;
}
[[macro]] static $::meta::tokens introduce_label_word(in $::meta::tokens input) {
    return $::quote { typedef u32 $::unquote(input); };
}
[[macro]] static $::meta::tokens introduce_label(in $::meta::tokens input) {
    return $::quote { $::unquote(input): ; };
}
static $::meta::tokens emit_dynamic_labeled_body(in $::meta::syntax_match input, in bool deferred) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax body = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    if (deferred && !$::meta::is_kind(body, "deferred"))
        $::syntax::error($::syntax::span(input), "expected a wholly deferred labeled body");
    return $::quote { [[noinline]] static u32 $::unquote($::syntax::capture(input, "name"))()
        $::unquote(body) };
}
[[syntax_expander]] static $::meta::tokens move_deferred_labels(in $::meta::syntax_match input) {
    return emit_dynamic_labeled_body(input, 1);
}
[[syntax_expander]] static $::meta::tokens move_generated_labels(in $::meta::syntax_match input) {
    return emit_dynamic_labeled_body(input, 0);
}
syntax MoveDeferredLabels : item {
    prefix "move_deferred_labels"; match name:ident body:function_def; expand move_deferred_labels;
}
syntax MoveGeneratedLabels : item {
    prefix "move_generated_labels"; match name:ident body:function_def; expand move_generated_labels;
}
syntax MoveDeferredLabels, MoveGeneratedLabels;
move_deferred_labels deferred_plain_labels static u32 original() {
    introduce_label_word!(Word)
    Word value = 29u32;
    goto point;
point: return value;
}
move_generated_labels generated_forward_labels static u32 original() {
    goto point;
    return 99u32;
    introduce_label!(point)
    return 31u32;
}
move_deferred_labels generated_deferred_labels static u32 original() {
    introduce_label_word!(Word)
    Word value = 37u32;
    goto point;
    return 99u32;
    introduce_label!(point)
    return value;
}
move_deferred_labels generated_backward_labels static u32 original() {
    introduce_label_word!(Word)
    Word value = 0u32;
    introduce_label!(point)
    value += 1u32;
    if (value != 3u32) goto point;
    return value;
}
// Splitting a function into independently retained header/body nodes must not
// discard the body's original generic declarations or callable component types.
static $::meta::tokens recompose_generic_output(in $::meta::syntax_match input, in bool textual) {
    $::meta::syntax source = $::syntax::node(input, "body");
    $::meta::syntax body = $::meta::child(source, $::meta::child_count(source) - 1uptr);
    $::meta::tokens tokens = $::meta::tokens(source);
    $::meta::syntax header = $::meta::parse("function_header",
        $::meta::slice(tokens, 0uptr, $::meta::len(tokens) - 1uptr), $::syntax::context(source));
    if (textual) return $::quote { $::unquote(header) $::unquote($::meta::tokens(body)) };
    return $::quote { $::unquote(header) $::unquote(body) };
}
[[syntax_expander]] static $::meta::tokens recompose_generic(in $::meta::syntax_match input) {
    return recompose_generic_output(input, 0);
}
[[syntax_expander]] static $::meta::tokens project_generic(in $::meta::syntax_match input) {
    return recompose_generic_output(input, 1);
}
syntax RecomposeGeneric : item {
    prefix "recompose_generic"; match body:function_def; expand recompose_generic;
}
syntax ProjectGeneric : item {
    prefix "project_generic"; match body:function_def; expand project_generic;
}
syntax RecomposeGeneric, ProjectGeneric;
recompose_generic [[generic(T), noinline]] static label recomposed_static_label() {
    static label selected = private_label_identity::<point>();
    static label direct = point;
    return selected == direct ? selected : (label)0uptr;
    point: ;
}
project_generic [[generic(T), noinline]] static label projected_static_label() {
    static label selected = private_label_identity::<point>();
    static label direct = point;
    return selected == direct ? selected : (label)0uptr;
    point: ;
}
recompose_generic [[generic(T), noinline]] static uptr generic_body_attribute() {
    T value = 0;
    return sizeof(value);
}
recompose_generic [[noinline]] static T generic_body_angle<T>(in T input) {
    typedef T Local;
    Local value = input;
    return value;
}
recompose_generic [[noinline]] static T generic_body_callable<T>(in T (*callback)(in T), in T input) {
    typedef T (*Callback)(in T);
    Callback local = callback;
    return local(input);
}
[[noinline]] static u32 generic_body_callback(in u32 input) { return input + 3u32; }
project_generic [[noinline]] static T generic_projected<T>(in T input) {
    typedef T Local;
    Local value = input;
    return value;
}
recompose_generic [[noinline]] static T generic_body_indirect<T, F>(in F callback, in u32 input) {
    return callback(input);
}
project_generic [[noinline]] static u32 projected_raw_parameter(in u32 value) {
    return hold_labeled_tokens!(value);
}
project_generic [[noinline]] static u32 projected_raw_generic<u32 value>() {
    return hold_labeled_tokens!(value);
}
project_generic [[noinline]] static u32 projected_deferred_shadow(in u32 value) {
    introduce_label_word!(Word)
    { Word value = 23u32; return hold_labeled_tokens!(value); }
}
project_generic [[noinline]] static u32 projected_deferred_generic_shadow<u32 value>() {
    introduce_label_word!(Word)
    { Word value = 29u32; return hold_labeled_tokens!(value); }
}
project_generic [[noinline]] static u32 projected_deferred_parameter(in u32 value) {
    introduce_label_word!(Word)
    Word copy = hold_labeled_tokens!(value);
    return copy;
}
[[macro]] static $::meta::tokens explicit_raw_local(in $::meta::tokens input) {
    $::meta::tokens name = $::meta::call_site($::meta::parse("temporary"));
    return $::quote { { u32 $::unquote(name) = 31u32; return $::unquote(name); } };
}
[[noinline]] static u32 generated_raw_local() { explicit_raw_local! {} }
[[noinline]] static u32 projected_raw_local() {
    syntax Surround;
    u32 value = 43u32;
    surround value, { return hold_labeled_tokens!(value); }
}
project_generic [[noinline]] static u32 projected_deferred_for(in u32 value) {
    introduce_label_word!(Word)
    for (Word value = 47u32; value != 48u32; value += 1u32) {
        return hold_labeled_tokens!(value);
    }
    return 0u32;
}
namespace RawLookup {
    static u32 value = 13u32;
    [[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
        $::meta::syntax source = $::syntax::node(input, "body");
        $::meta::syntax body = $::meta::child(source, $::meta::child_count(source) - 1uptr);
        return $::quote {
            [[noinline]] static u32 $::unquote($::syntax::capture(input, "function"))(in u32 $::unquote($::syntax::capture(input, "name")))
            $::unquote($::meta::tokens(body))
        };
    }
    syntax Move : item { prefix "move"; match function:ident name:ident body:function_def; expand move; }
    [[macro]] static $::meta::tokens reset(in $::meta::tokens input) {
        return $::meta::call_site(input);
    }
    [[syntax_expander]] static $::meta::tokens move_generic(in $::meta::syntax_match input) {
        $::meta::syntax source = $::syntax::node(input, "body");
        $::meta::syntax body = $::meta::child(source, $::meta::child_count(source) - 1uptr);
        return $::quote {
            [[generic(u32 $::unquote($::syntax::capture(input, "name"))), noinline]] static u32 generic_nonlocal()
            $::unquote($::meta::tokens(body))
        };
    }
    syntax MoveGeneric : item { prefix "move_generic"; match name:ident body:function_def; expand move_generic; }
    syntax Move, MoveGeneric;
    move nonlocal value static u32 original() { return hold_labeled_tokens!(value); }
    move retargeted value static u32 original() { return reset!(value); }
    move_generic value static u32 original() { return hold_labeled_tokens!(value); }
}
[[syntax_expander]] static $::meta::tokens merge_raw_scopes(in $::meta::syntax_match input) {
    $::meta::syntax first = $::syntax::node(input, "first");
    $::meta::syntax compound = $::meta::child($::meta::child(first, 0uptr), 0uptr);
    return $::quote {
        $::unquote($::meta::tokens($::meta::child(compound, 0uptr)))
        $::unquote($::meta::child(compound, 1uptr))
        $::unquote($::meta::tokens($::syntax::node(input, "second")))
        $::unquote($::meta::tokens($::meta::child(compound, 2uptr)))
    };
}
syntax MergeRawScopes : statement { prefix "merge_raw_scopes"; match first:stmt second:stmt; expand merge_raw_scopes; }
[[noinline]] static u32 raw_sibling_scope(in u32 value) {
    syntax MergeRawScopes;
    merge_raw_scopes { u32 value = 9u32; } { return hold_labeled_tokens!(value); }
}
[[macro]] static $::meta::tokens raw_header_parameter(in $::meta::tokens input) {
    return $::quote { in u32 $::unquote(input) };
}
static $::meta::tokens raw_constructed_name() {
    return $::meta::call_site($::meta::parse("value"));
}
[[macro]] static $::meta::tokens raw_constructed_parameter(in $::meta::tokens input) {
    return $::quote { in u32 $::unquote(raw_constructed_name()) };
}
[[macro]] static $::meta::tokens raw_nested_parameter(in $::meta::tokens input) {
    return $::quote { raw_constructed_parameter!() };
}
[[macro]] static $::meta::tokens raw_constructed_generic(in $::meta::tokens input) {
    return $::quote { u32 $::unquote(raw_constructed_name()) };
}
[[syntax_expander]] static $::meta::tokens copy_deferred_function(in $::meta::syntax_match input) {
    $::meta::syntax source = $::syntax::node(input, "body");
    if (!$::meta::is_kind(source, "deferred"))
        $::syntax::error($::syntax::span(input), "expected a deferred whole function");
    return $::meta::tokens(source);
}
syntax CopyDeferredFunction : item {
    prefix "copy_deferred_function"; match body:function_def; expand copy_deferred_function;
}
syntax CopyDeferredFunction;
copy_deferred_function [[noinline]] static u32 raw_deferred_header(raw_header_parameter!(value)) {
    return hold_labeled_tokens!(value) + 1u32;
}
copy_deferred_function [[noinline]] static u32 raw_deferred_constructed(raw_constructed_parameter!()) {
    return hold_labeled_tokens!(value);
}
copy_deferred_function [[noinline]] static u32 raw_deferred_nested(raw_nested_parameter!()) {
    return hold_labeled_tokens!(value);
}
copy_deferred_function [[noinline]] static u32 raw_deferred_generic() [[generic(raw_constructed_generic!())]] {
    return hold_labeled_tokens!(value);
}
#ifdef CUSTOM_SYNTAX_ABI
struct GenericBodyResult { u64 first; u64 second; };
[[noinline, abi("stack_result_abi")]] static u32 generic_body_stack(in u32 input) {
    return input + 5u32;
}
[[noinline, abi("memory_result_abi")]] static struct GenericBodyResult generic_body_memory(in u32 input) {
    struct GenericBodyResult result = {(u64)input, (u64)input + 7u64};
    return result;
}
#endif
namespace ImportedLabels {
    using LabelNames;
    static label saved = owner::point;
    [[noinline]] static u32 check() {
        return saved == owner::point ? owner() : 99u32;
    }
}
#ifdef TEST_RAW_LABELS
[[macro]] static $::meta::tokens raw_label_step(in $::meta::tokens input) {
    return $::quote {
        $::_jmp(next);
    skipped:
        $::unquote(input) += 99u64;
    next:
        $::unquote(input) += 3u64;
    };
}
// The explicit result is deliberately not the host ABI's ordinary result.
[[naked, clobber("r8", "flags")]] static u64 raw_quoted_labels() -> "r8" {
    register u64 result "r8";
    result = 0u64;
    raw_label_step! { result }
    raw_label_step! { result }
    goto next;
next:
    $::_ret();
}
[[macro]] static $::meta::tokens raw_object_jump(in $::meta::tokens input) {
    return $::quote { goto ($::unquote(input)); };
}
[[naked, clobber("r8")]]
static u64 raw_label_object(in label next "r9") -> "r8" {
    register u64 result "r8";
    raw_object_jump! { next }
next:
    result = 3u64;
    $::_ret();
other:
    result = 7u64;
    $::_ret();
}
[[naked, clobber("r8")]]
static u64 raw_label_direct(in label next "r9") -> "r8" {
    register u64 result "r8";
    goto next;
next:
    result = 11u64;
    $::_ret();
other:
    result = 99u64;
    $::_ret();
}
// Code addresses do not request callable adapters for the owners' manual ABI.
static label raw_object_target = raw_label_object::other;
static label raw_targets[2] = { raw_label_object::other, raw_label_direct::other };
namespace RawLabelScope {
    [[macro]] static $::meta::tokens branch(in $::meta::tokens input) {
        return $::quote { $::_jmp(owner::other); };
    }
    [[naked, clobber("r8")]] static u64 owner() -> "r8" {
        register u64 result "r8";
        branch! {}
    next:
        result = 99u64;
        $::_ret();
    other:
        result = 19u64;
        $::_ret();
    }
}
#endif
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    syntax ChooseCallSite;
    syntax ChooseDefinition;
    u32 outside = 41u32;
    u32 value = 6u32;
    if (parameter(17u32) != 17u32 || local() != 31u32 || nonlocal() != 23u32 ||
        scoped() != 11u32 || modified() != 7u32 || copyout(value) != 8u32 ||
        value != 8u32 || interior() != 16u32 ||
        generic<u32, 3u32>(9u32) != 12u32 || ordinary_shadow() != 22u32 ||
        callsite () != 41u32 || defsite () != 23u32 ||
        choose_macro_site! {} != 41u32 || fresh_pair_result() != 81u32 ||
        quoted_labels() != 20u32 || copied_label_goto() != 7u32 ||
        explicit_label_goto() != 11u32 || quoted_label_addresses() != 8u32 ||
        label_or_object(0) != 3u32 || label_or_object(1) != 7u32 ||
        LabelNames::check() != 13u32 || ImportedLabels::check() != 13u32 || private_label_generics() != 1u32 ||
        moved_labeled_body() != 7u32 || joined_labeled_bodies() != 8u32 ||
        joined_qualified_labels() != 1u32 ||
        retargeted_label() != 13u32 || reparsed_label() != 17u32 ||
        moved_nested_label() != 19u32 || moved_raw_labeled_body() != 23u32 ||
        deferred_plain_labels() != 29u32 || generated_forward_labels() != 31u32 ||
        generated_deferred_labels() != 37u32 || generated_backward_labels() != 3u32 ||
        generic_body_attribute::<u8>() != 1uptr || generic_body_attribute::<u32>() != 4uptr ||
        recomposed_static_label::<u32>() == (label)0uptr ||
        recomposed_static_label::<u32>() == recomposed_static_label::<u64>() ||
        projected_static_label::<u32>() == (label)0uptr ||
        projected_static_label::<u32>() == projected_static_label::<u64>() ||
        generic_body_angle(41u32) != 41u32 || generic_projected(47u32) != 47u32 ||
        generic_body_callable::<u32>(generic_body_callback, 40u32) != 43u32 ||
        projected_raw_parameter(17u32) != 17u32 || projected_raw_generic::<19u32>() != 19u32 ||
        projected_deferred_shadow(7u32) != 23u32 || projected_deferred_generic_shadow::<11u32>() != 29u32 ||
        projected_deferred_parameter(37u32) != 37u32 || generated_raw_local() != 31u32 ||
        projected_raw_local() != 43u32 || projected_deferred_for(7u32) != 47u32 ||
        RawLookup::nonlocal(7u32) != 13u32 || RawLookup::retargeted(7u32) != 7u32 ||
        RawLookup::generic_nonlocal::<7u32>() != 13u32 ||
        raw_sibling_scope(53u32) != 53u32 || raw_deferred_header(58u32) != 59u32 ||
        raw_deferred_constructed(67u32) != 67u32 || raw_deferred_nested(71u32) != 71u32 ||
        raw_deferred_generic::<73u32>() != 73u32 || GenericCopies::check() != 1u32)
        return 0u32;
#ifdef CUSTOM_SYNTAX_ABI
    if (generic_body_indirect::<u32>(generic_body_stack, 48u32) != 53u32) return 0u32;
    struct GenericBodyResult result = generic_body_indirect::<struct GenericBodyResult>(generic_body_memory, 52u32);
    if (result.first != 52u64 || result.second != 59u64) return 0u32;
#endif
#ifdef TEST_RAW_LABELS
    if (raw_quoted_labels() != 6u64 ||
        raw_label_object(raw_object_target) != 7u64 ||
        raw_label_object(raw_targets[0]) != 7u64 ||
        raw_label_object(raw_label_object::other) != 7u64 ||
        raw_label_object(private_label_identity::<raw_label_object::other>()) != 7u64 ||
        raw_label_direct(raw_targets[1]) != 11u64 ||
        RawLabelScope::owner() != 19u64)
        return 0u32;
#endif
    return 61u32;
}

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void generic_label_owner() {
    global label target:
    ;
}

namespace generic_labels {
    global void other_owner() {
        global label target:
        ;
    }
}

static u32 generic_private_label_owner() {
    first: return 3u32;
    second: return 5u32;
}

static label choose_label(in bool choose, in label first, in label second) {
    label result = first;
    if (!choose) result = (label)second;
    return result;
}
struct LabelBox { label values[2]; u32 tag; };
struct LabelMemberBox { label point; };
[[eval_only]] static uptr translation_label_layout() {
    return sizeof(point) + $::alignof((point));
    point: ;
}
[[generic(T), eval_only]] static uptr instance_label_layout() {
    return sizeof(point) + $::alignof(point) + sizeof(T);
    point: ;
}
[[eval_only]] static label translation_label_parameter(in label point) {
    return point;
    point: ;
}
[[eval_only]] static label translation_label_member(in struct LabelMemberBox value) {
    return value.point;
    point: ;
}
[[eval_only]] static label member_label() {
    struct LabelMemberBox value = {generic_private_label_owner::first};
    return translation_label_member(value);
}
[[eval_only]] static label translation_label_local() {
    label point = generic_private_label_owner::first;
    return point;
    point: ;
}
static uptr translation_label_size = sizeof(translation_label_layout::point);
static uptr translation_label_alignments[2] = {
    $::alignof(translation_label_layout::point), sizeof(translation_label_layout::point)
};
[[eval_only]] static u32 layout_operand_must_not_execute(in u32 divisor) {
    return 1u32 / divisor;
}
static uptr translation_operand_size = sizeof(layout_operand_must_not_execute(0u32));
$::static_assert(sizeof(translation_label_layout::point) == sizeof(label),
    "a qualified label layout query does not create an address");
$::static_assert(translation_label_layout() == sizeof(label) + $::alignof(label),
    "unevaluated label layout does not need an emitted owner");
$::static_assert(instance_label_layout::<u32>() == sizeof(label) + $::alignof(label) + sizeof(u32) &&
    instance_label_layout::<u64>() == sizeof(label) + $::alignof(label) + sizeof(u64),
    "generic label layout retains the selected target and instance");
$::static_assert(translation_label_parameter(generic_private_label_owner::first) ==
    generic_private_label_owner::first && member_label() == generic_private_label_owner::first &&
    translation_label_local() == generic_private_label_owner::first,
    "local, parameter and member names do not select an unrelated translation-only label");
union LabelStorage { label value; u64 padding[2]; };
static struct LabelBox make_label_box(in label first, in label second) {
    label selected;
    label *address = &selected;
    *address = second;
    union LabelStorage stored = {.value = first};
    union LabelStorage copied = stored;
    struct LabelBox value = {{copied.value, *address}, 17u32};
    struct LabelBox copy = value;
    copy.values[1] = selected;
    return copy;
}
static label box_label(in struct LabelBox value) {
    label copy[2] = {value.values[0], value.values[1]};
    return copy[1];
}
static label numeric_label(in uptr bits) {
    label value = (label)bits;
    label *address = &value;
    label copy[2] = {(label)0uptr, *address};
    return copy[1];
}
static struct LabelBox numeric_label_box() {
    struct LabelBox value = {};
    value.values[1] = numeric_label(0x1234uptr);
    return value;
}
static label static_numeric_label = numeric_label(0x1234uptr);
static struct LabelBox static_numeric_box = numeric_label_box();
$::static_assert((uptr)box_label(numeric_label_box()) == 0x1234uptr,
    "numeric labels survive addressed cells and aggregate copies");
$::static_assert(numeric_label(0uptr) == (label)0uptr &&
    numeric_label(0uptr) != numeric_label(0x1234uptr), "numeric label comparison");
static uptr zero_label_leaf() {
    struct LabelBox value = {};
    label values[2] = {};
    return (uptr)value.values[0] | (uptr)values[1];
}
$::static_assert(zero_label_leaf() == 0uptr, "zero-initialized label leaves");
static struct LabelBox static_label_box = make_label_box(
    generic_private_label_owner::first, generic_private_label_owner::second);
[[eval_only]] static label rotate_label(in u32 count, in label first, in label second) {
    if (count == 0u32) return first;
    return rotate_label(count - 1u32, second, first);
}
static label selected_static_label = choose_label((bool)0u32,
    generic_private_label_owner::first, generic_private_label_owner::second);
static void adjacent_label_owner() { first: ; second: ; }
[[noinline]] static bool adjacent_labels_equal() {
    return adjacent_label_owner::first == adjacent_label_owner::second;
}
$::static_assert(choose_label((bool)1u32, generic_private_label_owner::first,
    generic_private_label_owner::second) == generic_private_label_owner::first,
    "a selected symbolic label keeps its source identity");

[[generic(label L), noinline]]
#ifdef TEST_MIPS
static label label_identity() {
#else
static label label_identity() -> "r8" {
#endif
    return L;
}

[[generic(label First, label Second), noinline]] static label label_forward() {
    return label_identity::<choose_label((bool)0u32, First, Second)>();
}
[[generic(T), noinline]] static T deduced_label(in T value) { return value; }
[[generic(label Address), noinline]] static label deduced_label_forward() {
    return deduced_label(Address);
}

[[generic(T), noinline]] static label instance_owned_label() {
    static label direct = point;
    static label forwarded = label_identity::<point>();
    static struct LabelBox values = {{point, label_identity::<point>()}, sizeof(T)};
    return direct == forwarded && deduced_label(point) == direct &&
        values.values[0] == direct && values.tag == sizeof(T)
        ? values.values[1] : (label)0uptr;
    point: ;
}
[[generic(T), noinline]] static label constant_instance_label() {
    return point;
    point: ;
}
static label static_instance_label = constant_instance_label::<u32>();
static label other_static_instance_label = constant_instance_label::<u64>();
static label outside_instance_label = constant_instance_label::<u32>::point;
static label outside_angle_label = constant_instance_label<u64>::point;

[[noinline]] static label ordinary_owned_label() {
    static label direct = point;
    static label forwarded = label_identity::<point>();
    return direct == forwarded ? direct : (label)0uptr;
    point: ;
}

[[noinline]]
#ifdef TEST_MIPS
static label label_echo(in label value) {
#else
static label label_echo(in label value "r10") -> "stack+32" {
#endif
    return value;
}

global u64 generic_label_value_entry() {
    if (sizeof(translation_label_layout::point) != sizeof(label) ||
        $::alignof((translation_label_layout::point)) != $::alignof(label) ||
        translation_label_size != sizeof(label) ||
        translation_label_alignments[0] != $::alignof(label) ||
        translation_label_alignments[1] != sizeof(label) ||
        translation_operand_size != sizeof(u32) ||
        sizeof(layout_operand_must_not_execute(0u32)) != sizeof(u32) ||
        $::alignof(layout_operand_must_not_execute(0u32)) != $::alignof(u32)) return 0u64;
    label first = label_identity::<generic_label_owner::target>();
    label second = label_identity::<((generic_label_owner::target))>();
    label other = label_identity::<generic_labels::other_owner::target>();
    label through_stack = label_echo(first);
    label local_first = label_identity::<generic_private_label_owner::first>();
    label local_second = label_echo(label_identity::<generic_private_label_owner::second>());
    label conditional = label_identity::<(1u32 ? generic_private_label_owner::first : generic_private_label_owner::second)>();
    label selected = label_echo(label_identity::<choose_label((bool)0u32,
        generic_private_label_owner::first, generic_private_label_owner::second)>());
    label recursive = label_identity::<rotate_label(3u32,
        generic_private_label_owner::first, generic_private_label_owner::second)>();
    label forwarded = label_forward::<generic_private_label_owner::first, generic_private_label_owner::second>();
    label forced = $::eval(choose_label((bool)1u32,
        generic_private_label_owner::first, generic_private_label_owner::second));
    label object = label_identity::<box_label(make_label_box(
        generic_private_label_owner::first, generic_private_label_owner::second))>();
    struct LabelBox evaluated = $::eval(make_label_box(
        generic_private_label_owner::first, generic_private_label_owner::second));
    label rematerialized = box_label($::eval(make_label_box(
        generic_private_label_owner::first, generic_private_label_owner::second)));
    label numeric = label_echo(label_identity::<numeric_label(0x1234uptr)>());
    label zero = label_identity::<(label)0uptr>();
    label forwarded_numeric = label_forward::<(label)0uptr, (label)0x1234uptr>();
    struct LabelBox numeric_image = $::eval(numeric_label_box());
    label instance_first = instance_owned_label::<u32>();
    label instance_second = instance_owned_label::<u64>();
    label constant_instance = label_identity::<constant_instance_label::<u32>()>();
    label deduced = deduced_label(generic_private_label_owner::first);
    label deduced_forwarded = deduced_label_forward::<generic_private_label_owner::second>();
    return first == second && first == generic_label_owner::target &&
           other == generic_labels::other_owner::target && first != other &&
           through_stack == first && local_first == generic_private_label_owner::first &&
           local_second == generic_private_label_owner::second && local_first != local_second &&
           conditional == local_first && selected == local_second && recursive == local_second &&
           forwarded == local_second && forced == local_first && selected_static_label == local_second &&
           object == local_second && rematerialized == local_second && evaluated.tag == 17u32 &&
           evaluated.values[0] == local_first && evaluated.values[1] == local_second &&
           static_label_box.values[0] == local_first && static_label_box.values[1] == local_second &&
           (uptr)numeric == 0x1234uptr && zero == (label)0uptr && forwarded_numeric == numeric &&
           static_numeric_label == numeric && static_numeric_box.values[0] == zero &&
           static_numeric_box.values[1] == numeric && numeric_image.values[0] == zero &&
           numeric_image.values[1] == numeric &&
           (uptr)$::runtime(numeric_label(0x1234uptr)) == 0x1234uptr &&
           instance_first != (label)0uptr && instance_first != instance_second &&
           instance_first == instance_owned_label::<u32>() &&
           constant_instance == static_instance_label &&
           deduced == local_first && deduced_forwarded == local_second &&
           static_instance_label != other_static_instance_label &&
           outside_instance_label == static_instance_label &&
           outside_angle_label == other_static_instance_label &&
           ordinary_owned_label() == ordinary_owned_label::point &&
           adjacent_labels_equal() == $::runtime(adjacent_labels_equal());
}

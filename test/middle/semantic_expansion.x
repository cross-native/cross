[[macro]]
static $::meta::tokens identity(in $::meta::tokens input) {
    return input;
}

[[macro]]
static $::meta::tokens add_generated_object(in $::meta::tokens input) {
    return $::quote {
        $::unquote(input)
        global u64 generated_object = 55u64;
    };
}

identity! {
    global u64 procedural_value = 33u64;
}

add_generated_object! {
    global u64 attribute_value = 44u64;
}

[[eval_only]]
static u64 hash40(in const u8 *text) {
    u64 value = 0u64;
    for (uptr index = 0uptr; text[index] != 0u8; ++index) {
        value = value * 40u64 + text[index];
    }
    return value;
}

[[generic(T)]]
static T choose(in bool first, in T left, in T right) {
    return first ? left : right;
}

[[generic(T, uptr N)]]
static T add_count(in T value) {
    return value + N;
}

global u64 evaluated_hash = hash40("Cross");
global f128 binary128_value = 1.5f128;

global u64 semantic_expansion_entry() {
    u64 selected = choose::<u64>(1, 11u64, 22u64);
    return add_count::<u64, 9uptr>(selected);
}

global u128 wide_integer(in u128 value) {
    return value + 0x10000000000000000000000000000000u128;
}

[[section(".data.private")]]
global uptr patch_operand_site;

[[section(".data.private")]]
global uptr managed_patch_site;

global u64 managed_patch_expression(in u64 value) {
    return $::patch(0x1122334455667788u64, managed_patch_site) +
           value + 123u64;
}

global u32 managed_patch_without_sink() {
    return $::patch(0x55667788u32) ^ 0xa5a5a5a5u32;
}

[[naked, link_name("patch_entry")]]
global void patch_entry(out u64 value "rax") {
    $::_movabs(value,
               $::patch(0xb002ca11u64, patch_operand_site));
    $::_ret();
}

[[raw_inline]]
static u64 add_seven(in u64 value) {
    return value + 7u64;
}

[[naked, link_name("raw_inline_entry"), clobber("flags")]]
global void raw_inline_entry(inout u64 value "rax") {
    value = add_seven(value);
    $::_ret();
}

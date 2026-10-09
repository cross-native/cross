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

static T choose<T>(in bool first, in T left, in T right) {
    return first ? left : right;
}

static T add_count<T, uptr N>(in T value) {
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

[[raw_inline]]
static u64 mix(in u64 value) {
    u64 x = value;
    x ^= x >> 17;
    return x * 0x9e3779b97f4a7c15u64;
}

static u64 raw_inline_dead_call(in u64 value) {
    return value + 999u64;
}

[[raw_inline]]
static u64 structured_add(in u64 value) {
    u64 x = value;
    u64 iteration = 0;
    if (x < 4) {
        x += 10;
    } else {
        x ^= 3;
    }
    while (iteration < 3) {
        x += iteration;
        ++iteration;
    }
    if (0) {
        return raw_inline_dead_call(x);
    }
    return add_seven(x);
}

[[raw_inline]]
static u64 load_add(in u64 *source) {
    u64 x = *source;
    return x + 5u64;
}

[[naked, link_name("raw_inline_entry"), clobber("flags")]]
global void raw_inline_entry(inout u64 value "rax") {
    value = add_seven(value);
    $::_ret();
}

[[naked, link_name("raw_inline_complex"),
  clobber("r10", "r11", "flags")]]
global void raw_inline_complex(in u64 value "r9", out u64 result "r8") {
    result = mix(value);
    $::_ret();
}

[[naked, link_name("raw_inline_structured"),
  clobber("r10", "r11", "flags")]]
global void raw_inline_structured(in u64 value "r9", out u64 result "r8") {
    result = structured_add(value);
    $::_ret();
}

[[naked, link_name("raw_inline_memory"),
  clobber("r10", "flags", "memory")]]
global void raw_inline_memory(in u64 *source "r9", out u64 result "r8") {
    result = load_add(source);
    $::_ret();
}

[[naked, link_name("raw_inline_store"),
  clobber("r10", "r11", "rdx", "flags", "memory")]]
global void raw_inline_store(in u64 value "r9", in u64 *destination "r8") {
    destination[0] = mix(value);
    $::_ret();
}

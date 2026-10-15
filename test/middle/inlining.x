i64 inline_add(in i64 value) {
    i64 adjusted = value + 3;
    return adjusted;
}

[[noinline]]
i64 retained_add(in i64 value) {
    return value + 5;
}

[[always_inline]]
i64 mandatory_double(in i64 value) {
    return value * 2;
}

// Branches, several returns, and output parameters.
static bool inline_magnitude(in i64 value, out i64 magnitude) {
    if (value < 0) {
        magnitude = -value;
        return 1;
    }
    magnitude = value;
    return 0;
}

[[noinline]]
static void retained_magnitude(in i64 value, out i64 magnitude) {
    if (value < 0) {
        magnitude = -value;
        return;
    }
    magnitude = value;
}

[[always_inline]]
static i64 mandatory_clamp(in i64 value, inout i64 clamped) {
    if (value > clamped) return clamped;
    clamped = value;
    return value;
}

global i64 inline_entry(in i64 value) {
    i64 magnitude;
    i64 retained;
    i64 clamped = 100;
    const bool negative = inline_magnitude(value, magnitude);
    retained_magnitude(value, retained);
    return $::runtime(
        inline_add(value) +
        retained_add(value) +
        mandatory_double(value) +
        magnitude + retained + (i64)negative +
        mandatory_clamp(value, clamped) + clamped
    );
}

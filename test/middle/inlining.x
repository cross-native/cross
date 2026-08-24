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

global i64 inline_entry(in i64 value) {
    return $::runtime(
        inline_add(value) +
        retained_add(value) +
        mandatory_double(value)
    );
}

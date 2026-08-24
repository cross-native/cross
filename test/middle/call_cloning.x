[[noinline]]
i64 clone_target(in i64 selector, in i64 value) {
    if (selector == 1i64) {
        return value + 10i64;
    }
    return value - 3i64;
}

global i64 clone_entry(in i64 selector, in i64 value) {
    return $::runtime(
        clone_target(1i64, value) +
        clone_target(selector, value)
    );
}

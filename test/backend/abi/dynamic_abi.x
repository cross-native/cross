[[noinline]]
i64 private_identity(in i64 ignored, in i64 value) {
    return value;
}

[[noinline]]
i64 private_choose(in bool choose, in i64 left, in i64 right) {
    if (choose) {
        return left;
    }
    return right;
}

[[runtime_only, noinline]]
u64 private_clobber_leaf(in u64 value, in u64 step) {
    return (value + step) ^ (value >> 17);
}

[[runtime_only, noinline]]
u64 private_clobber_outer(in u64 value, in u64 step) {
    u64 first = private_clobber_leaf(value, step);
    return private_clobber_leaf(first * 5u64 + 1u64, step + 3u64);
}

global i64 dynamic_abi_entry(in i64 value) {
    return $::runtime(
        private_identity(1, value) +
        private_choose(value != 0, value, 7)
    );
}

global i32 dynamic_clobber_runtime_entry() {
    u64 result = 4;
    uptr index = 0;
    while (index < 9) {
        result = private_clobber_outer(result, index);
        index = index + 1;
    }
    return result == 10498162u64;
}

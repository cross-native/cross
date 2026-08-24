enum Count [[underlying(u32)]] {
    count_zero,
};

[[operator("+")]]
global enum Count add_counts(in enum Count left, in enum Count right) {
    return left;
}

global enum Count combine_counts(in enum Count left, in enum Count right) {
    return left + right;
}

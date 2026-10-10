#include "debug_info_header.x"

namespace outer::inner {
global i32 twice(in i32 x) {
    i32 y = x + x;
    return y;
}
}

static i32 pick<i32 N>(in i32 x) {
    return x + N;
}

[[noinline]] static i32 sum_to(in i32 n) {
    i32 total = 0;
    for (i32 i = 0; i < n; ++i) {
        total += i;
    }
    return total;
}

global i32 exits(in i32 v) {
    if (v < 0) {
        return 0;
    }
    i32 r = outer::inner::twice(v);
    r = r + sum_to(v);
    return r + pick::<2>(v) + header_add(v, 1);
}

enum mode [[underlying(u8)]] { idle, busy = 4 };

struct cell {
    i32 value;
    u32 low : 3;
    u32 high : 5;
};

namespace store {
global struct cell slots[2];
}

global enum mode state = busy;

global i32 inspect(in i32 index) {
    struct cell local = store::slots[index];
    local.low = 1;
    return local.value + (i32)state;
}

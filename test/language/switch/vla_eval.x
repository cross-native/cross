// A translation-only function obeys the same rule as a runtime one.
[[eval_only]] static u32 vla_eval(in u32 selector, in u32 n) {
    u32 result = 0u32;
    switch (selector) {
    case 1:
        u32 buffer[n];
        buffer[0] = 5u32;
        result = buffer[0];
    case 2:
        result += 1u32;
    }
    return result;
}

global u32 vla_eval_use() {
    return vla_eval(2u32, 3u32);
}

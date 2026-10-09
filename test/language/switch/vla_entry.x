global u32 vla_entry(in u32 selector, in u32 n) {
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

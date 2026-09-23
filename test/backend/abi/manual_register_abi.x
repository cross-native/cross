global void normalize(inout i32 status "eax") {
    if (status == 0) {
        status = 1;
    }
}

global void exchange(inout i32 left "eax", inout i32 right "edx") {
    i32 temporary = left;
    left = right;
    right = temporary;
}

global i32 manual_result(in i32 value "ecx") -> "eax" {
    return value + 7;
}

void local_adjust(inout i32 value "r10d") {
    value += 8;
}

global void indirect_increment(inout i32 value "*r11") {
    value += 4;
}

global void indirect_compute(in i32 seed "*r10", out i32 result "*r11") {
    result = seed + 3;
}

global f64 manual_scale(in f64 value "xmm1") -> "xmm0" {
    return value * 2.0;
}

global i32 manual_normalize_entry() {
    i32 status = 0;
    normalize(status);
    return status;
}

global i32 manual_exchange_entry() {
    i32 left = 10;
    i32 right = 20;
    exchange(left, right);
    return left * 100 + right;
}

global i32 manual_result_entry() {
    return manual_result(4);
}

global i32 manual_simd_entry() {
    return manual_scale(2.5) == 5.0;
}

global i32 manual_abi_entry() {
    i32 status = 0;
    normalize(status);

    i32 left = 10;
    i32 right = 20;
    exchange(left, right);

    i32 local = 0;
    local_adjust(local);
    indirect_increment(local);

    i32 indirect = 0;
    indirect_compute(5, indirect);

    return status + left + right + local + indirect + manual_result(4);
}

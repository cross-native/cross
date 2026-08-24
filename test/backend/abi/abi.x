[[abi("sysv_abi"), link_name("sum_sysv"), section(".text.boot")]]
global i64 sum(in i64 left, in i64 right) {
    return left + right;
}

[[abi("ms_abi"), link_name("sum_ms")]]
global i64 sum_windows(in i64 left, in i64 right) {
    return left + right;
}

[[section(".device_table")]]
global const uptr device_address = 0x40000000uptr;

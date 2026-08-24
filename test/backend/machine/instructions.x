global u64 instruction_test() {
    register u64 value = 0;
    $::_movabs(value, 0x123456789abcdef0u64);
    $::_add(value, 4);
    $::_cmp(value, 0);
    return value;
}

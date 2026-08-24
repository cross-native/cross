struct packed_value [[packed]] {
    u8 tag;
    u32 value;
};

global i32 packed_address_entry() {
    struct packed_value packed;
    u32 *pointer = &packed.value;
    return 0;
}

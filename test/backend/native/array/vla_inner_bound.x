global i32 invalid_vla_inner(in u32 count) {
    u32 values[2][count];
    return 0;
}

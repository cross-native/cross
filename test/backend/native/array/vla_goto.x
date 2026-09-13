global i32 invalid_vla_goto() {
    u32 count = 4u32;
    goto done;
    u32 values[count];
done:
    return values[0];
}

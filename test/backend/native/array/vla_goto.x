global i32 invalid_vla_goto() {
    u32 count = 4u32;
    u32 values[count];
    goto done;
done:
    return values[0];
}

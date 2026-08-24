typedef i32 i32x4 [[ext_vector_type(4)]];

global i32 bad_lane() {
    i32x4 value = 0;
    return value[4];
}

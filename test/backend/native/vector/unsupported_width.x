typedef i32 i32x3 [[ext_vector_type(3)]];

global i32 bad_width() {
    i32x3 value = 1;
    return value[0];
}

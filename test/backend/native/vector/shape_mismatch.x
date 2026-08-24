typedef i32 i32x4 [[ext_vector_type(4)]];
typedef i64 i64x2 [[ext_vector_type(2)]];

global i64 bad_shape() {
    i32x4 source = 1;
    i64x2 destination = source;
    return destination[0];
}

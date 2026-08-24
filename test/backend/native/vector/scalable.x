typedef i32 scalable_i32 [[scalable_vector(4)]];

global i32 bad_scalable() {
    scalable_i32 value = 1;
    return value[0];
}

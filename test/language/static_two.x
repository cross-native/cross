static i32 helper(in i32 value) {
    return value + 2;
}

global i32 static_two() {
    return helper(10);
}


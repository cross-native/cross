static i32 helper(in i32 value) {
    return value + 1;
}

global i32 static_one() {
    return helper(10);
}


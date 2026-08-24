enum Handle {
    handle_zero,
};

[[operator("+")]]
global i32 scalar_override(in i32 left, in i32 right) {
    return left + right;
}

[[operator("=")]]
global enum Handle assignment_override(
    in enum Handle left, in enum Handle right) {
    return left;
}

[[operator("-")]]
global enum Handle first_subtract(
    in enum Handle left, in enum Handle right) {
    return left;
}

[[operator("-")]]
global enum Handle second_subtract(
    in enum Handle left, in enum Handle right) {
    return right;
}

[[operator("*")]]
global enum Handle inconsistent_binding(
    in enum Handle left, in enum Handle right);

[[operator("/")]]
global enum Handle inconsistent_binding(
    in enum Handle left, in enum Handle right) {
    return left;
}

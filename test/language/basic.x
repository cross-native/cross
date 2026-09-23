global i32 add(in i32 left, in i32 right) {
    return left + right;
}

global void normalize(inout i32 status) {
    if (status == 0) {
        status = 1;
    }
}

global i32 entry() {
    i32 status = 0;
    normalize(status);
    return status + add(2, 3);
}

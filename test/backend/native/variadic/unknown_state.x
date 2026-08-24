[[abi("sysv_abi"), variadic(u32 offset "missing")]]
global i32 invalid(in i32 value, ...) {
    return value;
}

[[abi("sysv_abi")]]
global void preserved_output(inout i32 value "ebx") {
    value = 1;
}

global void wrong_width(in i64 value "eax") {
}

global void overlapping_inputs(in i32 first "eax", in i32 second "eax") {
}

global void invalid_indirect(in i32 value "*eax") {
}

global void reserved_stack_pointer(in i64 value "rsp") {
}

global void wrong_register_class(in i16 value "xmm0") {
}

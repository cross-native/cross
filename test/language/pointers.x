global i32 sum_array(in const i32 *values, in uptr count) {
    i32 total = 0;
    for (uptr index = 0; index < count; ++index) {
        total += values[index];
    }
    return total;
}

global i32 addressable_global = 5i32;

global i32 address_local() {
    i32 value = 41i32;
    i32 *pointer = &value;
    *pointer = *pointer + 1i32;
    return value;
}

global i32 address_parameter(in i32 value) {
    i32 *pointer = &value;
    *pointer = *pointer + 2i32;
    return value;
}

global i32 address_global() {
    i32 *pointer = &addressable_global;
    *pointer = 13i32;
    return addressable_global;
}

global void write_pair(out i32 first, out i32 second) {
    first = 17i32;
    second = 23i32;
}

global i32 copyout_addresses() {
    i32 value = 0i32;
    i32 *pointer = &value;
    write_pair(*pointer, *pointer);
    write_pair(addressable_global, 1i32 + 2i32);
    return value + addressable_global;
}

global i32 address_entry() {
    return address_local() + address_parameter(10i32) + address_global() +
           copyout_addresses();
}

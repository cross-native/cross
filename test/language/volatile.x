global u32 device_read(in volatile u32 *address) {
    return *address;
}

global void device_write(in volatile u32 *address, in u32 value) {
    *address = value;
}


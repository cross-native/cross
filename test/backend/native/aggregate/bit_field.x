struct flags {
    u32 enabled : 1;
};

struct paired_flags {
    u32 low : 1, high : 2;
};

global u32 bit_field_probe(in struct flags *value) {
    return value->enabled;
}

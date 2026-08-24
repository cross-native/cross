global i32 negative_integer = -42i32;
global u128 wide_integer = 0x112233445566778899aabbccddeeff00u128;

global f32 negative_f32 = -1.5f32;
global f64 negative_f64 = -1.5f64;
global f80 negative_f80 = -1.5f80;
global f128 negative_f128 = -1.5f128;

global const u64 read_only_object = 13u64;
static u64 private_object = 17u64;

[[section(".cross.data"), aligned(32), used]]
global u64 aligned_object = 7u64;

[[retain]]
global u64 retained_object = 9u64;

[[noinit, aligned(64)]]
global u64 uninitialized_object;

global u64 addressed_object = 11u64;
global u64 *object_address = &addressed_object;

global void addressed_function() {}
global void *function_address = &addressed_function;

global i32 dispatch() {
target:
    return 3i32;
}

global label label_address = dispatch::target;

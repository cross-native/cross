global f64 optimization_add_zero(in f64 value) {
    return value + 0.0;
}

global f64 optimization_multiply_one(in f64 value) {
    return value * 1.0;
}

global bool optimization_self_equal(in f64 value) {
    return value == value;
}

global f64 optimization_negated_constant() {
    return -0.25f64;
}

global i32 optimization_redundant(in i32 left, in i32 right) {
    return (left + right) * (left + right);
}

global i32 optimization_constant_branch() {
    if (1) {
        return 7;
    }
    return 9;
}

[[noinline, link_name("optimization_pure_leaf")]]
i32 optimization_pure_leaf(in i32 value) {
    return value + 5;
}

[[noinline, link_name("optimization_cell_leaf")]]
void optimization_cell_leaf(i32 value) {
    value += 1;
}

[[link_name("optimization_unused_pure_call")]]
global i32 optimization_unused_pure_call(in i32 value) {
    optimization_pure_leaf(value);
    return value;
}

[[link_name("optimization_required_cell_call")]]
global i32 optimization_required_cell_call(in i32 value) {
    i32 local = value;
    optimization_cell_leaf(local);
    return local;
}

[[link_name("optimization_dead_stores")]]
global i32 optimization_dead_stores(in i32 value) {
    i32 local = value;
    local = 41;
    local = 42;
    return local;
}

[[link_name("optimization_volatile_stores")]]
global i32 optimization_volatile_stores(in i32 value) {
    register volatile i32 local = value;
    local = 43;
    local = 44;
    return local;
}

[[link_name("optimization_cfg_dead_stores")]]
global i32 optimization_cfg_dead_stores(in bool choose) {
    i32 local = 101;
    if (choose) {
        local = 102;
    } else {
        local = 103;
    }
    local = 104;
    return local;
}

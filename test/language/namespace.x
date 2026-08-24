namespace math {
global i32 twice(in i32 value) {
    return value * 2;
}

i32 local_increment(i32 value) {
    value += 1;
    return value;
}
}

using math;

global i32 app::entry() {
    i32 value = twice(20);
    local_increment(value);
    return value;
}

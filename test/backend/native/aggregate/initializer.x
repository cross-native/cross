struct point {
    i32 x;
};

global i32 aggregate_initializer_entry() {
    struct point point = 1;
    return 0;
}

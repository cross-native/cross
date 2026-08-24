struct point {
    i32 x;
};

global i32 unknown_member_entry() {
    struct point point;
    return point.y;
}

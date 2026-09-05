global i32 duplicate_case(in i32 x) {
    switch (x) { case 1: return 1; case 1: return 2; }
}

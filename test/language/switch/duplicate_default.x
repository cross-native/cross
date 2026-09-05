global i32 duplicate_default(in i32 x) {
    switch (x) { default: return 1; default: return 2; }
}

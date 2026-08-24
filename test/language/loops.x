global i32 sum_to(in i32 count) {
    i32 total = 0;
    for (i32 index = 0; index < count; ++index) {
        if (index == 7) {
            continue;
        }
        total += index;
    }

    do {
        --total;
    } while (total > 100);
    return total;
}


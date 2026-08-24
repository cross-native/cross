// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

extern long long native_entry(void);

int main(void) {
    return native_entry() == 419 ? 0 : 1;
}

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROSS_ENTRY
#error "CROSS_ENTRY must name the Cross function under test"
#endif

extern "C" int CROSS_ENTRY();

int main() {
    return CROSS_ENTRY();
}

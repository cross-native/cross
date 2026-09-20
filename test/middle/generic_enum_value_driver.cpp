// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

extern "C" unsigned long long generic_enum_value_entry();

int main() {
    return generic_enum_value_entry() == 1 ? 0 : 1;
}

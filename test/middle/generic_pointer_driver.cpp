// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" int generic_pointer_entry();
extern "C" const unsigned *explicit_pointer();
int main() { return generic_pointer_entry() == 1 && *explicit_pointer() == 11 ? 0 : 1; }

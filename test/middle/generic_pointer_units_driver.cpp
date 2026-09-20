// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" int pointer_unit_a();
extern "C" int pointer_unit_b();
int main() { return pointer_unit_a() == 1 && pointer_unit_b() == 1 ? 0 : 1; }

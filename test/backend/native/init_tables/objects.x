// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Initializer-table entries of each object format, as the profile's tables
// place them. PRIORITIES adds prioritized entries where the tables have them.

global u32 order;
global void work();

#if PRIORITIES
[[constructor(101)]] static void early() { order = 1u32; }
[[destructor(101)]] static void late() { order = 2u32; }
#endif

// The table's ABI may preserve more than the Cross ABI lets work() change,
// as ms_abi does; the entry's adapter then saves the difference.
[[constructor]] static void plain() { work(); }
[[destructor]] global void finish() { order = 4u32; }

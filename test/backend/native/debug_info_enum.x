// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Enumerators keep the width and signedness of their underlying type.
enum small [[underlying(i8)]] { negative = -1i8, lowest = -127i8 - 1i8, highest = 127i8 };
enum word [[underlying(u16)]] { top = 65535u16 };
enum wide [[underlying(i128)]] {
    minus = -2i128,
    large = 0x0123456789abcdef0123456789abcdefi128,
};
enum huge [[underlying(u128)]] { all = 0xffffffffffffffffffffffffffffffffu128 };
enum pointer [[underlying(iptr)]] { back = -3iptr };

global enum small s = negative;
global enum word w = top;
global enum wide x = minus;
global enum huge h = all;
global enum pointer p = back;

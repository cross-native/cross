// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 (* [[address_space(0)]] space_zero_callback)(in i32);
typedef i32 [[address_space(0)]] (*prefixed_space_callback)(in i32);

global space_zero_callback callback_slot;
global prefixed_space_callback prefixed_callback_slot;

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
struct huge { u8 bytes[8192]; };
global struct huge too_large(in struct huge input) { return input; }

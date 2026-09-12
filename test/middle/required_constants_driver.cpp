// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif
extern "C" CROSS_ABI int required_constants_entry();
extern "C" CROSS_ABI int required_wide_entry();

int main() {
    return required_constants_entry() == 1 && required_wide_entry() == 1 ? 0 : 1;
}

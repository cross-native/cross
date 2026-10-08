// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver/driver.hpp"

int main(int argc, char** argv) {
    return cross::run_with_compiler_stack(cross::cc_main, argc, argv);
}

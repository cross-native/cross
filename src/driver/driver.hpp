// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cross {

int cc_main(int argc, char** argv);
int cpp_main(int argc, char** argv);
// Runs a tool entry point on a thread with a large stack and returns its result.
int run_with_compiler_stack(int (*entry)(int, char**), int argc, char** argv);

} // namespace cross

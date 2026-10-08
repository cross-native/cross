// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/continuation_task.hpp"

namespace cross {

// Frontend spelling retained for evaluator/parser consumers. The scheduler is
// target-independent compiler-host infrastructure, also used during lowering.
template<class T>
using EvaluationTask = ContinuationTask<T>;

} // namespace cross

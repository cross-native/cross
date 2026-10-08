// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cross {

enum class MemoryOrder { Relaxed, Acquire, Release, AcqRel, SeqCst };

inline bool valid_atomic_failure_order(MemoryOrder success, MemoryOrder failure) {
    if (failure == MemoryOrder::Release || failure == MemoryOrder::AcqRel) return false;
    switch (success) {
    case MemoryOrder::Relaxed: case MemoryOrder::Release:
        return failure == MemoryOrder::Relaxed;
    case MemoryOrder::Acquire: case MemoryOrder::AcqRel:
        return failure == MemoryOrder::Relaxed || failure == MemoryOrder::Acquire;
    case MemoryOrder::SeqCst:
        return failure == MemoryOrder::Relaxed || failure == MemoryOrder::Acquire ||
               failure == MemoryOrder::SeqCst;
    }
    return false;
}

} // namespace cross

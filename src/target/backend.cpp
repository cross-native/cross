// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/backend.hpp"

#include "target/target.hpp"
#include "target/x86_64/backend.hpp"

namespace cross {

const std::vector<const TargetBackend*>& all_target_backends() {
    static const std::vector<const TargetBackend*> backends{
        &x86_64::backend(),
    };
    return backends;
}

const TargetBackend* target_backend_for(const TargetInfo& target) {
    for (const auto* backend : all_target_backends()) {
        if (backend->architecture() == target.architecture) return backend;
    }
    return nullptr;
}

} // namespace cross

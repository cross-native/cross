// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/native/stack_usage.hpp"

#include "backend/native/debug_info.hpp"

#include <algorithm>
#include <vector>

namespace cross::native {

std::string stack_usage_report(const hir::Module& module,
                               const CompilerOptions& options,
                               std::span<const mir::FunctionAssembly> functions) {
    std::vector<const mir::FunctionAssembly*> order;
    for (const auto& function : functions) order.push_back(&function);
    std::ranges::sort(order, [&](const auto* left, const auto* right) {
        return hir::emitted_before(module, left->function, right->function);
    });
    std::string report;
    for (const auto* function : order) {
        const auto& entity = module.function(function->function);
        const auto place = written_location(entity.location);
        if (place.valid()) {
            report += mapped_source_path(options, place.file->path) + ':' +
                      std::to_string(place.line) + ':' +
                      std::to_string(place.column) + ':';
        }
        report += entity.link_symbol + '\t' + std::to_string(function->stack.bytes) +
                  (function->stack.dynamic ? "\tdynamic\n" : "\tstatic\n");
    }
    return report;
}

} // namespace cross::native

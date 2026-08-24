// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/target.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cross {
struct CompilerOptions;
class Diagnostics;

enum class ObjectFormat { Elf, Coff, MachO, Unsupported };

// These tables belong to a target implementation, not the driver.  Adding an
// architecture therefore only requires registering its TargetInfo and a
// matching SubtargetTable (until the table is embedded in TargetInfo itself).
struct SubtargetFeature {
    std::string_view name;
    bool enabled_by_default{};
    bool selectable{true};
    // ISA extensions frequently form dependency graphs (for example AVX2 ->
    // AVX, or RISC-V D -> F).  The common resolver expands these edges and
    // diagnoses explicit contradictions without architecture-name branches.
    std::vector<std::string_view> prerequisites;
    std::vector<std::string_view> conflicts;
};

struct SubtargetCpu {
    std::string_view name;
    std::vector<std::string_view> enabled_features;
};

struct SubtargetTable {
    std::string_view architecture;
    std::vector<SubtargetFeature> features;
    std::vector<SubtargetCpu> cpus;
};

class Subtarget {
public:
    [[nodiscard]] const TargetInfo& target() const { return *target_; }
    [[nodiscard]] ObjectFormat object_format() const { return object_format_; }
    [[nodiscard]] std::string_view abi() const { return abi_->canonical_name; }
    [[nodiscard]] const AbiEntry& abi_info() const { return *abi_; }
    [[nodiscard]] std::string_view cpu() const { return cpu_; }
    [[nodiscard]] std::string_view tune() const { return tune_; }
    [[nodiscard]] bool has_feature(std::string_view name) const;
    [[nodiscard]] const std::vector<std::string>& enabled_features() const {
        return enabled_features_;
    }

private:
    Subtarget() = default;
    friend std::optional<Subtarget> resolve_subtarget(const TargetInfo&, const CompilerOptions&,
                                                       const SubtargetTable&, Diagnostics&);
    const TargetInfo* target_{};
    const AbiEntry* abi_{};
    ObjectFormat object_format_{ObjectFormat::Unsupported};
    std::string cpu_;
    std::string tune_;
    std::vector<std::string> enabled_features_;
};

// Built-in table selected for a compiled-in target.  It is intentionally
// separate from command-line parsing and may be replaced by a target module's
// own table through the overload below.
[[nodiscard]] const SubtargetTable* subtarget_table_for(const TargetInfo& target);

// Validates typed target options, expands architecture feature baselines, and
// writes the canonical CPU/tune/feature state consumed by preprocessing and
// lowering. It runs after model/profile/common option resolution.
bool normalize_subtarget_options(const TargetInfo& target,
                                 CompilerOptions& options,
                                 Diagnostics& diagnostics);

[[nodiscard]] std::optional<Subtarget> resolve_subtarget(const TargetInfo& target,
                                                          const CompilerOptions& options,
                                                          const SubtargetTable& table,
                                                          Diagnostics& diagnostics);

[[nodiscard]] std::optional<Subtarget> resolve_subtarget(const TargetInfo& target,
                                                          const CompilerOptions& options,
                                                          Diagnostics& diagnostics);

} // namespace cross

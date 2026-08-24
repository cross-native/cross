// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/machine_ir.hpp"

#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cross::native {

// Targets own the dense ID space; the common pipeline owns stage ordering and
// instrumentation. This mirrors TargetOpcodeId without creating a global list
// that every new architecture would have to extend.
struct TargetMachinePassId {
    std::uint16_t value{};

    constexpr TargetMachinePassId() = default;
    constexpr explicit TargetMachinePassId(std::uint16_t raw) : value(raw) {}

    template <typename TargetPass>
        requires std::is_enum_v<TargetPass>
    constexpr TargetMachinePassId(TargetPass pass)
        : value(static_cast<std::uint16_t>(pass)) {}

    friend constexpr bool operator==(TargetMachinePassId,
                                     TargetMachinePassId) = default;
};

enum class MachineStage : std::uint8_t {
    Legalization,
    Canonicalization,
    InstructionCombining,
    ControlFlow,
    Scheduling,
    RegisterAllocation,
    FrameFinalization,
};

struct MachinePassInfo {
    TargetMachinePassId id;
    MachineStage stage{MachineStage::Canonicalization};
    // A presentation-only spelling for traces and diagnostics. No pass is
    // selected or dispatched by this string.
    std::string_view name;
};

using MachineFunctionPass = std::function<bool(machine::Function&)>;
using MachinePassObserver = std::function<void(
    const MachinePassInfo&, const machine::Function&, bool changed)>;

class MachineFunctionPassManager {
public:
    void add(MachinePassInfo info, MachineFunctionPass pass);
    [[nodiscard]] bool run(machine::Function& function,
                           const MachinePassObserver& observer = {}) const;
    [[nodiscard]] bool empty() const { return passes_.empty(); }

private:
    struct Entry {
        MachinePassInfo info;
        MachineFunctionPass run;
    };
    std::vector<Entry> passes_;
};

} // namespace cross::native

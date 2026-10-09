// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"
#include "middle/hir.hpp"
#include "target/abi_lowering.hpp"
#include "target/x86_64/manual_endpoint.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cross {

class Diagnostics;
struct CompilerOptions;
class Subtarget;
struct TargetInfo;

namespace mir {
struct ManagedModule;
}

namespace x86_64 {

enum class ManualBoundaryKind : std::uint8_t {
    None,
    DirectRegister,
    RegisterPieces,
    IndirectRegister,
    Stack,
    IndirectStack,
};

// A model-classified value can occupy more than one physical register (for
// example a SysV two-eightbyte record or i128).  Explicit source endpoints
// remain single locations; this form is used only for the automatic portion
// of a mixed interface.
struct ManualRegisterPiece {
    const RegisterView* register_view{};
    std::uint16_t value_bit_offset{};
    std::uint16_t value_bits{};
    std::uint16_t carrier_bits{};
};

// A boundary is fully resolved: `auto`, stack allocation, and LIFO spelling
// have already become a physical register or a logical stack offset.
struct ManualBoundary {
    ManualBoundaryKind kind{ManualBoundaryKind::None};
    const RegisterView* register_view{};
    std::uint64_t stack_offset{};
    bool has_stack_offset{};
    bool fixed_stack_offset{};
    LifoEndpointForm source_lifo{LifoEndpointForm::none};
    std::vector<ManualRegisterPiece> register_pieces;

    [[nodiscard]] bool is_register() const {
        return kind == ManualBoundaryKind::DirectRegister ||
               kind == ManualBoundaryKind::RegisterPieces ||
               kind == ManualBoundaryKind::IndirectRegister;
    }
    [[nodiscard]] bool is_stack() const {
        return kind == ManualBoundaryKind::Stack ||
               kind == ManualBoundaryKind::IndirectStack;
    }
    [[nodiscard]] bool is_indirect() const {
        return kind == ManualBoundaryKind::IndirectRegister ||
               kind == ManualBoundaryKind::IndirectStack;
    }
    [[nodiscard]] bool is_x87() const {
        return kind == ManualBoundaryKind::DirectRegister && register_view &&
               register_view->register_class == RegisterClass::x87;
    }
};

struct ManualParameterPlan {
    std::size_t parameter_index{};
    hir::TypeId type;
    ParameterMode mode{ParameterMode::In};
    ManualBoundary input;
    ManualBoundary output;
};

struct ManualResultPlan {
    bool present{};
    hir::TypeId type;
    ManualBoundary output;
    // Some model-defined indirect results also return the result-area
    // address in a physical register.
    std::string indirect_result_register;
};

struct ManualStackLayout {
    // Logical offsets exclude the ABI's fixed outgoing stack prefix. The call
    // emitter adds `home_space_size`; a callee additionally accounts for the
    // return address and its own prologue.
    std::vector<std::optional<std::uint64_t>> input_offsets;
    std::vector<std::optional<std::uint64_t>> output_offsets;
    std::optional<std::uint64_t> result_offset;
    std::uint64_t extent{};
    std::uint64_t home_space_size{};
    std::uint64_t outgoing_area_size{};
    std::uint64_t outgoing_area_alignment{16};
    bool has_stack{};
};

struct ManualX87Layout {
    std::array<std::optional<std::size_t>, 8> inputs;
    std::array<std::optional<std::size_t>, 8> outputs;
    std::optional<unsigned> result;
    unsigned input_depth{};
    unsigned output_depth{};
    bool has_x87{};
};

// A plan describes a function's own interface or, with `signature`, the
// pointed-to function type of indirect calls.
struct ManualAbiPlan {
    hir::FunctionId function;
    std::optional<hir::TypeId> signature;
    const AbiEntry* abi_info{};
    bool manual{};
    bool callee_cleanup{};
    bool valid{true};
    // Resources the interface clobbers in addition to its ABI's.
    std::vector<std::string> clobbers;
    std::vector<ManualParameterPlan> parameters;
    ManualResultPlan result;
    ManualStackLayout stack;
    ManualX87Layout x87;
};

class ManualAbiPlans {
public:
    ManualAbiPlans() = default;
    explicit ManualAbiPlans(std::vector<ManualAbiPlan> entries)
        : entries_(std::move(entries)) {}

    [[nodiscard]] const ManualAbiPlan* find(hir::FunctionId function) const;
    [[nodiscard]] const ManualAbiPlan* find(hir::TypeId signature) const;
    // The plan of a direct callee or of an indirect call's pointed-to type.
    [[nodiscard]] const ManualAbiPlan* find(
        std::optional<hir::FunctionId> direct,
        std::optional<hir::TypeId> indirect) const {
        return direct ? find(*direct) : indirect ? find(*indirect) : nullptr;
    }
    [[nodiscard]] const std::vector<ManualAbiPlan>& entries() const {
        return entries_;
    }

private:
    std::vector<ManualAbiPlan> entries_;
};

// Builds plans only for interfaces with a non-auto physical parameter or result
// location: those of functions and, given managed MIR, the pointed-to types of
// its indirect calls. Diagnostics are emitted once while planning; invalid
// plans remain queryable with `valid == false` so later ownership decisions
// are stable.
[[nodiscard]] ManualAbiPlans build_manual_abi_plans(
    const hir::Module& module, const TargetInfo& target,
    const Subtarget& subtarget, const CompilerOptions& options,
    Diagnostics& diagnostics, const mir::ManagedModule* managed = nullptr);

} // namespace x86_64
} // namespace cross

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Machine IR is deliberately target-neutral.  Targets select opcodes, physical
// register numbers, and calling-convention details; the middle end owns the
// CFG, virtual-register, and frame invariants declared here.

#include "common/diagnostic.hpp"
#include "middle/hir.hpp"
#include "middle/mir.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace cross::machine {

struct BlockId {
    std::uint32_t value{};
    friend bool operator==(BlockId, BlockId) = default;
};

struct VirtualRegisterId {
    std::uint32_t value{};
    friend bool operator==(VirtualRegisterId, VirtualRegisterId) = default;
};

struct PhysicalRegisterId {
    std::uint32_t value{};
    friend bool operator==(PhysicalRegisterId, PhysicalRegisterId) = default;
};

// Exact target register views are distinct from physical storage: on x86,
// for example, `ah` and `al` share one register but name different bit ranges.
// Machine IR keeps the target-local dense identity without retaining source
// spelling or imposing one architecture's alias model on another.
struct TargetRegisterViewId {
    static constexpr std::uint16_t invalid_value =
        std::numeric_limits<std::uint16_t>::max();
    std::uint16_t value{invalid_value};

    [[nodiscard]] constexpr bool valid() const { return value != invalid_value; }
    friend constexpr bool operator==(TargetRegisterViewId,
                                     TargetRegisterViewId) = default;
};

struct StackSlotId {
    std::uint32_t value{};
    friend bool operator==(StackSlotId, StackSlotId) = default;
};

// Target opcodes are dense, target-local identities.  Machine IR never owns
// their textual spelling: the active target's machine description maps an ID
// to diagnostics, instruction properties, scheduling data, and assembly
// lowering.  Value zero is reserved for "no opcode" so generic instructions
// cannot accidentally masquerade as selected target instructions.
struct TargetOpcodeId {
    std::uint32_t value{};

    constexpr TargetOpcodeId() = default;
    constexpr TargetOpcodeId(std::uint32_t raw) : value(raw) {}

    template <typename TargetOpcode>
        requires std::is_enum_v<TargetOpcode>
    constexpr TargetOpcodeId(TargetOpcode opcode)
        : value(static_cast<std::uint32_t>(opcode)) {}

    template <typename TargetOpcode>
        requires std::is_enum_v<TargetOpcode>
    constexpr TargetOpcodeId& operator=(TargetOpcode opcode) {
        value = static_cast<std::uint32_t>(opcode);
        return *this;
    }

    [[nodiscard]] constexpr bool valid() const { return value != 0; }
    [[nodiscard]] constexpr bool empty() const { return !valid(); }
    friend constexpr bool operator==(TargetOpcodeId, TargetOpcodeId) = default;

    template <typename TargetOpcode>
        requires std::is_enum_v<TargetOpcode>
    friend constexpr bool operator==(TargetOpcodeId left,
                                     TargetOpcode right) {
        return left.value == static_cast<std::uint32_t>(right);
    }

    template <typename TargetOpcode>
        requires std::is_enum_v<TargetOpcode>
    friend constexpr bool operator==(TargetOpcode left,
                                     TargetOpcodeId right) {
        return right == left;
    }
};

// Integer modes name values by their representation width, not a host type.
// A target may use any positive width, including target-specific extended ones.
struct IntegerMode {
    std::uint16_t bits{};

    [[nodiscard]] constexpr bool valid() const { return bits != 0; }
    friend constexpr bool operator==(IntegerMode, IntegerMode) = default;
};

inline constexpr IntegerMode i1{1};
inline constexpr IntegerMode i8{8};
inline constexpr IntegerMode i16{16};
inline constexpr IntegerMode i32{32};
inline constexpr IntegerMode i64{64};
inline constexpr IntegerMode i128{128};

enum class RegisterKind { Virtual, Physical };

// The representation width alone cannot distinguish an integer/pointer value
// from a scalar floating or vector value. Targets use this class to choose a
// physical register bank while keeping the Machine IR contract independent of
// architecture-specific register names.
enum class VirtualRegisterClass { Integer, Floating, Vector, Memory };

struct Register {
    RegisterKind kind{RegisterKind::Virtual};
    std::uint32_t id{};
    IntegerMode mode{};

    [[nodiscard]] static constexpr Register virtual_register(VirtualRegisterId id,
                                                               IntegerMode mode) {
        return {RegisterKind::Virtual, id.value, mode};
    }
    [[nodiscard]] static constexpr Register physical_register(PhysicalRegisterId id,
                                                                IntegerMode mode) {
        return {RegisterKind::Physical, id.value, mode};
    }
    friend constexpr bool operator==(Register, Register) = default;
};

struct RegisterOperand {
    Register value;
    friend bool operator==(const RegisterOperand&,
                           const RegisterOperand&) = default;
};

struct ImmediateOperand {
    std::uint64_t value{};
    std::uint64_t high{};
    IntegerMode mode{};
    bool is_signed{};
    friend bool operator==(const ImmediateOperand&,
                           const ImmediateOperand&) = default;
};

// The target chooses the relocation spelling and code model.  The symbol name
// is the canonical HIR linkage name when the operand originates in Cross.
struct SymbolOperand {
    std::string name;
    std::int64_t addend{};
    bool is_function{};
    std::optional<hir::ObjectId> object;
    friend bool operator==(const SymbolOperand&,
                           const SymbolOperand&) = default;
};

struct BlockOperand {
    BlockId target;
    friend bool operator==(const BlockOperand&,
                           const BlockOperand&) = default;
};

struct StackSlotOperand {
    StackSlotId slot;
    std::int32_t offset{};
    IntegerMode mode{};
    friend bool operator==(const StackSlotOperand&,
                           const StackSlotOperand&) = default;
};

using Operand = std::variant<RegisterOperand, ImmediateOperand, SymbolOperand,
                             BlockOperand, StackSlotOperand>;

enum class StackSlotKind { Local, Spill, OutgoingArgument, IncomingArgument };

struct StackSlot {
    StackSlotId id;
    StackSlotKind kind{StackSlotKind::Local};
    std::uint32_t size{};
    std::uint32_t alignment{1};
    std::optional<std::int32_t> frame_offset;
    SourceLocation location;
    std::string name;
    std::optional<TargetRegisterViewId> hard_register;
    // A target may keep an explicit fallback home for a virtual register and
    // elide it after physical assignment. Elision is represented explicitly
    // so finalized-frame verification never mistakes a missing offset for a
    // partially lowered slot.
    std::optional<VirtualRegisterId> spill_for;
    // Spill homes with the same color have disjoint live ranges and may share
    // one finalized frame range.  The target allocator owns coloring; generic
    // Machine IR verification treats it as optional layout metadata.
    std::optional<std::uint32_t> frame_color;
    bool elided{};
};

struct FrameInfo {
    std::uint32_t stack_alignment{1};
    std::uint32_t local_size{};
    std::uint32_t outgoing_argument_size{};
    std::uint32_t outgoing_argument_alignment{1};
    // A target may explicitly save and reserve one physical register as the
    // stable pre-realignment CFA anchor for a frame that combines aligned
    // fixed storage with dynamic stack allocation. It need not be preserved
    // by the function's ABI before the target-owned save.
    std::optional<PhysicalRegisterId> cfa_anchor_register;
    bool has_frame_pointer{};
    bool finalized{};
};

// Patch metadata is carried independently from an immediate so legalization,
// allocation, and printing cannot accidentally turn a siteful patch source
// into an ordinary constant. The target printer defines the label immediately
// after the selected contiguous field; the data emitter may relocate `sink`
// to that label minus the field width.
struct PatchSite {
    std::uint32_t identity{};
    unsigned field_bits{};
    std::optional<hir::ObjectId> sink;
};

enum class InstructionKind {
    Target,
    Copy,
    Call,
    Branch,
    ConditionalBranch,
    IndirectBranch,
    Return,
    Unreachable,
    Trap,
};

// `defs`, `uses`, and `clobbers` describe register effects independently from
// target operand spelling.  A call's first operand is its callee (symbol or
// register). Branch operands contain their target block(s): one for Branch,
// two for ConditionalBranch. A target opcode may use the same operands but is
// otherwise interpreted solely by its target lowering/emitter.
struct Instruction {
    InstructionKind kind{InstructionKind::Target};
    TargetOpcodeId opcode;
    SourceLocation location;
    std::vector<Operand> operands;
    std::vector<Register> defs;
    std::vector<Register> uses;
    std::vector<Register> clobbers;
    // Virtual registers whose assigned physical storage must survive this
    // call. A volatile allocation is split through its spill home immediately
    // around the call; preserved allocations need no emitted move.
    std::vector<Register> live_across_call;
    // Targets may fold a comparison into a conditional operation and retain
    // its selected comparison/test opcode here.  The identity and
    // interpretation are target-owned; generic Machine IR only verifies that
    // non-target instructions use it where the contract permits.
    TargetOpcodeId condition_predicate;
    std::optional<PatchSite> patch;
    // Direct calls retain canonical identity independently from symbol
    // spelling so IPA and target ABI planning never reverse-map assembler
    // names. Indirect calls leave this empty.
    std::optional<hir::FunctionId> direct_callee;
    // Logical source types remain attached to call operands after instruction
    // selection.  Width/register class alone cannot distinguish every
    // variadic floating, vector, pointer, and by-reference boundary value.
    std::vector<hir::TypeId> call_argument_types;
    // Dense identity of model-provided incoming variadic state. It is local
    // to the function's ABI model and never appears in the operand stream.
    AbiStateId variadic_state;
    bool may_load{};
    bool may_store{};
    bool has_side_effects{};
};

struct Block {
    BlockId id;
    SourceLocation location;
    std::string label;
    std::vector<Instruction> instructions;
    std::vector<BlockId> predecessors;
    std::vector<BlockId> successors;
    bool reachable{};
};

struct Function {
    hir::FunctionId source;
    std::optional<mir::BlockId> source_entry;
    SourceLocation location;
    std::string symbol;
    AbiId abi;
    BlockId entry;
    std::vector<IntegerMode> virtual_registers;
    std::vector<VirtualRegisterClass> virtual_register_classes;
    // A target allocator may assign selected virtual registers to physical
    // storage. Unassigned values retain their spill-home fallback.
    std::vector<std::optional<PhysicalRegisterId>> virtual_register_assignments;
    // A target may declare that a pure virtual-register definition can be
    // reconstructed from an immediate at each selected use. Allocation may
    // still cache the value in a physical register; otherwise this is a
    // spill-free fallback and no materialized home is needed. The target
    // retains the defining instruction so SSA identity and diagnostics remain
    // explicit.
    // Scalar immediates and the target-independent repeated-zero vector
    // recipe may be reconstructed at uses instead of owning a spill home.
    std::vector<std::optional<ImmediateOperand>> rematerialized_immediates;
    // Physical storage this function must preserve: allocator colors from
    // its ABI-preserved set plus bridge saves required by stronger caller
    // contracts. The target frame lowerer owns the slots and unwind records.
    std::vector<PhysicalRegisterId> callee_saved_registers;
    std::vector<StackSlot> stack_slots;
    FrameInfo frame;
    std::vector<Block> blocks;
    std::vector<BlockId> layout;
    struct LocalLabel {
        hir::LabelId label;
        BlockId block;
    };
    std::vector<LocalLabel> labels;
};

struct Module {
    std::vector<Function> functions;
};

// Checks structural invariants only, so it can run before target-specific
// register allocation or instruction-form verification. It returns false when
// it emits one or more errors into `diagnostics`.
bool verify(const Module& module, Diagnostics& diagnostics);
bool verify(const Function& function, Diagnostics& diagnostics);

} // namespace cross::machine

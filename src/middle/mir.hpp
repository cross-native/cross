// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/data_ir.hpp"
#include "middle/hir.hpp"
#include "common/memory_order.hpp"
#include "common/patch_address.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace cross {
class Subtarget;
}

namespace cross::mir {

struct BlockId {
    std::uint32_t value{};
    friend bool operator==(BlockId, BlockId) = default;
};

struct ValueId {
    std::uint32_t value{};
    friend bool operator==(ValueId, ValueId) = default;
};

struct SlotId {
    std::uint32_t value{};
    friend bool operator==(SlotId, SlotId) = default;
};

struct EffectId {
    std::uint32_t value{};
    friend bool operator==(EffectId, EffectId) = default;
};

enum class ValueKind {
    Parameter,
    ConstantInteger,
    ConstantFloating,
    LabelAddress,
    FunctionAddress,
    SlotAddress,
    GlobalAddress,
    IndexedAddress,
    VariadicState,
    Unary,
    Binary,
    Cast,
    Select,
    Splat,
    ExtractElement,
    InsertElement,
    Phi,
    LifetimeStart,
    LifetimeEnd,
    DynamicStackSave,
    DynamicAlloca,
    DynamicStackRestore,
    Load,
    Store,
    PointerLoad,
    PointerStore,
    IndexedLoad,
    GlobalLoad,
    GlobalStore,
    Atomic,
    Call,
    PatchValue,
    Intrinsic,
    // A completed, effect-free void expression; never a machine register.
    VoidValue,
};
enum class IntrinsicOperation {
    Expect,
    Assume,
    Unreachable,
    Trap,
    MachineNop,
};
enum class AtomicOperation {
    Load, Store, Exchange, CompareExchange,
    FetchAdd, FetchSub, FetchAnd, FetchXor, FetchOr,
    // A source-level atomic compound operation without a direct target RMW
    // instruction. `ManagedValue::binary` identifies the update computed by
    // the target's inline compare-exchange loop.
    FetchUpdate,
    ThreadFence, SignalFence,
};
using cross::MemoryOrder;
enum class UnaryOperation { Negate, BitNot, IsZero };
enum class BinaryOperation {
    Add, Subtract, Multiply, SignedDivide, UnsignedDivide,
    SignedRemainder, UnsignedRemainder, BitAnd, BitOr, BitXor,
    ShiftLeft, ShiftRightArithmetic, ShiftRightLogical,
    RotateLeft, RotateRight,
    Equal, NotEqual, SignedLess, SignedLessEqual, SignedGreater,
    SignedGreaterEqual, UnsignedLess, UnsignedLessEqual,
    UnsignedGreater, UnsignedGreaterEqual,
};
enum class CastOperation {
    SignExtend, ZeroExtend, Truncate, Reinterpret, FloatExtend, FloatTruncate,
    SignedIntegerToFloat, UnsignedIntegerToFloat,
    FloatToSignedInteger, FloatToUnsignedInteger,
};

struct PhiIncoming {
    BlockId predecessor;
    ValueId value;
    friend bool operator==(const PhiIncoming&,
                           const PhiIncoming&) = default;
};

struct CallArgument {
    std::optional<ValueId> value;
    std::optional<SlotId> cell;
    hir::TypeId type;
    bool unnamed{};
};

struct PatchSink {
    hir::ObjectId object;
    std::uint64_t offset{};
    PatchAddressRepresentation representation{PatchAddressRepresentation::Unavailable};
    unsigned storage_bytes{};

    friend bool operator==(const PatchSink&, const PatchSink&) = default;
};

struct ManagedValue {
    struct BitFieldRegion {
        unsigned width{};
        unsigned offset{};
    };

    ValueId id;
    SourceLocation location;
    hir::TypeId type;
    ValueKind kind{ValueKind::ConstantInteger};
    UnaryOperation unary{UnaryOperation::Negate};
    BinaryOperation binary{BinaryOperation::Add};
    CastOperation cast{CastOperation::Reinterpret};
    IntrinsicOperation intrinsic{IntrinsicOperation::Expect};
    AtomicOperation atomic{AtomicOperation::Load};
    MemoryOrder memory_order{MemoryOrder::SeqCst};
    MemoryOrder failure_order{MemoryOrder::SeqCst};
    std::uint64_t integer{};
    std::uint64_t integer_high{};
    std::uint32_t parameter_index{};
    AbiStateId variadic_state;
    std::optional<SlotId> slot;
    // A bit-field access uses a carrier load/store, but its source-semantic
    // access covers only the named field. The update's carrier load is not a
    // source read of otherwise uninitialized neighboring fields.
    std::optional<BitFieldRegion> bit_field_region;
    bool bit_field_update_read{};
    std::optional<hir::FunctionId> callee;
    // Indirect calls carry their stable function TypeId and place the target
    // value first in operands. Direct calls retain only canonical callee ID.
    std::optional<hir::TypeId> call_signature;
    std::optional<hir::LabelId> label;
    // Direct global accesses name their HIR object. IndexedLoad obtains its
    // scale and result type from the pointer-typed first operand.
    std::optional<hir::ObjectId> object;
    std::optional<PatchSink> patch_sink;
    std::optional<data::AddressConstant> patch_initial_address;
    // One physical cell per source identity/concrete owner; copied uses can
    // have separate SSA values/control paths but must agree on cell metadata.
    std::uint32_t patch_id{};
    bool is_volatile_access{};
    // A source [[musttail]] return owns this call. Optimizers and targets must
    // preserve the call boundary and either emit a tail transfer or diagnose.
    bool must_tail{};
    // Proven minimum alignment for pointer-based memory operations. Zero
    // means the pointee type's natural alignment.
    unsigned memory_alignment{};
    std::optional<EffectId> effect_input;
    std::optional<EffectId> effect_output;
    std::vector<ValueId> operands;
    std::vector<CallArgument> call_arguments;
    std::vector<PhiIncoming> incoming;
};

struct ManagedSlot {
    SlotId id;
    SourceLocation location;
    hir::TypeId type;
    std::string name;
    std::optional<std::string> physical_location;
    bool is_volatile{};
    bool address_taken{};
    // `out`/`inout` parameter cells are consumed by ABI copy-out after the
    // source-level body. Optimizers must model that implicit return-edge read.
    bool live_on_return{};
    unsigned minimum_alignment{1};
    // Present only for a source parameter cell. Kept through lowering so
    // source definite-assignment checks do not infer identity from slot names.
    std::optional<std::uint32_t> source_parameter;
};

enum class EffectKind { Entry, Phi, Operation };

struct EffectIncoming {
    BlockId predecessor;
    EffectId effect;
};

struct ManagedEffect {
    EffectId id;
    SourceLocation location;
    EffectKind kind{EffectKind::Entry};
    std::optional<EffectId> input;
    std::optional<ValueId> operation;
    std::vector<EffectIncoming> incoming;
};

enum class TerminatorKind {
    None, Return, Branch, ConditionalBranch, IndirectBranch, Unreachable, Trap,
};

struct ManagedTerminator {
    TerminatorKind kind{TerminatorKind::None};
    SourceLocation location;
    std::optional<ValueId> value;
    std::vector<BlockId> successors;
    EffectId effect;
};

struct ManagedBlock {
    BlockId id;
    SourceLocation location;
    std::vector<ValueId> values;
    std::vector<BlockId> predecessors;
    ManagedTerminator terminator;
    EffectId effect;
};

struct ManagedLabel {
    hir::LabelId label;
    BlockId block;
};

struct ManagedFunction {
    hir::FunctionId source;
    SourceLocation location;
    hir::TypeId result_type;
    BlockId entry;
    std::vector<ValueId> parameters;
    std::vector<ManagedSlot> slots;
    std::vector<ManagedValue> values;
    std::vector<ManagedEffect> effects;
    std::vector<ManagedBlock> blocks;
    std::vector<ManagedLabel> labels;
};

struct ManagedModule {
    [[nodiscard]] bool owns(hir::FunctionId id) const {
        return definitions.contains(id.value);
    }
    [[nodiscard]] const ManagedFunction* find(hir::FunctionId id) const;
    [[nodiscard]] bool owns(hir::ObjectId id) const {
        return object_definitions.contains(id.value);
    }

    std::vector<ManagedFunction> functions;
    std::unordered_set<std::uint32_t> definitions;
    std::unordered_set<std::uint32_t> object_definitions;
};

ManagedModule lower_managed(hir::Module& hir_module,
                            const Subtarget& subtarget,
                            const CompilerOptions& options,
                            Diagnostics& diagnostics);
bool verify(const ManagedModule& module, const hir::Module& hir_module,
            Diagnostics& diagnostics);
void optimize(ManagedModule& module, hir::Module& hir_module,
              const Subtarget& subtarget, const CompilerOptions& options,
              Diagnostics& diagnostics);

// Materialize native-register-width views of proven non-wrapping unsigned
// loop inductions. Targets whose integer registers are wider than their
// pointer-sized source mode can call this bridge after the target-independent
// pipeline, avoiding a repeated extension at every wide use while preserving
// the source recurrence's modular semantics.
bool promote_native_induction_views(ManagedModule& module,
                                    hir::Module& hir_module,
                                    unsigned native_integer_bits);

// Specializes only calls that survived the inline phase. The driver invokes
// this after its first whole-program ABI analysis; newly created private
// variants are then included in the final boundary plan.
bool specialize_surviving_calls(ManagedModule& module,
                                hir::Module& hir_module,
                                const CompilerOptions& options);

} // namespace cross::mir

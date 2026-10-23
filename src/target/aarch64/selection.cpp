// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/selection.hpp"

#include "backend/native/machine_transform.hpp"
#include "middle/mir_analysis.hpp"
#include "target/aarch64/abi.hpp"
#include "target/aarch64/machine.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cross::aarch64 {
namespace {

using machine::Instruction;
using machine::Register;

machine::Operand register_operand(Register value) { return machine::RegisterOperand{value}; }

machine::Operand immediate_operand(std::uint64_t value, bool is_signed = false) {
    return machine::ImmediateOperand{value, 0, machine::i64, is_signed};
}

machine::Operand block_operand(std::uint32_t block) {
    return machine::BlockOperand{machine::BlockId{block}};
}

constexpr std::uint64_t low_mask(unsigned bits) {
    return bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1U;
}

constexpr std::uint64_t sign_extended(std::uint64_t value, unsigned bits) {
    if (bits >= 64) return value;
    const auto sign = std::uint64_t{1} << (bits - 1);
    value &= low_mask(bits);
    return (value ^ sign) - sign;
}

// The width an operation on a value of `bits` computes in: a w register for
// at most 32 bits, an x register above.
constexpr unsigned operation_bits(unsigned bits) { return bits > 32 ? 64U : 32U; }

machine::IntegerMode mode_of_bits(unsigned bits) {
    return {static_cast<std::uint16_t>(bits)};
}

bool preemptible(const hir::Function& function, const CompilerOptions& options) {
    if (function.linkage == Linkage::Static) return false;
    if (!function.definition) return true;
    if (function.linkage != Linkage::Global || options.pie) return false;
    return options.semantic_interposition;
}

bool preemptible(const hir::Object& object, const CompilerOptions& options) {
    if (object.linkage == Linkage::Static || options.direct_external_data) return false;
    if (!object.definition) return true;
    if (object.linkage != Linkage::Global || options.pie) return false;
    return options.semantic_interposition;
}

std::optional<Condition> comparison_condition(mir::BinaryOperation operation) {
    using mir::BinaryOperation;
    switch (operation) {
    case BinaryOperation::Equal: return Condition::Eq;
    case BinaryOperation::NotEqual: return Condition::Ne;
    case BinaryOperation::SignedLess: return Condition::Lt;
    case BinaryOperation::SignedLessEqual: return Condition::Le;
    case BinaryOperation::SignedGreater: return Condition::Gt;
    case BinaryOperation::SignedGreaterEqual: return Condition::Ge;
    case BinaryOperation::UnsignedLess: return Condition::Lo;
    case BinaryOperation::UnsignedLessEqual: return Condition::Ls;
    case BinaryOperation::UnsignedGreater: return Condition::Hi;
    case BinaryOperation::UnsignedGreaterEqual: return Condition::Hs;
    default: return std::nullopt;
    }
}

// The condition that holds with the operands exchanged.
Condition swapped(Condition condition) {
    switch (condition) {
    case Condition::Lt: return Condition::Gt;
    case Condition::Gt: return Condition::Lt;
    case Condition::Le: return Condition::Ge;
    case Condition::Ge: return Condition::Le;
    case Condition::Lo: return Condition::Hi;
    case Condition::Hi: return Condition::Lo;
    case Condition::Ls: return Condition::Hs;
    case Condition::Hs: return Condition::Ls;
    default: return condition;
    }
}

bool signed_condition(Condition condition) {
    return condition == Condition::Lt || condition == Condition::Le ||
           condition == Condition::Gt || condition == Condition::Ge;
}

// Whether an immediate offset fits a load or store of `bytes`: scaled and
// unsigned, or unscaled from -256 to 255.
bool encodable_offset(std::int64_t offset, unsigned bytes) {
    if (offset >= -256 && offset <= 255) return true;
    return offset >= 0 && offset % bytes == 0 && offset / bytes <= 4095;
}

// An address operand of a load or store: a frame slot plus an offset, a
// base plus an offset, a base plus a shifted index, or a base plus the low
// 12 bits of a symbol.
struct Address {
    enum class Kind { Slot, Base, Indexed, Symbol } kind{Kind::Base};
    machine::StackSlotId slot{};
    Register base{};
    Register index{};
    std::int64_t offset{};
    unsigned shift{};
    machine::SymbolOperand symbol{};

    static Address in_slot(machine::StackSlotId slot, std::int64_t offset = 0) {
        Address result;
        result.kind = Kind::Slot;
        result.slot = slot;
        result.offset = offset;
        return result;
    }
    static Address at(Register base, std::int64_t offset = 0) {
        Address result;
        result.base = base;
        result.offset = offset;
        return result;
    }
    static Address indexed(Register base, Register index, unsigned shift) {
        Address result;
        result.kind = Kind::Indexed;
        result.base = base;
        result.index = index;
        result.shift = shift;
        return result;
    }
    static Address low(Register page, machine::SymbolOperand symbol) {
        Address result;
        result.kind = Kind::Symbol;
        result.base = page;
        result.symbol = std::move(symbol);
        return result;
    }
};

class Selector {
public:
    Selector(const hir::Module& hir_module, const Subtarget& subtarget,
             const CompilerOptions& options, Diagnostics& diagnostics)
        : hir_(hir_module), subtarget_(subtarget), target_(subtarget.target()),
          options_(options), diagnostics_(diagnostics) {}

    std::optional<SelectedFunction> select(const mir::ManagedFunction& source) {
        const auto errors = diagnostics_.errors();
        result_ = {};
        source_ = &source;
        reported_.clear();
        value_blocks_.clear();
        captures_.clear();
        wide_.clear();
        aggregates_.clear();
        result_addresses_.clear();
        entry_addresses_.clear();
        entry_code_.clear();
        pending_phi_copies_.clear();
        auto& function = result_.function;
        const auto& entity = hir_.function(source.source);
        function.source = source.source;
        function.source_entry = source.entry;
        function.location = source.location;
        function.symbol = entity.link_symbol;
        function.abi = entity.abi;
        function.entry = {source.entry.value};
        function.frame.stack_alignment = 16;
        function.frame.outgoing_argument_alignment = 16;
        function.frame.has_frame_pointer = true;
        result_.returns = mir::has_reachable_return(source);
        abi_ = abi_model(entity.abi);
        if (!abi_) {
            diagnostics_.error(source.location,
                               "the function's ABI has no AArch64 model entry");
            return std::nullopt;
        }
        layout_ = classify_function(hir_, entity, subtarget_, *abi_);
        if (!layout_) {
            diagnostics_.error(source.location,
                               "the AArch64 ABI '" + abi_->canonical_name +
                                   "' cannot place this function's parameters and result");
            return std::nullopt;
        }
        for (const auto& result : layout_->results) {
            if (result.indirect) continue;
            for (const auto& piece : result.pieces) {
                if (piece.location.kind != LocationKind::Register) continue;
                if (const auto physical = physical_register(piece.location.reg)) {
                    result_.result_registers.push_back(*physical);
                }
            }
        }
        count_uses();
        create_slots();
        capture_result_addresses();
        create_registers();
        find_fused_comparisons();
        for (const auto& block : source.blocks) lower_block(block);
        for (const auto& label : source.labels) {
            function.labels.push_back({label.label, {label.block.value}});
        }
        // Incoming registers are captured before any other entry code can
        // overwrite them.
        for (auto& block : function.blocks) {
            if (block.id != function.entry) continue;
            block.instructions.insert(block.instructions.begin(),
                                      std::make_move_iterator(entry_code_.begin()),
                                      std::make_move_iterator(entry_code_.end()));
            block.instructions.insert(block.instructions.begin(),
                                      std::make_move_iterator(captures_.begin()),
                                      std::make_move_iterator(captures_.end()));
        }
        if (options_.machine_dce) {
            (void)native::eliminate_dead_definitions(function);
        }
        if (diagnostics_.errors() != errors) return std::nullopt;
        return std::move(result_);
    }

private:
    // --- Registers and instructions ---

    Register new_register(unsigned bits) {
        auto& function = result_.function;
        const machine::VirtualRegisterId id{
            static_cast<std::uint32_t>(function.virtual_registers.size())};
        const auto mode = mode_of_bits(bits);
        function.virtual_registers.push_back(mode);
        function.virtual_register_classes.push_back(machine::VirtualRegisterClass::Integer);
        function.virtual_register_assignments.push_back(std::nullopt);
        function.rematerialized_immediates.push_back(std::nullopt);
        return Register::virtual_register(id, mode);
    }

    const mir::ManagedValue& value(mir::ValueId id) const { return source_->values[id.value]; }

    unsigned bits_of(mir::ValueId id) const { return type_bits(hir_, value(id).type); }

    Register reg(mir::ValueId id) {
        if (id.value < registers_.size() && registers_[id.value]) return *registers_[id.value];
        diagnostics_.error(value(id).location,
                           "AArch64 selection met a value without a register");
        return new_register(64);
    }

    Instruction target(Opcode opcode) const {
        Instruction result;
        result.kind = machine::InstructionKind::Target;
        result.opcode = machine_opcode(opcode);
        result.location = location_;
        result.inline_site = site_;
        return result;
    }

    Instruction& emit(Instruction instruction) {
        instruction.location = location_;
        instruction.inline_site = site_;
        block_->instructions.push_back(std::move(instruction));
        return block_->instructions.back();
    }

    // An instruction that defines `to` from registers and immediates.
    Instruction& define(Opcode opcode, Register to, std::initializer_list<Register> from,
                        std::initializer_list<std::uint64_t> immediates = {}) {
        auto result = target(opcode);
        for (const auto source : from) {
            result.operands.push_back(register_operand(source));
            result.uses.push_back(source);
        }
        for (const auto immediate : immediates) result.operands.push_back(immediate_operand(immediate));
        result.defs.push_back(to);
        return emit(std::move(result));
    }

    Register compute(Opcode opcode, unsigned bits, std::initializer_list<Register> from,
                     std::initializer_list<std::uint64_t> immediates = {}) {
        const auto to = new_register(bits);
        define(opcode, to, from, immediates);
        return to;
    }

    void copy(Register to, Register from) {
        Instruction result;
        result.kind = machine::InstructionKind::Copy;
        result.defs = {to};
        result.uses = {from};
        emit(std::move(result));
    }

    void constant(Register to, std::uint64_t bits_value) {
        define(Opcode::MovImmediate, to, {}, {bits_value & low_mask(operation_bits(to.mode.bits))});
    }

    Register constant(unsigned bits, std::uint64_t bits_value) {
        const auto to = new_register(bits);
        constant(to, bits_value);
        return to;
    }

    // The value of a constant integer operand, if it is one.
    std::optional<std::uint64_t> constant_of(mir::ValueId id) const {
        const auto& source = value(id);
        if (source.kind != mir::ValueKind::ConstantInteger) return std::nullopt;
        return source.integer;
    }

    // `from` of `bits` sign- or zero-extended to `to_bits`.
    Register extend(Register from, unsigned bits, bool is_signed, unsigned to_bits = 32) {
        if (bits >= to_bits) return from;
        const auto opcode = bits <= 8    ? (is_signed ? Opcode::Sxtb : Opcode::Uxtb)
                            : bits <= 16 ? (is_signed ? Opcode::Sxth : Opcode::Uxth)
                                         : (is_signed ? Opcode::Sxtw : Opcode::Uxtw);
        return compute(opcode, to_bits, {from});
    }

    // Reports each unsupported construct once per function, where it first
    // occurs.
    void unsupported(SourceLocation location, std::string_view what) {
        if (!reported_.insert(std::string(what)).second) return;
        diagnostics_.error(location,
                           "AArch64 code generation does not yet support " + std::string(what));
    }

    // A 128-bit integer, which MIR keeps only at interfaces and the casts,
    // shifts, and bitwise operations that join and split its halves.
    bool wide_integer(hir::TypeId type) const {
        const auto& entry = hir_.type(type);
        return entry.kind == hir::Type::Kind::Builtin &&
               (entry.builtin == BuiltinType::I128 || entry.builtin == BuiltinType::U128);
    }

    // Why the backend cannot hold a value of `type`, or empty.
    std::string_view unsupported_type(hir::TypeId type) const {
        if (is_void(hir_, type) || is_general_scalar(hir_, type) || wide_integer(type)) return {};
        if (is_floating(hir_, type)) return "floating-point values";
        if (is_vector(hir_, type)) return "vector values";
        if (is_aggregate(hir_, type)) {
            return hir::natural_size(hir_, type, target_) ? std::string_view{}
                                                          : "record and array values without a size";
        }
        if (type_bits(hir_, type) > 64) return "128-bit integers";
        return "values of this type";
    }

    // --- Function setup ---

    void count_uses() {
        uses_.assign(source_->values.size(), 0);
        const auto use = [&](mir::ValueId id) {
            if (id.value < uses_.size()) ++uses_[id.value];
        };
        for (const auto& item : source_->values) {
            for (const auto operand : item.operands) use(operand);
            for (const auto& argument : item.call_arguments) {
                if (argument.value) use(*argument.value);
            }
            for (const auto& incoming : item.incoming) use(incoming.value);
        }
        for (const auto& block : source_->blocks) {
            if (block.terminator.value) use(*block.terminator.value);
        }
    }

    bool has_result(const mir::ManagedValue& item) const {
        if (is_void(hir_, item.type)) return false;
        return item.kind != mir::ValueKind::LifetimeStart &&
               item.kind != mir::ValueKind::LifetimeEnd && item.kind != mir::ValueKind::Store &&
               item.kind != mir::ValueKind::PointerStore &&
               item.kind != mir::ValueKind::GlobalStore && item.kind != mir::ValueKind::VoidValue;
    }

    void create_registers() {
        registers_.assign(source_->values.size(), std::nullopt);
        for (const auto& item : source_->values) {
            if (!has_result(item)) continue;
            if (const auto why = unsupported_type(item.type); !why.empty()) {
                unsupported(item.location, why);
                continue;
            }
            if (wide_integer(item.type)) {
                const auto low = new_register(64);
                wide_.emplace(item.id.value, std::pair{low, new_register(64)});
                continue;
            }
            if (is_aggregate(hir_, item.type)) {
                aggregates_.emplace(item.id.value,
                                    temporary_slot(aggregate_bytes(item.type), 16,
                                                   "$aggregate." + std::to_string(item.id.value),
                                                   item.location));
                continue;
            }
            registers_[item.id.value] = new_register(type_bits(hir_, item.type));
        }
    }

    bool held(mir::ValueId id) const {
        return registers_[id.value].has_value() || wide_.contains(id.value) ||
               aggregates_.contains(id.value);
    }

    // --- Aggregates ---

    // A record or array value lives in a frame slot of its own; operations
    // on it copy bytes, and interfaces move its pieces.
    std::uint32_t aggregate_bytes(hir::TypeId type) const {
        return static_cast<std::uint32_t>(hir::natural_size(hir_, type, target_).value_or(1));
    }

    unsigned aggregate_alignment(hir::TypeId type) const {
        return static_cast<unsigned>(hir::natural_alignment(hir_, type, target_).value_or(1));
    }

    machine::StackSlotId temporary_slot(std::uint32_t size, std::uint32_t alignment,
                                        std::string name, SourceLocation location) {
        auto& function = result_.function;
        machine::StackSlot slot;
        slot.id = {static_cast<std::uint32_t>(function.stack_slots.size())};
        slot.kind = machine::StackSlotKind::Local;
        slot.size = std::max<std::uint32_t>(size, 1);
        slot.alignment = alignment;
        slot.location = location;
        slot.name = std::move(name);
        function.stack_slots.push_back(std::move(slot));
        return function.stack_slots.back().id;
    }

    // `address` plus `delta`, in a form that reaches an access of `bytes`.
    Address advanced(const Address& address, std::int64_t delta, unsigned bytes) {
        switch (address.kind) {
        case Address::Kind::Slot: return Address::in_slot(address.slot, address.offset + delta);
        case Address::Kind::Base: {
            if (encodable_offset(address.offset + delta, bytes)) {
                return Address::at(address.base, address.offset + delta);
            }
            const auto sum = new_register(64);
            add_offset(sum, address.base, address.offset + delta);
            return Address::at(sum);
        }
        case Address::Kind::Indexed: {
            const auto sum = new_register(64);
            define(Opcode::AddShifted, sum, {address.base, address.index}, {address.shift});
            return advanced(Address::at(sum), delta, bytes);
        }
        case Address::Kind::Symbol: {
            const auto sum = new_register(64);
            auto low = target(Opcode::AddLow);
            low.operands.push_back(register_operand(address.base));
            low.operands.push_back(address.symbol);
            low.uses.push_back(address.base);
            low.defs.push_back(sum);
            emit(std::move(low));
            return advanced(Address::at(sum), delta, bytes);
        }
        }
        return address;
    }

    // The largest access of at most 8 bytes that `alignment` allows.
    static unsigned chunk_bytes(std::uint64_t remaining, unsigned alignment) {
        unsigned bytes = 8;
        while (bytes > 1 && (bytes > remaining || bytes > alignment)) bytes /= 2;
        return bytes;
    }

    // Copies `size` bytes in accesses that both addresses' `alignment`
    // allows.
    void copy_bytes(const Address& to, const Address& from, std::uint64_t size,
                    unsigned alignment) {
        auto destination = to;
        auto source = from;
        // A register base and its index or symbol are folded first.
        if (destination.kind == Address::Kind::Indexed || destination.kind == Address::Kind::Symbol) {
            destination = advanced(destination, 0, 8);
        }
        if (source.kind == Address::Kind::Indexed || source.kind == Address::Kind::Symbol) {
            source = advanced(source, 0, 8);
        }
        for (std::uint64_t offset = 0; offset < size;) {
            const auto bytes = chunk_bytes(size - offset, alignment);
            const auto data = new_register(bytes * 8U > 32 ? 64 : 32);
            load_bytes(data, advanced(source, static_cast<std::int64_t>(offset), bytes), bytes,
                       false, false);
            store(data, advanced(destination, static_cast<std::int64_t>(offset), bytes), bytes);
            offset += bytes;
        }
    }

    // The low `bits` of the bytes at `address`, which is `alignment`
    // aligned, in a register; the bits above them are zero.
    Register load_piece(const Address& address, unsigned bits, unsigned alignment) {
        const auto total = (bits + 7U) / 8U;
        std::optional<Register> value;
        for (unsigned offset = 0; offset < total;) {
            const auto bytes = chunk_bytes(total - offset, alignment);
            auto part = new_register(64);
            load_bytes(part, advanced(address, offset, bytes), bytes, false, false);
            if (offset != 0) part = compute(Opcode::LslImmediate, 64, {part}, {offset * 8U});
            value = value ? compute(Opcode::Orr, 64, {*value, part}) : part;
            offset += bytes;
        }
        return *value;
    }

    // Stores the low `bits` of `data` at `address`, which is `alignment`
    // aligned.
    void store_piece(Register data, const Address& address, unsigned bits, unsigned alignment) {
        const auto total = (bits + 7U) / 8U;
        for (unsigned offset = 0; offset < total;) {
            const auto bytes = chunk_bytes(total - offset, alignment);
            const auto part =
                offset == 0 ? data : compute(Opcode::LsrImmediate, 64, {data}, {offset * 8U});
            store(part, advanced(address, offset, bytes), bytes);
            offset += bytes;
        }
    }

    // The alignment that an offset into a 16-byte aligned slot keeps.
    static unsigned offset_alignment(std::uint64_t offset) {
        return offset == 0 ? 16U : std::min<unsigned>(16, static_cast<unsigned>(offset & (~offset + 1U)));
    }

    // Copies and interface moves of record and array values.
    void lower_aggregate(const mir::ManagedValue& item) {
        using mir::ValueKind;
        const auto type = item.kind == ValueKind::Store || item.kind == ValueKind::PointerStore ||
                                  item.kind == ValueKind::GlobalStore
                              ? value(item.operands.back()).type
                              : item.type;
        const auto size = aggregate_bytes(type);
        const auto alignment = std::min(16U, aggregate_alignment(type));
        const auto own = [&](mir::ValueId id) { return Address::in_slot(aggregates_.at(id.value)); };
        switch (item.kind) {
        case ValueKind::Load:
            copy_bytes(own(item.id), Address::in_slot({item.slot->value}), size, alignment);
            return;
        case ValueKind::Store:
            copy_bytes(Address::in_slot({item.slot->value}), own(item.operands.front()), size,
                       alignment);
            return;
        case ValueKind::PointerLoad:
            copy_bytes(own(item.id), address_of(item.operands.front(), 8), size,
                       item.memory_alignment != 0 ? std::min(alignment, item.memory_alignment)
                                                  : alignment);
            return;
        case ValueKind::PointerStore:
            copy_bytes(address_of(item.operands[0], 8), own(item.operands[1]), size,
                       item.memory_alignment != 0 ? std::min(alignment, item.memory_alignment)
                                                  : alignment);
            return;
        case ValueKind::IndexedLoad:
            copy_bytes(own(item.id),
                       indexed_address(item.operands[0], item.operands[1],
                                       storage_size(hir_, item.type, target_), 8),
                       size, alignment);
            return;
        case ValueKind::GlobalLoad:
        case ValueKind::GlobalStore: {
            const auto& object = hir_.object(*item.object);
            if (object.is_thread_local) {
                unsupported(item.location, "thread-local objects");
                return;
            }
            const auto address = object_address(*item.object, 0, 1);
            if (item.kind == ValueKind::GlobalLoad) {
                copy_bytes(own(item.id), address, size, alignment);
            } else {
                copy_bytes(address, own(item.operands.front()), size, alignment);
            }
            return;
        }
        case ValueKind::Cast:
            if (item.cast == mir::CastOperation::Reinterpret &&
                aggregates_.contains(item.operands.front().value)) {
                copy_bytes(own(item.id), own(item.operands.front()), size, 16);
                return;
            }
            break;
        case ValueKind::Select: {
            // A copy from the selected value's slot.
            const auto truth = slot_address(aggregates_.at(item.operands[1].value), 0);
            const auto falsity = slot_address(aggregates_.at(item.operands[2].value), 0);
            Condition condition = Condition::Ne;
            if (fused_.contains(item.operands[0].value)) {
                condition = emit_comparison(value(item.operands[0]));
            } else {
                compare_zero(reg(item.operands[0]), 8);
            }
            const auto chosen = new_register(64);
            define(Opcode::Csel, chosen, {truth, falsity}, {static_cast<std::uint64_t>(condition)})
                .uses.push_back(flags_register());
            copy_bytes(own(item.id), Address::at(chosen), size, 16);
            return;
        }
        case ValueKind::Phi: {
            // A PHI of the incoming values' slot addresses, which the entry
            // computes, and a copy from the chosen slot after the block's
            // PHIs, through a temporary so that PHIs may exchange values.
            auto instruction = target(Opcode::Phi);
            for (const auto& incoming : item.incoming) {
                const auto source = entry_address(aggregates_.at(incoming.value.value));
                instruction.operands.push_back(block_operand(incoming.predecessor.value));
                instruction.operands.push_back(register_operand(source));
                instruction.uses.push_back(source);
            }
            const auto chosen = new_register(64);
            instruction.defs.push_back(chosen);
            emit(std::move(instruction));
            pending_phi_copies_.push_back(
                {chosen, temporary_slot(size, 16, "$aggregate.phi", item.location),
                 aggregates_.at(item.id.value), size});
            return;
        }
        default: break;
        }
        unsupported(item.location, "record and array values that convert to other types");
    }

    // The address of a slot, computed once at the entry, where it holds
    // everywhere.
    Register entry_address(machine::StackSlotId slot) {
        if (const auto found = entry_addresses_.find(slot.value); found != entry_addresses_.end()) {
            return found->second;
        }
        const auto address = new_register(64);
        auto instruction = target(Opcode::StackAddress);
        instruction.operands.push_back(machine::StackSlotOperand{slot, 0, machine::i64});
        instruction.defs.push_back(address);
        entry_code_.push_back(std::move(instruction));
        entry_addresses_.emplace(slot.value, address);
        return address;
    }

    // Copies the values the block's aggregate PHIs chose, once every PHI
    // has read its source.
    void flush_phi_copies() {
        for (const auto& copy : pending_phi_copies_) {
            copy_bytes(Address::in_slot(copy.temporary), Address::at(copy.source), copy.size, 16);
        }
        for (const auto& copy : pending_phi_copies_) {
            copy_bytes(Address::in_slot(copy.destination), Address::in_slot(copy.temporary),
                       copy.size, 16);
        }
        pending_phi_copies_.clear();
    }

    // The registers of a value: its own, or the low and high halves of a
    // 128-bit integer.
    std::vector<Register> chunks(mir::ValueId id) {
        if (const auto found = wide_.find(id.value); found != wide_.end()) {
            return {found->second.first, found->second.second};
        }
        return {reg(id)};
    }

    // The register that holds ABI piece `piece` of a value with `parts`:
    // its bits shifted down to bit 0; the bits above them are unspecified.
    std::optional<Register> piece_of(const std::vector<Register>& parts, const ValuePiece& piece) {
        const auto chunk = piece.value_bit_offset / 64U;
        const auto inner = piece.value_bit_offset % 64U;
        if (chunk >= parts.size() || inner + piece.value_bits > 64U) return std::nullopt;
        const auto source = parts[chunk];
        if (inner == 0) return source;
        const auto width = operation_bits(source.mode.bits);
        return compute(Opcode::LsrImmediate, width, {source}, {inner});
    }

    // Defines `targets`, a value's registers, from the registers that hold
    // its ABI pieces.
    void compose(const std::vector<Register>& targets,
                 const std::vector<std::pair<Register, const ValuePiece*>>& parts) {
        for (std::size_t chunk = 0; chunk < targets.size(); ++chunk) {
            const auto to = targets[chunk];
            const auto width = operation_bits(to.mode.bits);
            std::optional<Register> value;
            for (const auto& [source, piece] : parts) {
                if (piece->value_bit_offset / 64U != chunk) continue;
                const auto inner = piece->value_bit_offset % 64U;
                auto part = source;
                // A narrower piece's carrier bits above it are unspecified.
                if (piece->value_bits < to.mode.bits && parts.size() > 1) {
                    part = compute(Opcode::AndImmediate, width, {part},
                                   {low_mask(piece->value_bits)});
                }
                if (inner != 0) part = compute(Opcode::LslImmediate, width, {part}, {inner});
                value = value ? compute(Opcode::Orr, width, {*value, part}) : part;
            }
            if (value) {
                copy(to, *value);
            } else {
                constant(to, 0);
            }
        }
    }

    void create_slots() {
        auto& function = result_.function;
        for (const auto& slot : source_->slots) {
            machine::StackSlot target;
            target.id = {static_cast<std::uint32_t>(function.stack_slots.size())};
            target.kind = machine::StackSlotKind::Local;
            target.size = storage_size(hir_, slot.type, target_);
            target.alignment = std::max(
                {static_cast<unsigned>(hir::layout_alignment(hir_, slot.type, target_).value_or(1)),
                 static_cast<unsigned>(hir::natural_alignment(hir_, slot.type, target_).value_or(1)),
                 slot.minimum_alignment, 1U});
            target.location = slot.location;
            target.name = slot.name;
            if (target.size == 0) {
                unsupported(slot.location, "stack objects without a fixed size");
                target.size = 1;
            }
            if (target.alignment > 16) {
                unsupported(slot.location, "stack objects aligned to more than 16 bytes");
                target.alignment = 16;
            }
            if (slot.physical_location && *slot.physical_location != "auto") {
                unsupported(slot.location, "register-bound local objects");
            }
            function.stack_slots.push_back(std::move(target));
        }
    }

    // A result in memory arrives as the addresses of its pieces, which the
    // entry captures; an ABI that returns the address takes it back.
    void capture_result_addresses() {
        if (layout_->results.empty() || !layout_->results.front().indirect) return;
        const auto& result = layout_->results.front();
        for (const auto& piece : result.pieces) {
            const auto physical = piece.location.kind == LocationKind::Register
                                      ? physical_register(piece.location.reg)
                                      : std::nullopt;
            if (!physical || *physical >= stack_pointer) {
                unsupported(source_->location, "result addresses that the ABI passes on the stack");
                return;
            }
            const auto address = new_register(64);
            Instruction capture;
            capture.kind = machine::InstructionKind::Copy;
            capture.location = source_->location;
            capture.defs = {address};
            capture.uses = {gpr(*physical, 64)};
            captures_.push_back(std::move(capture));
            result_addresses_.emplace_back(address, &piece);
        }
        if (!result.indirect_result_reg.empty()) {
            if (const auto physical = physical_register(result.indirect_result_reg)) {
                result_.result_registers.push_back(*physical);
            }
        }
    }

    machine::StackSlotId outgoing_slot(std::uint32_t size) {
        auto& function = result_.function;
        if (!result_.outgoing_arguments) {
            machine::StackSlot slot;
            slot.id = {static_cast<std::uint32_t>(function.stack_slots.size())};
            slot.kind = machine::StackSlotKind::OutgoingArgument;
            slot.size = size;
            slot.alignment = 16;
            slot.location = function.location;
            slot.name = "$outgoing";
            result_.outgoing_arguments = slot.id;
            function.stack_slots.push_back(std::move(slot));
        }
        auto& slot = function.stack_slots[result_.outgoing_arguments->value];
        slot.size = std::max(slot.size, size);
        function.frame.outgoing_argument_size = slot.size;
        return *result_.outgoing_arguments;
    }

    machine::StackSlotId incoming_slot(std::uint32_t offset, std::uint32_t size,
                                       SourceLocation location) {
        auto& function = result_.function;
        machine::StackSlot slot;
        slot.id = {static_cast<std::uint32_t>(function.stack_slots.size())};
        slot.kind = machine::StackSlotKind::IncomingArgument;
        slot.size = size;
        // The frame size is a multiple of 16, so the offset's alignment holds.
        slot.alignment = std::min({std::bit_floor(size), 16U,
                                   offset == 0 ? 16U : (offset & (~offset + 1U))});
        slot.location = location;
        slot.name = "$incoming." + std::to_string(offset);
        result_.incoming_arguments.emplace_back(slot.id, offset);
        function.stack_slots.push_back(std::move(slot));
        return function.stack_slots.back().id;
    }

    // Comparisons whose only use is a select or branch of their own block
    // are emitted with it.
    void find_fused_comparisons() {
        fused_.clear();
        const auto fusable = [&](mir::ValueId condition, std::uint32_t block) {
            const auto& compare = value(condition);
            return compare.kind == mir::ValueKind::Binary &&
                   comparison_condition(compare.binary) && uses_[condition.value] == 1 &&
                   block_of(condition) == block && compare.operands.size() == 2 &&
                   is_general_scalar(hir_, value(compare.operands[0]).type);
        };
        for (const auto& block : source_->blocks) {
            for (const auto id : block.values) {
                const auto& item = value(id);
                if (options_.compare_select_fusion && item.kind == mir::ValueKind::Select &&
                    !item.operands.empty() && fusable(item.operands[0], block.id.value)) {
                    fused_.insert(item.operands[0].value);
                }
            }
            if (options_.compare_branch_fusion &&
                block.terminator.kind == mir::TerminatorKind::ConditionalBranch &&
                block.terminator.value && fusable(*block.terminator.value, block.id.value)) {
                fused_.insert(block.terminator.value->value);
            }
        }
    }

    std::optional<std::uint32_t> block_of(mir::ValueId id) {
        if (value_blocks_.empty()) {
            value_blocks_.assign(source_->values.size(), std::numeric_limits<std::uint32_t>::max());
            for (const auto& block : source_->blocks) {
                for (const auto item : block.values) value_blocks_[item.value] = block.id.value;
            }
        }
        const auto block = value_blocks_[id.value];
        if (block == std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
        return block;
    }

    // --- Blocks ---

    void lower_block(const mir::ManagedBlock& block) {
        auto& function = result_.function;
        machine::Block target;
        target.id = {block.id.value};
        target.location = block.location;
        target.label = "mir.bb." + std::to_string(block.id.value);
        target.reachable = true;
        for (const auto predecessor : block.predecessors) {
            target.predecessors.push_back({predecessor.value});
        }
        for (const auto successor : block.terminator.successors) {
            target.successors.push_back({successor.value});
        }
        function.blocks.push_back(std::move(target));
        function.layout.push_back({block.id.value});
        block_ = &function.blocks.back();
        tail_call_ = false;
        for (const auto id : block.values) {
            const auto& item = value(id);
            location_ = item.location;
            site_ = item.inline_site;
            if (tail_call_) {
                // A tail transfer leaves the frame; what follows it in MIR
                // ends the lifetimes of its slots.
                if (item.kind == mir::ValueKind::LifetimeEnd) continue;
            }
            if (fused_.contains(id.value)) continue;
            if (item.kind != mir::ValueKind::Phi && !pending_phi_copies_.empty()) {
                flush_phi_copies();
            }
            lower_value(item);
        }
        location_ = block.terminator.location;
        site_ = block.terminator.inline_site;
        if (!pending_phi_copies_.empty()) flush_phi_copies();
        lower_terminator(block.terminator);
    }

    // --- Values ---

    void lower_value(const mir::ManagedValue& item) {
        using mir::ValueKind;
        // A value of a type the backend cannot hold was reported where it
        // is defined.
        for (const auto operand : item.operands) {
            if (!held(operand)) return;
        }
        if (has_result(item) && !held(item.id)) return;
        if (item.kind != ValueKind::Call && item.kind != ValueKind::Parameter &&
            (aggregates_.contains(item.id.value) ||
             std::any_of(item.operands.begin(), item.operands.end(),
                         [&](mir::ValueId operand) { return aggregates_.contains(operand.value); }) ||
             std::any_of(item.incoming.begin(), item.incoming.end(),
                         [&](const mir::PhiIncoming& incoming) {
                             return aggregates_.contains(incoming.value.value);
                         }))) {
            lower_aggregate(item);
            return;
        }
        if (item.kind != ValueKind::Call && item.kind != ValueKind::Parameter &&
            (wide_.contains(item.id.value) ||
             std::any_of(item.operands.begin(), item.operands.end(),
                         [&](mir::ValueId operand) { return wide_.contains(operand.value); }) ||
             std::any_of(item.incoming.begin(), item.incoming.end(),
                         [&](const mir::PhiIncoming& incoming) {
                             return wide_.contains(incoming.value.value);
                         }))) {
            lower_wide(item);
            return;
        }
        switch (item.kind) {
        case ValueKind::Parameter: lower_parameter(item); return;
        case ValueKind::ConstantInteger: constant(reg(item.id), item.integer); return;
        case ValueKind::ConstantFloating: unsupported(item.location, "floating-point values"); return;
        case ValueKind::LabelAddress: lower_label_address(item); return;
        case ValueKind::FunctionAddress:
            symbol_address(reg(item.id),
                           {hir_.function(*item.callee).link_symbol, 0, true, std::nullopt,
                            item.callee},
                           hir_.function(*item.callee).fixed_address);
            return;
        case ValueKind::GlobalAddress: {
            const auto& object = hir_.object(*item.object);
            if (object.is_thread_local) {
                unsupported(item.location, "thread-local objects");
                return;
            }
            symbol_address(reg(item.id), {object.link_symbol, 0, false, item.object},
                           object.fixed_address);
            return;
        }
        case ValueKind::SlotAddress: {
            auto instruction = target(Opcode::StackAddress);
            instruction.operands.push_back(
                machine::StackSlotOperand{{item.slot->value}, 0, machine::i64});
            instruction.defs.push_back(reg(item.id));
            emit(std::move(instruction));
            return;
        }
        case ValueKind::IndexedAddress: lower_indexed_address(item); return;
        case ValueKind::Unary: lower_unary(item); return;
        case ValueKind::Binary: lower_binary(item); return;
        case ValueKind::Cast: lower_cast(item); return;
        case ValueKind::Select: lower_select(item); return;
        case ValueKind::Phi: {
            auto instruction = target(Opcode::Phi);
            for (const auto& incoming : item.incoming) {
                const auto source = reg(incoming.value);
                instruction.operands.push_back(block_operand(incoming.predecessor.value));
                instruction.operands.push_back(register_operand(source));
                instruction.uses.push_back(source);
            }
            instruction.defs.push_back(reg(item.id));
            emit(std::move(instruction));
            return;
        }
        case ValueKind::LifetimeStart:
        case ValueKind::LifetimeEnd:
        case ValueKind::VoidValue: return;
        case ValueKind::Load: {
            const auto bytes = (bits_of(item.id) + 7U) / 8U;
            load(item, reg(item.id), Address::in_slot({item.slot->value}), bytes);
            return;
        }
        case ValueKind::Store: {
            const auto data = item.operands.front();
            store(reg(data), Address::in_slot({item.slot->value}),
                  (bits_of(data) + 7U) / 8U);
            return;
        }
        case ValueKind::PointerLoad: {
            const auto bytes = (bits_of(item.id) + 7U) / 8U;
            load(item, reg(item.id), address_of(item.operands.front(), bytes), bytes);
            return;
        }
        case ValueKind::PointerStore: {
            const auto data = item.operands[1];
            const auto bytes = (bits_of(data) + 7U) / 8U;
            store(reg(data), address_of(item.operands[0], bytes), bytes);
            return;
        }
        case ValueKind::IndexedLoad: {
            const auto bytes = (bits_of(item.id) + 7U) / 8U;
            load(item, reg(item.id),
                 indexed_address(item.operands[0], item.operands[1],
                                 storage_size(hir_, item.type, target_), bytes),
                 bytes);
            return;
        }
        case ValueKind::GlobalLoad:
        case ValueKind::GlobalStore: {
            const auto& object = hir_.object(*item.object);
            if (object.is_thread_local) {
                unsupported(item.location, "thread-local objects");
                return;
            }
            const bool loads = item.kind == ValueKind::GlobalLoad;
            const auto data = loads ? item.id : item.operands.front();
            const auto bytes = (bits_of(data) + 7U) / 8U;
            const auto address = object_address(*item.object, 0, bytes);
            if (loads) {
                load(item, reg(item.id), address, bytes);
            } else {
                store(reg(data), address, bytes);
            }
            return;
        }
        case ValueKind::Call: lower_call(item); return;
        case ValueKind::Intrinsic: lower_intrinsic(item); return;
        case ValueKind::VariadicState: unsupported(item.location, "variadic functions"); return;
        case ValueKind::Splat:
        case ValueKind::ExtractElement:
        case ValueKind::InsertElement: unsupported(item.location, "vector values"); return;
        case ValueKind::DynamicStackSave:
        case ValueKind::DynamicAlloca:
        case ValueKind::DynamicStackRestore:
            unsupported(item.location, "variable-length arrays");
            return;
        case ValueKind::Atomic: unsupported(item.location, "atomic operations"); return;
        case ValueKind::PatchValue: unsupported(item.location, "$::patch values"); return;
        case ValueKind::MachineInstruction:
            unsupported(item.location, "machine instruction built-ins");
            return;
        }
    }

    // The forms that join and split 128-bit integers, whose other
    // operations MIR has expanded into operations on the halves.
    void lower_wide(const mir::ManagedValue& item) {
        using mir::ValueKind;
        const auto halves = [&](mir::ValueId id) { return wide_.at(id.value); };
        switch (item.kind) {
        case ValueKind::ConstantInteger: {
            const auto [low, high] = halves(item.id);
            constant(low, item.integer);
            constant(high, item.integer_high);
            return;
        }
        case ValueKind::Cast: {
            const auto source = item.operands.front();
            const bool from_wide = wide_.contains(source.value);
            const bool to_wide = wide_.contains(item.id.value);
            if (from_wide && to_wide) {
                const auto [low, high] = halves(source);
                const auto [to_low, to_high] = halves(item.id);
                copy(to_low, low);
                copy(to_high, high);
                return;
            }
            if (from_wide) {
                if (item.cast != mir::CastOperation::Truncate &&
                    item.cast != mir::CastOperation::Reinterpret) {
                    break;
                }
                copy(reg(item.id), halves(source).first);
                return;
            }
            if (item.cast != mir::CastOperation::ZeroExtend &&
                item.cast != mir::CastOperation::SignExtend) {
                break;
            }
            const bool is_signed = item.cast == mir::CastOperation::SignExtend;
            const auto [low, high] = halves(item.id);
            const auto extended = extend(reg(source), bits_of(source), is_signed, 64);
            copy(low, extended);
            if (is_signed) {
                define(Opcode::AsrImmediate, high, {low}, {63});
            } else {
                constant(high, 0);
            }
            return;
        }
        case ValueKind::Binary: {
            const auto [left_low, left_high] = halves(item.operands[0]);
            const auto [low, high] = halves(item.id);
            if (item.binary == mir::BinaryOperation::BitAnd ||
                item.binary == mir::BinaryOperation::BitOr ||
                item.binary == mir::BinaryOperation::BitXor) {
                const auto opcode = item.binary == mir::BinaryOperation::BitAnd ? Opcode::And
                                    : item.binary == mir::BinaryOperation::BitOr ? Opcode::Orr
                                                                                 : Opcode::Eor;
                const auto [right_low, right_high] = halves(item.operands[1]);
                define(opcode, low, {left_low, right_low});
                define(opcode, high, {left_high, right_high});
                return;
            }
            const auto count = constant_of(item.operands[1]);
            const bool left_shift = item.binary == mir::BinaryOperation::ShiftLeft;
            const bool arithmetic = item.binary == mir::BinaryOperation::ShiftRightArithmetic;
            if (!count || (!left_shift && !arithmetic &&
                           item.binary != mir::BinaryOperation::ShiftRightLogical)) {
                break;
            }
            const auto amount = static_cast<unsigned>(*count % 128U);
            // The half the bits move from and into, and the other half.
            const auto from = left_shift ? left_low : left_high;
            const auto other = left_shift ? left_high : left_low;
            const auto near = left_shift ? high : low;
            const auto far = left_shift ? low : high;
            const auto shift = left_shift   ? Opcode::LslImmediate
                               : arithmetic ? Opcode::AsrImmediate
                                            : Opcode::LsrImmediate;
            if (amount == 0) {
                copy(low, left_low);
                copy(high, left_high);
            } else if (amount >= 64) {
                if (amount == 64) {
                    copy(near, from);
                } else {
                    define(shift, near, {from}, {amount - 64U});
                }
                if (arithmetic) {
                    define(Opcode::AsrImmediate, far, {from}, {63});
                } else {
                    constant(far, 0);
                }
            } else {
                const auto moved = compute(left_shift ? Opcode::LsrImmediate : Opcode::LslImmediate,
                                           64, {from}, {64U - amount});
                const auto kept = compute(left_shift ? Opcode::LslImmediate : Opcode::LsrImmediate,
                                          64, {other}, {amount});
                define(Opcode::Orr, near, {kept, moved});
                define(shift, far, {from}, {amount});
            }
            return;
        }
        case ValueKind::Phi:
            for (const bool high_half : {false, true}) {
                auto instruction = target(Opcode::Phi);
                for (const auto& incoming : item.incoming) {
                    const auto pair = halves(incoming.value);
                    const auto source = high_half ? pair.second : pair.first;
                    instruction.operands.push_back(block_operand(incoming.predecessor.value));
                    instruction.operands.push_back(register_operand(source));
                    instruction.uses.push_back(source);
                }
                const auto pair = halves(item.id);
                instruction.defs.push_back(high_half ? pair.second : pair.first);
                emit(std::move(instruction));
            }
            return;
        case ValueKind::Select: {
            Condition condition = Condition::Ne;
            if (fused_.contains(item.operands[0].value)) {
                condition = emit_comparison(value(item.operands[0]));
            } else {
                compare_zero(reg(item.operands[0]), 8);
            }
            const auto truth = halves(item.operands[1]);
            const auto falsity = halves(item.operands[2]);
            const auto [low, high] = halves(item.id);
            for (const auto& [to, pick] : {std::pair{low, std::pair{truth.first, falsity.first}},
                                           std::pair{high, std::pair{truth.second, falsity.second}}}) {
                define(Opcode::Csel, to, {pick.first, pick.second},
                       {static_cast<std::uint64_t>(condition)})
                    .uses.push_back(flags_register());
            }
            return;
        }
        default: break;
        }
        diagnostics_.error(item.location,
                           "AArch64 selection met an unexpanded 128-bit integer operation");
    }

    void lower_parameter(const mir::ManagedValue& item) {
        const auto& arguments = layout_->call.arguments;
        if (item.parameter_index >= arguments.size()) {
            diagnostics_.error(item.location, "AArch64 parameter has no ABI placement");
            return;
        }
        const auto& assignment = arguments[item.parameter_index];
        if (const auto found = aggregates_.find(item.id.value); found != aggregates_.end()) {
            lower_aggregate_parameter(item, assignment, found->second);
            return;
        }
        if (assignment.indirect) {
            unsupported(item.location, "parameters that the ABI passes by reference");
            return;
        }
        const auto targets = chunks(item.id);
        // One piece for the whole value goes straight to its register.
        const bool whole = targets.size() == 1 && assignment.pieces.size() == 1 &&
                           assignment.pieces.front().value_bit_offset == 0;
        std::vector<std::pair<Register, const ValuePiece*>> parts;
        for (const auto& piece : assignment.pieces) {
            const auto bits = std::min<unsigned>(64, std::max<unsigned>(piece.value_bits, 8));
            const auto to = whole ? targets.front() : new_register(bits);
            if (piece.location.kind == LocationKind::Register) {
                const auto physical = physical_register(piece.location.reg);
                if (!physical || *physical >= stack_pointer) {
                    unsupported(item.location, "parameters in SIMD and floating-point registers");
                    return;
                }
                Instruction capture;
                capture.kind = machine::InstructionKind::Copy;
                capture.location = location_;
                capture.inline_site = site_;
                capture.defs = {to};
                capture.uses = {gpr(*physical, to.mode.bits)};
                captures_.push_back(std::move(capture));
            } else {
                const auto bytes = (to.mode.bits + 7U) / 8U;
                const auto slot = incoming_slot(
                    static_cast<std::uint32_t>(callee_stack_offset(piece, *abi_)), bytes,
                    item.location);
                load_bytes(to, Address::in_slot(slot), bytes,
                           is_signed_integer(hir_, item.type), false);
            }
            parts.emplace_back(to, &piece);
        }
        if (!whole) compose(targets, parts);
    }

    // The register or stack value of an incoming piece, captured at entry.
    std::optional<Register> incoming_piece(const ValuePiece& piece, unsigned bits,
                                           SourceLocation location) {
        const auto to = new_register(bits);
        if (piece.location.kind == LocationKind::Register) {
            const auto physical = physical_register(piece.location.reg);
            if (!physical || *physical >= stack_pointer) {
                unsupported(location, "parameters in SIMD and floating-point registers");
                return std::nullopt;
            }
            Instruction capture;
            capture.kind = machine::InstructionKind::Copy;
            capture.location = location_;
            capture.inline_site = site_;
            capture.defs = {to};
            capture.uses = {gpr(*physical, to.mode.bits)};
            captures_.push_back(std::move(capture));
            return to;
        }
        const auto bytes = (bits + 7U) / 8U;
        const auto slot = incoming_slot(
            static_cast<std::uint32_t>(callee_stack_offset(piece, *abi_)), bytes, location);
        load_bytes(to, Address::in_slot(slot), bytes, false, false);
        return to;
    }

    // A record or array parameter: its pieces, or copies of what the
    // addresses that the ABI passes instead point at, fill its slot.
    void lower_aggregate_parameter(const mir::ManagedValue& item,
                                   const ArgumentAssignment& assignment,
                                   machine::StackSlotId slot) {
        const auto alignment = std::min(16U, aggregate_alignment(item.type));
        for (const auto& piece : assignment.pieces) {
            const auto offset = piece.value_bit_offset / 8U;
            if (piece.value_bit_offset % 8U != 0) {
                unsupported(item.location, "record pieces that do not start at a byte");
                return;
            }
            if (assignment.indirect) {
                const auto address = incoming_piece(piece, 64, item.location);
                if (!address) return;
                copy_bytes(Address::in_slot(slot, offset), Address::at(*address),
                           (piece.indirect_value_bits + 7U) / 8U, alignment);
                continue;
            }
            if (piece.location.kind == LocationKind::Stack) {
                const auto bytes = (piece.value_bits + 7U) / 8U;
                const auto stack_offset =
                    static_cast<std::uint32_t>(callee_stack_offset(piece, *abi_));
                const auto incoming = incoming_slot(stack_offset, bytes, item.location);
                copy_bytes(Address::in_slot(slot, offset), Address::in_slot(incoming), bytes,
                           std::min(offset_alignment(offset), offset_alignment(stack_offset)));
                continue;
            }
            const auto bits = piece.value_bits > 32 ? 64U : 32U;
            const auto part = incoming_piece(piece, bits, item.location);
            if (!part) return;
            store_piece(*part, Address::in_slot(slot, offset), piece.value_bits,
                        offset_alignment(offset));
        }
    }

    void lower_label_address(const mir::ManagedValue& item) {
        const auto* entity = item.label ? &hir_.labels.at(item.label->value) : nullptr;
        if (entity && (entity->is_global || entity->owner != source_->source)) {
            const auto symbol = entity->is_global ? entity->link_symbol
                                                  : hir::local_label_symbol(hir_, *entity);
            symbol_address(reg(item.id),
                           {symbol, 0, true, std::nullopt, entity->owner, entity->id},
                           std::nullopt);
            return;
        }
        const auto found = std::find_if(source_->labels.begin(), source_->labels.end(),
                                        [&](const mir::ManagedLabel& label) {
                                            return item.label && label.label == *item.label;
                                        });
        if (found == source_->labels.end()) {
            diagnostics_.error(item.location, "AArch64 label address has no target block");
            return;
        }
        auto instruction = target(Opcode::Adr);
        instruction.operands.push_back(block_operand(found->block.value));
        instruction.defs.push_back(reg(item.id));
        emit(std::move(instruction));
    }

    // The address of a symbol: adrp and add, or adrp and a load of its GOT
    // entry; a fixed address is a constant.
    void symbol_address(Register to, machine::SymbolOperand symbol,
                        std::optional<std::uint64_t> fixed_address) {
        if (fixed_address) {
            constant(to, *fixed_address + static_cast<std::uint64_t>(symbol.addend));
            return;
        }
        const bool got = reaches_through_got(hir_, options_, symbol);
        const auto page = new_register(64);
        auto high = target(got ? Opcode::AdrpGot : Opcode::Adrp);
        high.operands.push_back(symbol);
        high.defs.push_back(page);
        emit(std::move(high));
        auto low = target(got ? Opcode::LoadGot : Opcode::AddLow);
        low.operands.push_back(register_operand(page));
        low.operands.push_back(std::move(symbol));
        low.uses.push_back(page);
        low.defs.push_back(to);
        low.may_load = got;
        emit(std::move(low));
    }

    // The address of `object` plus `offset` for an access of `bytes`.
    Address object_address(hir::ObjectId id, std::int64_t offset, unsigned bytes) {
        const auto& object = hir_.object(id);
        machine::SymbolOperand symbol{object.link_symbol, offset, false, id};
        const auto alignment = std::max<std::uint64_t>(
            {hir::layout_alignment(hir_, object.type, target_).value_or(1),
             object.minimum_alignment, 1});
        if (!object.fixed_address && !reaches_through_got(hir_, options_, symbol) &&
            alignment >= bytes && offset % static_cast<std::int64_t>(bytes) == 0) {
            const auto page = new_register(64);
            auto high = target(Opcode::Adrp);
            high.operands.push_back(symbol);
            high.defs.push_back(page);
            emit(std::move(high));
            return Address::low(page, std::move(symbol));
        }
        const auto address = new_register(64);
        symbol_address(address, std::move(symbol), object.fixed_address);
        return Address::at(address);
    }

    std::uint64_t element_size(const mir::ManagedValue& pointer) const {
        const auto& type = hir_.type(pointer.type);
        return type.pointee ? storage_size(hir_, *type.pointee, target_) : 0U;
    }

    // base + index * size into `to`.
    void scaled_sum(Register to, Register base, mir::ValueId index, std::uint64_t size) {
        if (const auto known = constant_of(index)) {
            add_offset(to, base, static_cast<std::int64_t>(*known * size));
            return;
        }
        const auto scaled = reg(index);
        if (size == 0) {
            copy(to, base);
        } else if (std::has_single_bit(size)) {
            define(Opcode::AddShifted, to, {base, scaled},
                   {static_cast<std::uint64_t>(std::countr_zero(size))});
        } else {
            const auto factor = constant(64, size);
            define(Opcode::Madd, to, {scaled, factor, base});
        }
    }

    void add_offset(Register to, Register base, std::int64_t offset) {
        const auto magnitude = static_cast<std::uint64_t>(offset < 0 ? -offset : offset);
        if (offset == 0) {
            copy(to, base);
        } else if (arithmetic_immediate(magnitude)) {
            define(offset < 0 ? Opcode::SubImmediate : Opcode::AddImmediate, to, {base},
                   {magnitude});
        } else {
            define(Opcode::Add, to, {base, constant(64, static_cast<std::uint64_t>(offset))});
        }
    }

    void lower_indexed_address(const mir::ManagedValue& item) {
        scaled_sum(reg(item.id), reg(item.operands[0]), item.operands[1], element_size(item));
    }

    // The value a pointer reinterprets, through any chain of such casts.
    mir::ValueId through_casts(mir::ValueId id) const {
        while (value(id).kind == mir::ValueKind::Cast &&
               value(id).cast == mir::CastOperation::Reinterpret &&
               hir_.type(value(id).type).kind == hir::Type::Kind::Pointer &&
               hir_.type(value(value(id).operands.front()).type).kind ==
                   hir::Type::Kind::Pointer) {
            id = value(id).operands.front();
        }
        return id;
    }

    // The address an access of `bytes` through `pointer` uses, folding a
    // frame slot, a constant displacement, or a scaled index.
    Address address_of(mir::ValueId pointer, unsigned bytes) {
        const auto& source = value(through_casts(pointer));
        if (source.kind == mir::ValueKind::SlotAddress) {
            return Address::in_slot({source.slot->value});
        }
        if (source.kind == mir::ValueKind::IndexedAddress) {
            return indexed_address(source.operands[0], source.operands[1], element_size(source),
                                   bytes);
        }
        return Address::at(reg(pointer));
    }

    Address indexed_address(mir::ValueId base, mir::ValueId index, std::uint64_t size,
                            unsigned bytes) {
        const auto& origin = value(through_casts(base));
        if (const auto known = constant_of(index)) {
            const auto offset = static_cast<std::int64_t>(*known * size);
            if (origin.kind == mir::ValueKind::SlotAddress && offset >= 0 &&
                offset <= std::numeric_limits<std::int32_t>::max()) {
                return Address::in_slot({origin.slot->value}, offset);
            }
            if (encodable_offset(offset, bytes)) {
                return Address::at(reg(base), offset);
            }
        } else if (size == bytes || size == 1) {
            return Address::indexed(
                reg(base), reg(index),
                size == 1 ? 0U : static_cast<unsigned>(std::countr_zero(size)));
        }
        const auto address = new_register(64);
        scaled_sum(address, reg(base), index, size);
        return Address::at(address);
    }

    void append_address(Instruction& instruction, const Address& address) {
        switch (address.kind) {
        case Address::Kind::Slot:
            instruction.operands.push_back(machine::StackSlotOperand{
                address.slot, static_cast<std::int32_t>(address.offset), machine::i64});
            break;
        case Address::Kind::Base:
            instruction.operands.push_back(register_operand(address.base));
            instruction.operands.push_back(
                immediate_operand(static_cast<std::uint64_t>(address.offset), true));
            instruction.uses.push_back(address.base);
            break;
        case Address::Kind::Indexed:
            instruction.operands.push_back(register_operand(address.base));
            instruction.operands.push_back(register_operand(address.index));
            instruction.operands.push_back(immediate_operand(address.shift));
            instruction.uses.push_back(address.base);
            instruction.uses.push_back(address.index);
            break;
        case Address::Kind::Symbol:
            instruction.operands.push_back(register_operand(address.base));
            instruction.operands.push_back(address.symbol);
            instruction.uses.push_back(address.base);
            break;
        }
    }

    void load(const mir::ManagedValue& item, Register to, const Address& address,
              unsigned bytes) {
        load_bytes(to, address, bytes, is_signed_integer(hir_, item.type),
                   item.is_volatile_access);
    }

    void load_bytes(Register to, const Address& address, unsigned bytes, bool is_signed,
                    bool is_volatile) {
        const auto opcode = bytes == 1   ? (is_signed ? Opcode::LoadS8 : Opcode::LoadU8)
                            : bytes == 2 ? (is_signed ? Opcode::LoadS16 : Opcode::LoadU16)
                            : bytes == 4 ? Opcode::Load32
                                         : Opcode::Load64;
        auto instruction = target(opcode);
        append_address(instruction, address);
        instruction.defs.push_back(to);
        instruction.may_load = true;
        instruction.has_side_effects = is_volatile;
        emit(std::move(instruction));
    }

    void store(Register data, const Address& address, unsigned bytes) {
        const auto opcode = bytes == 1   ? Opcode::Store8
                            : bytes == 2 ? Opcode::Store16
                            : bytes == 4 ? Opcode::Store32
                                         : Opcode::Store64;
        auto instruction = target(opcode);
        instruction.operands.push_back(register_operand(data));
        instruction.uses.push_back(data);
        append_address(instruction, address);
        instruction.may_store = true;
        instruction.has_side_effects = true;
        emit(std::move(instruction));
    }

    // --- Integer operations ---

    void lower_unary(const mir::ManagedValue& item) {
        const auto operand = item.operands.front();
        const auto from = reg(operand);
        const auto to = reg(item.id);
        switch (item.unary) {
        case mir::UnaryOperation::Negate: define(Opcode::Neg, to, {from}); return;
        case mir::UnaryOperation::BitNot: define(Opcode::Mvn, to, {from}); return;
        case mir::UnaryOperation::IsZero: {
            const auto bits = bits_of(operand);
            compare_zero(from, bits);
            define(Opcode::Cset, to, {}, {static_cast<std::uint64_t>(Condition::Eq)})
                .uses.push_back(flags_register());
            return;
        }
        }
    }

    // Sets the flags as a comparison of `from` of `bits` with zero.
    void compare_zero(Register from, unsigned bits) {
        auto instruction =
            bits < 32 ? target(Opcode::TstImmediate) : target(Opcode::CmpImmediate);
        instruction.operands.push_back(register_operand(from));
        instruction.operands.push_back(immediate_operand(bits < 32 ? low_mask(bits) : 0));
        instruction.uses.push_back(from);
        instruction.defs.push_back(flags_register());
        emit(std::move(instruction));
    }

    void flag_instruction(Opcode opcode, std::initializer_list<Register> from,
                          std::optional<std::uint64_t> immediate = std::nullopt) {
        auto instruction = target(opcode);
        for (const auto source : from) {
            instruction.operands.push_back(register_operand(source));
            instruction.uses.push_back(source);
        }
        if (immediate) instruction.operands.push_back(immediate_operand(*immediate));
        instruction.defs.push_back(flags_register());
        emit(std::move(instruction));
    }

    // Emits the comparison and returns the condition that is true when it
    // holds.
    Condition emit_comparison(const mir::ManagedValue& compare) {
        auto condition = *comparison_condition(compare.binary);
        auto left = compare.operands[0];
        auto right = compare.operands[1];
        if (constant_of(left) && !constant_of(right)) {
            std::swap(left, right);
            condition = swapped(condition);
        }
        const auto bits = bits_of(left);
        const auto width = operation_bits(bits);
        const bool is_signed = signed_condition(condition);
        const bool equality = condition == Condition::Eq || condition == Condition::Ne;
        auto known = constant_of(right);
        auto a = reg(left);
        if (bits < 32 && equality && known && (*known & low_mask(bits)) == 0) {
            compare_zero(a, bits);
            return condition;
        }
        if (bits < 32) {
            a = extend(a, bits, is_signed);
            if (known) {
                known = is_signed ? sign_extended(*known, bits) & low_mask(32)
                                  : *known & low_mask(bits);
            }
        } else if (known) {
            known = *known & low_mask(width);
        }
        if (known) {
            const auto negated = (~*known + 1U) & low_mask(width);
            if (arithmetic_immediate(*known)) {
                flag_instruction(Opcode::CmpImmediate, {a}, *known);
            } else if (arithmetic_immediate(negated)) {
                flag_instruction(Opcode::CmnImmediate, {a}, negated);
            } else {
                flag_instruction(Opcode::Cmp, {a, constant(width, *known)});
            }
            return condition;
        }
        auto b = reg(right);
        if (bits < 32) b = extend(b, bits, is_signed);
        flag_instruction(Opcode::Cmp, {a, b});
        return condition;
    }

    void lower_binary(const mir::ManagedValue& item) {
        using mir::BinaryOperation;
        const auto to = reg(item.id);
        if (comparison_condition(item.binary)) {
            const auto holds = emit_comparison(item);
            define(Opcode::Cset, to, {}, {static_cast<std::uint64_t>(holds)})
                .uses.push_back(flags_register());
            return;
        }
        auto left = item.operands[0];
        auto right = item.operands[1];
        const auto bits = bits_of(item.id);
        const auto width = operation_bits(bits);
        const bool commutative =
            item.binary == BinaryOperation::Add || item.binary == BinaryOperation::Multiply ||
            item.binary == BinaryOperation::BitAnd || item.binary == BinaryOperation::BitOr ||
            item.binary == BinaryOperation::BitXor;
        if (commutative && constant_of(left) && !constant_of(right)) std::swap(left, right);
        const auto known = constant_of(right);
        const auto value_of = [&](std::uint64_t bits_value) { return bits_value & low_mask(width); };
        switch (item.binary) {
        case BinaryOperation::Add:
        case BinaryOperation::Subtract: {
            const bool add = item.binary == BinaryOperation::Add;
            if (known) {
                const auto k = value_of(*known);
                const auto negated = value_of(~*known + 1U);
                if (arithmetic_immediate(k)) {
                    define(add ? Opcode::AddImmediate : Opcode::SubImmediate, to, {reg(left)},
                           {k});
                    return;
                }
                if (arithmetic_immediate(negated)) {
                    define(add ? Opcode::SubImmediate : Opcode::AddImmediate, to, {reg(left)},
                           {negated});
                    return;
                }
            }
            define(add ? Opcode::Add : Opcode::Sub, to, {reg(left), reg(right)});
            return;
        }
        case BinaryOperation::Multiply:
            define(Opcode::Mul, to, {reg(left), reg(right)});
            return;
        case BinaryOperation::SignedDivide:
        case BinaryOperation::UnsignedDivide:
        case BinaryOperation::SignedRemainder:
        case BinaryOperation::UnsignedRemainder: {
            const bool is_signed = item.binary == BinaryOperation::SignedDivide ||
                                   item.binary == BinaryOperation::SignedRemainder;
            const bool remainder = item.binary == BinaryOperation::SignedRemainder ||
                                   item.binary == BinaryOperation::UnsignedRemainder;
            const auto dividend = extend(reg(left), bits, is_signed);
            const auto divisor = extend(reg(right), bits, is_signed);
            const auto opcode = is_signed ? Opcode::Sdiv : Opcode::Udiv;
            if (!remainder) {
                define(opcode, to, {dividend, divisor});
                return;
            }
            const auto quotient = compute(opcode, width, {dividend, divisor});
            define(Opcode::Msub, to, {quotient, divisor, dividend});
            return;
        }
        case BinaryOperation::BitAnd:
        case BinaryOperation::BitOr:
        case BinaryOperation::BitXor: {
            const auto immediate = item.binary == BinaryOperation::BitAnd ? Opcode::AndImmediate
                                   : item.binary == BinaryOperation::BitOr ? Opcode::OrrImmediate
                                                                           : Opcode::EorImmediate;
            const auto plain = item.binary == BinaryOperation::BitAnd ? Opcode::And
                               : item.binary == BinaryOperation::BitOr ? Opcode::Orr
                                                                       : Opcode::Eor;
            if (known && logical_immediate(value_of(*known), width)) {
                define(immediate, to, {reg(left)}, {value_of(*known)});
                return;
            }
            define(plain, to, {reg(left), reg(right)});
            return;
        }
        case BinaryOperation::ShiftLeft:
        case BinaryOperation::ShiftRightLogical:
        case BinaryOperation::ShiftRightArithmetic: {
            const bool left_shift = item.binary == BinaryOperation::ShiftLeft;
            const bool arithmetic = item.binary == BinaryOperation::ShiftRightArithmetic;
            // A right shift of a narrow value reads its extended bits.
            const auto shifted =
                left_shift ? reg(left) : extend(reg(left), bits, arithmetic);
            if (known && *known < width) {
                define(left_shift   ? Opcode::LslImmediate
                       : arithmetic ? Opcode::AsrImmediate
                                    : Opcode::LsrImmediate,
                       to, {shifted}, {*known});
                return;
            }
            define(left_shift ? Opcode::Lslv : arithmetic ? Opcode::Asrv : Opcode::Lsrv, to,
                   {shifted, amount(right, width)});
            return;
        }
        case BinaryOperation::RotateLeft:
        case BinaryOperation::RotateRight: {
            // MIR leaves rotations at the widths that rorv has.
            const bool left_rotate = item.binary == BinaryOperation::RotateLeft;
            if (known) {
                const auto count = *known % width;
                define(Opcode::RorImmediate, to, {reg(left)},
                       {left_rotate ? (width - count) % width : count});
                return;
            }
            auto count = amount(right, width);
            if (left_rotate) count = compute(Opcode::Neg, width, {count});
            define(Opcode::Rorv, to, {reg(left), count});
            return;
        }
        default:
            unsupported(item.location, "this integer operation");
            return;
        }
    }

    // A shift amount, which the instruction reads at the operation's width.
    Register amount(mir::ValueId id, unsigned width) {
        const auto bits = bits_of(id);
        return bits < width ? extend(reg(id), bits, false, width) : reg(id);
    }

    void lower_cast(const mir::ManagedValue& item) {
        using mir::CastOperation;
        const auto operand = item.operands.front();
        const auto from = reg(operand);
        const auto to = reg(item.id);
        const auto from_bits = bits_of(operand);
        const auto to_bits = bits_of(item.id);
        switch (item.cast) {
        case CastOperation::SignExtend:
        case CastOperation::ZeroExtend: {
            const bool is_signed = item.cast == CastOperation::SignExtend;
            if (from_bits >= to_bits) {
                copy(to, from);
                return;
            }
            const auto opcode = from_bits <= 8    ? (is_signed ? Opcode::Sxtb : Opcode::Uxtb)
                                : from_bits <= 16 ? (is_signed ? Opcode::Sxth : Opcode::Uxth)
                                                  : (is_signed ? Opcode::Sxtw : Opcode::Uxtw);
            define(opcode, to, {from});
            return;
        }
        case CastOperation::Truncate:
        case CastOperation::Reinterpret: copy(to, from); return;
        default: unsupported(item.location, "floating-point conversions"); return;
        }
    }

    void lower_select(const mir::ManagedValue& item) {
        const auto condition_value = item.operands[0];
        Condition condition = Condition::Ne;
        if (fused_.contains(condition_value.value)) {
            condition = emit_comparison(value(condition_value));
        } else {
            compare_zero(reg(condition_value), 8);
        }
        define(Opcode::Csel, reg(item.id), {reg(item.operands[1]), reg(item.operands[2])},
               {static_cast<std::uint64_t>(condition)})
            .uses.push_back(flags_register());
    }

    void lower_intrinsic(const mir::ManagedValue& item) {
        using mir::IntrinsicOperation;
        switch (item.intrinsic) {
        case IntrinsicOperation::Expect: copy(reg(item.id), reg(item.operands.front())); return;
        case IntrinsicOperation::Assume:
        case IntrinsicOperation::Unreachable: return;
        case IntrinsicOperation::MachineNop: {
            auto instruction = target(Opcode::Nop);
            instruction.has_side_effects = true;
            emit(std::move(instruction));
            return;
        }
        case IntrinsicOperation::Trap: {
            auto instruction = target(Opcode::Brk);
            instruction.operands.push_back(immediate_operand(1000));
            instruction.has_side_effects = true;
            emit(std::move(instruction));
            return;
        }
        // MIR leaves these at the widths that the cost model gives
        // instructions for.
        case IntrinsicOperation::Clz:
            define(Opcode::Clz, reg(item.id), {reg(item.operands.front())});
            return;
        case IntrinsicOperation::Ctz: {
            const auto operand = item.operands.front();
            const auto reversed = compute(Opcode::Rbit, bits_of(operand), {reg(operand)});
            define(Opcode::Clz, reg(item.id), {reversed});
            return;
        }
        case IntrinsicOperation::Bswap: {
            const auto operand = item.operands.front();
            define(bits_of(operand) <= 16 ? Opcode::Rev16 : Opcode::Rev, reg(item.id),
                   {reg(operand)});
            return;
        }
        default:
            unsupported(item.location, mir::floating_intrinsic_operation(item.intrinsic)
                                           ? "floating-point intrinsics"
                                           : "this intrinsic");
            return;
        }
    }

    // --- Calls ---

    // An argument piece and the register that holds it.
    struct Placement {
        Register source;
        const ValuePiece* piece;
    };

    Register slot_address(machine::StackSlotId slot, std::int32_t offset) {
        const auto address = new_register(64);
        auto instruction = target(Opcode::StackAddress);
        instruction.operands.push_back(machine::StackSlotOperand{slot, offset, machine::i64});
        instruction.defs.push_back(address);
        emit(std::move(instruction));
        return address;
    }

    // The pieces of a record or array argument: loads from its slot, or the
    // addresses of a copy that the callee owns.
    bool aggregate_argument(const mir::ManagedValue& call, const mir::CallArgument& argument,
                            const ArgumentAssignment& assignment,
                            std::vector<Placement>& placements) {
        const auto slot = aggregates_.at(argument.value->value);
        const auto size = aggregate_bytes(argument.type);
        std::optional<machine::StackSlotId> copy_slot;
        // The copy would leave with the caller's frame.
        if (assignment.indirect && call.must_tail) {
            unsupported(call.location, "[[musttail]] calls that pass records by reference");
            return false;
        }
        if (assignment.indirect) {
            copy_slot = temporary_slot(size, 16, "$argument.copy", call.location);
            copy_bytes(Address::in_slot(*copy_slot), Address::in_slot(slot), size, 16);
        }
        for (const auto& piece : assignment.pieces) {
            if (piece.value_bit_offset % 8U != 0) {
                unsupported(call.location, "record pieces that do not start at a byte");
                return false;
            }
            const auto offset = piece.value_bit_offset / 8U;
            if (copy_slot) {
                placements.push_back({slot_address(*copy_slot, static_cast<std::int32_t>(offset)),
                                      &piece});
                continue;
            }
            if (piece.location.kind == LocationKind::Stack) {
                if (call.must_tail) {
                    unsupported(call.location, "[[musttail]] calls with stack arguments");
                    return false;
                }
                copy_bytes(Address::in_slot(*result_.outgoing_arguments,
                                            static_cast<std::int64_t>(piece.location.stack_offset)),
                           Address::in_slot(slot, offset), (piece.value_bits + 7U) / 8U,
                           std::min(offset_alignment(offset),
                                    offset_alignment(piece.location.stack_offset)));
                continue;
            }
            placements.push_back({load_piece(Address::in_slot(slot, offset), piece.value_bits,
                                             offset_alignment(offset)),
                                  &piece});
        }
        return true;
    }

    // Physical registers that a list of model register names gives.
    void append_clobbers(Instruction& call, const std::vector<std::string>& names) {
        for (const auto& name : names) {
            const auto physical = physical_register(name);
            if (!physical || *physical == stack_pointer) continue;
            const auto bits = *physical >= first_vector && *physical < flags ? 128U : 64U;
            const auto reg = machine::Register::physical_register({*physical}, mode_of_bits(bits));
            if (std::none_of(call.clobbers.begin(), call.clobbers.end(),
                             [&](const Register& present) { return present.id == reg.id; })) {
                call.clobbers.push_back(reg);
            }
        }
    }

    // `from` with the register bits above a carrier filled as the ABI piece
    // requires.
    Register extend_for_piece(Register from, const ValuePiece& piece) {
        if (piece.extension == AbiExtensionKind::None || piece.carrier_bits >= 64) return from;
        return extend(from, piece.carrier_bits, piece.extension == AbiExtensionKind::Sign, 64);
    }

    void lower_call(const mir::ManagedValue& item) {
        const auto signature = hir::call_signature(hir_, item.callee, item.call_signature);
        const hir::Function* callee = item.callee ? &hir_.function(*item.callee) : nullptr;
        const auto* abi = signature ? abi_model(signature->abi) : nullptr;
        if (!signature || !abi) {
            diagnostics_.error(item.location, "the callee's ABI has no AArch64 model entry");
            return;
        }
        if (signature->variadic) {
            unsupported(item.location, "variadic functions");
            return;
        }
        if (hir::manual_interface(*signature) || (callee && callee->naked)) {
            unsupported(item.location, "calls through manual register endpoints");
            return;
        }
        std::vector<hir::TypeId> actual;
        for (const auto& argument : item.call_arguments) actual.push_back(argument.type);
        const auto classified = classify_call(hir_, subtarget_, *signature, *abi, actual);
        if (!classified ||
            classified.layout.call.arguments.size() != item.call_arguments.size()) {
            diagnostics_.error(item.location, "the AArch64 ABI '" + abi->canonical_name +
                                                  "' cannot place this call's arguments");
            return;
        }
        const auto& layout = classified.layout;
        for (const auto& argument : item.call_arguments) {
            if (argument.value && !held(*argument.value)) return;
        }
        if (item.must_tail && has_result(item) && aggregates_.contains(item.id.value) &&
            (layout.results.size() != 1 || layout.results.front().indirect)) {
            unsupported(item.location, "[[musttail]] calls that return records in memory");
            return;
        }
        if (layout.call.outgoing_area_size != 0) {
            (void)outgoing_slot(static_cast<std::uint32_t>(layout.call.outgoing_area_size));
        }
        // Each argument piece's register, then stack stores before any
        // register placement, which no later instruction may overwrite.
        std::vector<Placement> placements;
        for (std::size_t index = 0; index < item.call_arguments.size(); ++index) {
            const auto& argument = item.call_arguments[index];
            const auto& assignment = layout.call.arguments[index];
            if (argument.value && aggregates_.contains(argument.value->value)) {
                if (!aggregate_argument(item, argument, assignment, placements)) return;
                continue;
            }
            if (assignment.indirect) {
                unsupported(item.location, "arguments that the ABI passes by reference");
                return;
            }
            std::vector<Register> parts;
            if (argument.value) {
                parts = chunks(*argument.value);
            } else if (argument.cell) {
                const auto address = new_register(64);
                auto instruction = target(Opcode::StackAddress);
                instruction.operands.push_back(
                    machine::StackSlotOperand{{argument.cell->value}, 0, machine::i64});
                instruction.defs.push_back(address);
                emit(std::move(instruction));
                parts = {address};
            } else {
                diagnostics_.error(item.location, "AArch64 call argument has no value");
                return;
            }
            for (const auto& piece : assignment.pieces) {
                auto source = piece_of(parts, piece);
                if (!source) {
                    unsupported(item.location, "ABI pieces that straddle 64-bit halves");
                    return;
                }
                if (piece.location.kind == LocationKind::Register) {
                    source = extend_for_piece(*source, piece);
                }
                placements.push_back({*source, &piece});
            }
        }
        // A result in memory goes to its slot, whose addresses the call
        // passes.
        if (has_result(item) && aggregates_.contains(item.id.value) && !item.must_tail &&
            layout.results.size() == 1 && layout.results.front().indirect) {
            for (const auto& piece : layout.results.front().pieces) {
                placements.push_back({slot_address(aggregates_.at(item.id.value),
                                                   piece.value_bit_offset / 8U),
                                      &piece});
            }
        }
        Instruction call;
        call.kind = machine::InstructionKind::Call;
        call.must_tail = item.must_tail;
        call.has_side_effects = true;
        for (const auto& placement : placements) {
            if (placement.piece->location.kind != LocationKind::Stack) continue;
            if (item.must_tail) {
                unsupported(item.location, "[[musttail]] calls with stack arguments");
                return;
            }
            const auto bytes = (std::min<unsigned>(placement.source.mode.bits,
                                                   placement.piece->value_bits) + 7U) / 8U;
            const auto address = Address::in_slot(
                *result_.outgoing_arguments,
                static_cast<std::int64_t>(placement.piece->location.stack_offset));
            auto instruction = target(bytes == 1   ? Opcode::Store8
                                      : bytes == 2 ? Opcode::Store16
                                      : bytes == 4 ? Opcode::Store32
                                                   : Opcode::Store64);
            instruction.operands.push_back(register_operand(placement.source));
            instruction.uses.push_back(placement.source);
            append_address(instruction, address);
            instruction.may_store = true;
            instruction.has_side_effects = true;
            emit(std::move(instruction));
        }
        if (callee) {
            if (callee->fixed_address) {
                const auto address = new_register(64);
                constant(address, *callee->fixed_address);
                call.operands.push_back(register_operand(address));
                call.uses.push_back(address);
                call.call_signature = item.call_signature;
            } else {
                call.operands.push_back(machine::SymbolOperand{callee->link_symbol, 0, true,
                                                               std::nullopt, callee->id});
                call.direct_callee = callee->id;
            }
        } else {
            const auto address = reg(item.operands.front());
            call.operands.push_back(register_operand(address));
            call.uses.push_back(address);
            call.call_signature = item.call_signature;
        }
        for (const auto& placement : placements) {
            if (placement.piece->location.kind != LocationKind::Register) continue;
            const auto physical = physical_register(placement.piece->location.reg);
            if (!physical || *physical >= stack_pointer) {
                unsupported(item.location, "arguments in SIMD and floating-point registers");
                return;
            }
            // The epilogue before a tail transfer restores the registers
            // the function's own ABI preserves.
            if (item.must_tail &&
                std::none_of(abi_->call_clobbers.begin(), abi_->call_clobbers.end(),
                             [&](const std::string& name) {
                                 return physical_register(name) == *physical;
                             })) {
                unsupported(item.location,
                            "[[musttail]] calls that pass arguments in registers the caller "
                            "preserves");
                return;
            }
            const auto destination = gpr(*physical, placement.source.mode.bits);
            Instruction place;
            place.kind = machine::InstructionKind::Copy;
            place.defs = {destination};
            place.uses = {placement.source};
            emit(std::move(place));
            call.uses.push_back(destination);
        }
        append_clobbers(call, abi->call_clobbers);
        append_clobbers(call, signature->clobbers);
        // The result pieces' registers, which copies take after the call.
        std::vector<std::pair<Register, const ValuePiece*>> results;
        const bool aggregate_result = has_result(item) && aggregates_.contains(item.id.value);
        if (has_result(item) && held(item.id) && !item.must_tail) {
            if (layout.results.size() != 1) {
                unsupported(item.location, "results that the ABI returns in memory");
                return;
            }
            if (layout.results.front().indirect && !aggregate_result) {
                unsupported(item.location, "results that the ABI returns in memory");
                return;
            }
        }
        if (aggregate_result && !item.must_tail && layout.results.front().indirect) {
            // The callee writes the result to its slot.
        } else if (has_result(item) && held(item.id) && !item.must_tail) {
            const auto bits = aggregate_result ? 64U : chunks(item.id).front().mode.bits;
            for (const auto& piece : layout.results.front().pieces) {
                const auto physical = piece.location.kind == LocationKind::Register
                                          ? physical_register(piece.location.reg)
                                          : std::nullopt;
                if (!physical || *physical >= stack_pointer) {
                    unsupported(item.location,
                                "results in SIMD and floating-point registers or memory");
                    return;
                }
                const auto view = gpr(*physical, std::min<unsigned>(bits, 64));
                call.defs.push_back(view);
                results.emplace_back(view, &piece);
            }
        }
        if (item.must_tail) tail_call_ = true;
        emit(std::move(call));
        if (results.empty()) return;
        if (aggregate_result) {
            const auto slot = aggregates_.at(item.id.value);
            for (const auto& [physical, piece] : results) {
                const auto part = new_register(64);
                copy(part, gpr(physical.id, 64));
                const auto offset = piece->value_bit_offset / 8U;
                store_piece(part, Address::in_slot(slot, offset), piece->value_bits,
                            offset_alignment(offset));
            }
            return;
        }
        const auto targets = chunks(item.id);
        if (targets.size() == 1 && results.size() == 1 &&
            results.front().second->value_bit_offset == 0) {
            copy(targets.front(), results.front().first);
            return;
        }
        std::vector<std::pair<Register, const ValuePiece*>> parts;
        for (const auto& [physical, piece] : results) {
            const auto part = new_register(64);
            copy(part, gpr(physical.id, 64));
            parts.emplace_back(part, piece);
        }
        compose(targets, parts);
    }

    // --- Terminators ---

    // Returns a record or array value: its pieces in the result registers,
    // or a copy at the addresses the caller passed, which an ABI that
    // returns the address takes back.
    bool return_aggregate(const mir::ManagedTerminator& terminator, Instruction& result) {
        if (layout_->results.size() != 1) return false;
        const auto& assignment = layout_->results.front();
        const auto slot = aggregates_.at(terminator.value->value);
        const auto type = value(*terminator.value).type;
        if (assignment.indirect) {
            for (const auto& [address, piece] : result_addresses_) {
                copy_bytes(Address::at(address), Address::in_slot(slot, piece->value_bit_offset / 8U),
                           (piece->indirect_value_bits + 7U) / 8U,
                           std::min(16U, aggregate_alignment(type)));
            }
            if (!assignment.indirect_result_reg.empty() && !result_addresses_.empty()) {
                if (const auto physical = physical_register(assignment.indirect_result_reg)) {
                    const auto destination = gpr(*physical, 64);
                    Instruction place;
                    place.kind = machine::InstructionKind::Copy;
                    place.defs = {destination};
                    place.uses = {result_addresses_.front().first};
                    emit(std::move(place));
                    result.uses.push_back(destination);
                }
            }
            return true;
        }
        std::vector<std::pair<Register, std::uint32_t>> placed;
        for (const auto& piece : assignment.pieces) {
            const auto physical = piece.location.kind == LocationKind::Register
                                      ? physical_register(piece.location.reg)
                                      : std::nullopt;
            if (!physical || *physical >= stack_pointer) {
                unsupported(terminator.location,
                            "results in SIMD and floating-point registers or memory");
                return false;
            }
            const auto offset = piece.value_bit_offset / 8U;
            placed.emplace_back(load_piece(Address::in_slot(slot, offset), piece.value_bits,
                                           offset_alignment(offset)),
                                *physical);
        }
        for (const auto& [source, physical] : placed) {
            const auto destination = gpr(physical, 64);
            Instruction place;
            place.kind = machine::InstructionKind::Copy;
            place.defs = {destination};
            place.uses = {source};
            emit(std::move(place));
            result.uses.push_back(destination);
        }
        return true;
    }

    void lower_terminator(const mir::ManagedTerminator& terminator) {
        Instruction result;
        switch (terminator.kind) {
        case mir::TerminatorKind::Return:
            result.kind = machine::InstructionKind::Return;
            if (terminator.value && !tail_call_ &&
                aggregates_.contains(terminator.value->value)) {
                if (!return_aggregate(terminator, result)) return;
            } else if (terminator.value && !tail_call_) {
                if (!held(*terminator.value)) return;
                if (layout_->results.size() != 1 || layout_->results.front().indirect) {
                    unsupported(terminator.location, "results that the ABI returns in memory");
                    return;
                }
                const auto parts = chunks(*terminator.value);
                std::vector<std::pair<Register, std::uint32_t>> placed;
                for (const auto& piece : layout_->results.front().pieces) {
                    const auto physical = piece.location.kind == LocationKind::Register
                                              ? physical_register(piece.location.reg)
                                              : std::nullopt;
                    if (!physical || *physical >= stack_pointer) {
                        unsupported(terminator.location,
                                    "results in SIMD and floating-point registers or memory");
                        return;
                    }
                    const auto source = piece_of(parts, piece);
                    if (!source) {
                        unsupported(terminator.location, "ABI pieces that straddle 64-bit halves");
                        return;
                    }
                    placed.emplace_back(extend_for_piece(*source, piece), *physical);
                }
                for (const auto& [source, physical] : placed) {
                    const auto destination = gpr(physical, source.mode.bits);
                    Instruction place;
                    place.kind = machine::InstructionKind::Copy;
                    place.defs = {destination};
                    place.uses = {source};
                    emit(std::move(place));
                    result.uses.push_back(destination);
                }
            }
            break;
        case mir::TerminatorKind::Branch:
            result.kind = machine::InstructionKind::Branch;
            result.operands.push_back(block_operand(terminator.successors.front().value));
            break;
        case mir::TerminatorKind::ConditionalBranch: {
            result.kind = machine::InstructionKind::ConditionalBranch;
            Condition condition = Condition::Ne;
            if (fused_.contains(terminator.value->value)) {
                condition = emit_comparison(value(*terminator.value));
            } else {
                compare_zero(reg(*terminator.value), 8);
            }
            result.operands.push_back(immediate_operand(static_cast<std::uint64_t>(condition)));
            for (const auto successor : terminator.successors) {
                result.operands.push_back(block_operand(successor.value));
            }
            result.uses.push_back(flags_register());
            break;
        }
        case mir::TerminatorKind::IndirectBranch: {
            result.kind = machine::InstructionKind::IndirectBranch;
            const auto destination = reg(*terminator.value);
            result.operands.push_back(register_operand(destination));
            result.uses.push_back(destination);
            for (const auto successor : terminator.successors) {
                result.operands.push_back(block_operand(successor.value));
            }
            break;
        }
        case mir::TerminatorKind::Unreachable:
            result.kind = machine::InstructionKind::Unreachable;
            result.has_side_effects = true;
            break;
        case mir::TerminatorKind::Trap:
            result.kind = machine::InstructionKind::Trap;
            result.has_side_effects = true;
            break;
        case mir::TerminatorKind::None:
            diagnostics_.error(terminator.location,
                               "managed MIR block has no terminator during AArch64 selection");
            result.kind = machine::InstructionKind::Unreachable;
            break;
        }
        emit(std::move(result));
    }

    const hir::Module& hir_;
    const Subtarget& subtarget_;
    const TargetInfo& target_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    SelectedFunction result_;
    const mir::ManagedFunction* source_{};
    const AbiEntry* abi_{};
    std::optional<SignatureLayout> layout_;
    std::vector<std::optional<Register>> registers_;
    std::vector<std::uint32_t> uses_;
    std::vector<std::uint32_t> value_blocks_;
    std::unordered_set<std::uint32_t> fused_;
    // The low and high halves of each 128-bit integer.
    std::unordered_map<std::uint32_t, std::pair<Register, Register>> wide_;
    // The frame slot of each record or array value.
    std::unordered_map<std::uint32_t, machine::StackSlotId> aggregates_;
    // The incoming addresses of the function's result in memory.
    std::vector<std::pair<Register, const ValuePiece*>> result_addresses_;
    // Slot addresses that the entry computes, after its captures.
    std::unordered_map<std::uint32_t, Register> entry_addresses_;
    std::vector<Instruction> entry_code_;
    struct PhiCopy {
        Register source;
        machine::StackSlotId temporary;
        machine::StackSlotId destination;
        std::uint32_t size{};
    };
    std::vector<PhiCopy> pending_phi_copies_;
    std::unordered_set<std::string> reported_;
    // Captures of incoming registers, which start the entry block.
    std::vector<Instruction> captures_;
    machine::Block* block_{};
    SourceLocation location_;
    std::optional<mir::InlineSiteId> site_;
    // The block makes a [[musttail]] call: the return after it transfers
    // nothing more.
    bool tail_call_{};
};

} // namespace

bool reaches_through_got(const hir::Module& module, const CompilerOptions& options,
                         const machine::SymbolOperand& symbol) {
    if (!options.position_independent) return false;
    if (symbol.label && !module.labels.at(symbol.label->value).is_global) return false;
    if (symbol.function) return preemptible(module.function(*symbol.function), options);
    if (symbol.object) return preemptible(module.object(*symbol.object), options);
    return false;
}

std::vector<SelectedFunction> select_module(const mir::ManagedModule& managed,
                                            const hir::Module& hir_module,
                                            const Subtarget& subtarget,
                                            const CompilerOptions& options,
                                            Diagnostics& diagnostics) {
    std::vector<SelectedFunction> result;
    Selector selector(hir_module, subtarget, options, diagnostics);
    for (const auto& function : managed.functions) {
        if (auto selected = selector.select(function)) result.push_back(std::move(*selected));
    }
    return result;
}

} // namespace cross::aarch64

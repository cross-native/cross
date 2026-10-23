// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The assignment proof on source MIR, before inlining or slot promotion can
// erase the writes it follows: errors for `out` parameter cells, and the
// uninitialized-read warnings for declared automatic objects.

#include "middle/initialization.hpp"
#include "middle/mir_analysis.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace cross::mir {
namespace {

using BitRange = InitializedBitRange;
using BitRanges = std::vector<BitRange>;

std::uint64_t storage_size(const hir::Module& module, hir::TypeId id,
                           const TargetInfo& target) {
    return hir::layout_size(module, id, target).value_or(0);
}

bool integer_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin >= BuiltinType::Bool && type.builtin <= BuiltinType::Uptr;
}

void add_range(BitRanges& ranges, BitRange added) {
    if (added.begin >= added.end) return;
    ranges.push_back(added);
    std::sort(ranges.begin(), ranges.end(),
              [](const auto& left, const auto& right) {
                  return left.begin < right.begin;
              });
    std::size_t kept = 0;
    for (const auto& range : ranges) {
        if (kept && range.begin <= ranges[kept - 1].end) {
            ranges[kept - 1].end = std::max(ranges[kept - 1].end, range.end);
        } else {
            ranges[kept++] = range;
        }
    }
    ranges.resize(kept);
}

BitRanges intersect_ranges(const BitRanges& left, const BitRanges& right) {
    BitRanges result;
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < left.size() && b < right.size()) {
        const auto begin = std::max(left[a].begin, right[b].begin);
        const auto end = std::min(left[a].end, right[b].end);
        if (begin < end) result.push_back({begin, end});
        if (left[a].end < right[b].end) ++a;
        else ++b;
    }
    return result;
}

BitRanges unite_ranges(BitRanges left, const BitRanges& right) {
    for (const auto& range : right) add_range(left, range);
    return left;
}

bool covers(const BitRanges& assigned, BitRange needed) {
    return std::any_of(assigned.begin(), assigned.end(),
                       [&](const BitRange& available) {
                           return available.begin <= needed.begin &&
                                  available.end >= needed.end;
                       });
}

bool type_complete(const hir::Module& module, const TargetInfo& target,
                   hir::TypeId id, std::uint64_t base_bits,
                   const BitRanges& assigned) {
    return initialized_type(module, id, base_bits, assigned,
        [&](hir::TypeId type) { return storage_size(module, type, target); });
}

// The value whose truth decides `condition` having `truth`, through the
// zero tests, logical negations and widenings that branch lowering adds.
std::pair<ValueId, bool> truth_source(const ManagedFunction& function,
                                      const hir::Module& module,
                                      ValueId condition, bool truth) {
    const auto zero = [&](ValueId id) {
        const auto& value = function.values[id.value];
        return value.kind == ValueKind::ConstantInteger && value.integer == 0 &&
               value.integer_high == 0;
    };
    for (;;) {
        const auto& value = function.values[condition.value];
        if (value.kind == ValueKind::Unary && value.unary == UnaryOperation::IsZero &&
            value.operands.size() == 1) {
            truth = !truth;
        } else if (value.kind == ValueKind::Binary && value.operands.size() == 2 &&
                   (value.binary == BinaryOperation::NotEqual ||
                    value.binary == BinaryOperation::Equal) && zero(value.operands[1])) {
            if (value.binary == BinaryOperation::Equal) truth = !truth;
        } else if (!(value.kind == ValueKind::Cast && value.operands.size() == 1 &&
                     (value.cast == CastOperation::ZeroExtend ||
                      value.cast == CastOperation::SignExtend ||
                      value.cast == CastOperation::Reinterpret) &&
                     integer_type(module, value.type) &&
                     integer_type(module, function.values[value.operands[0].value].type))) {
            return {condition, truth};
        }
        condition = value.operands[0];
    }
}

bool constant_truth(const ManagedValue& value) {
    return value.integer != 0 || value.integer_high != 0;
}

// What the paths to a point assign of one cell: the bits that every path
// assigns, the bits that some path assigns, and the bits that some path may
// assign through an access the proof does not resolve.
struct AssignmentState {
    BitRanges must;
    BitRanges may;
    BitRanges unresolved;
    friend bool operator==(const AssignmentState&, const AssignmentState&) = default;
};

AssignmentState join(const AssignmentState& left, const AssignmentState& right) {
    return {intersect_ranges(left.must, right.must),
            unite_ranges(left.may, right.may),
            unite_ranges(left.unresolved, right.unresolved)};
}

void assign(AssignmentState& state, BitRange range, bool resolved) {
    add_range(state.must, range);
    add_range(state.may, range);
    if (!resolved) add_range(state.unresolved, range);
}

// The same facts for objects that every access covers whole, a bit each.
struct ScalarState {
    std::vector<std::uint64_t> must;
    std::vector<std::uint64_t> may;
    std::vector<std::uint64_t> unresolved;
    friend bool operator==(const ScalarState&, const ScalarState&) = default;
};

ScalarState join(ScalarState left, const ScalarState& right) {
    for (std::size_t word = 0; word < left.must.size(); ++word) {
        left.must[word] &= right.must[word];
        left.may[word] |= right.may[word];
        left.unresolved[word] |= right.unresolved[word];
    }
    return left;
}

bool has(const std::vector<std::uint64_t>& bits, std::size_t index) {
    return (bits[index / 64] >> (index % 64) & 1U) != 0;
}

// The blocks that the entry reaches and the block of each value.
struct FlowShape {
    explicit FlowShape(const ManagedFunction& function)
        : reachable(function.blocks.size()),
          defined_in(function.values.size()) {
        for (const auto& block : function.blocks)
            for (const auto id : block.values) defined_in[id.value] = block.id;
        std::vector<BlockId> pending{function.entry};
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            if (reachable[id.value]) continue;
            reachable[id.value] = true;
            const auto& successors = function.blocks[id.value].terminator.successors;
            pending.insert(pending.end(), successors.begin(), successors.end());
        }
    }

    std::vector<bool> reachable;
    std::vector<std::optional<BlockId>> defined_in;
};

// The assignment proof on the control flow, with the states "when true" and
// "when false" of the conditions that branches test, as in C# and Java.
// `entry` is the state at the function's entry and `none` that of no path.
// Blocks outside `active`, when given, keep the entry state: the caller
// passes the blocks that a transfer can change and that can reach a use.
template <class State>
class AssignmentFlow {
public:
    using Transfer = std::function<State(const ManagedBlock&, State)>;

    AssignmentFlow(const ManagedFunction& function, const hir::Module& module,
                   const FlowShape& shape, const State& entry, const State& none,
                   Transfer transfer, const std::vector<bool>* active = nullptr)
        : function_(function), module_(module), shape_(shape),
          transfer_(std::move(transfer)) {
        in_.assign(function.blocks.size(), none);
        for (const auto& block : function.blocks)
            if (active && !(*active)[block.id.value]) in_[block.id.value] = entry;
        out_ = in_;
        in_[function.entry.value] = entry;
        for (bool changed = true; changed;) {
            changed = false;
            known_.clear();
            for (const auto& block : function.blocks) {
                if (!shape.reachable[block.id.value] ||
                    (active && !(*active)[block.id.value])) continue;
                State incoming = entry;
                if (block.id != function.entry) {
                    std::optional<State> joined;
                    for (const auto predecessor : block.predecessors) {
                        if (!shape.reachable[predecessor.value]) continue;
                        auto state = edge(predecessor, block.id);
                        if (!state) continue;
                        joined = joined ? join(std::move(*joined), *state) : std::move(state);
                    }
                    incoming = joined ? std::move(*joined) : none;
                }
                auto outgoing = transfer_(block, incoming);
                if (in_[block.id.value] != incoming || out_[block.id.value] != outgoing) {
                    in_[block.id.value] = std::move(incoming);
                    out_[block.id.value] = std::move(outgoing);
                    changed = true;
                }
            }
        }
        known_.clear();
    }

    [[nodiscard]] const State& entry(BlockId block) const { return in_[block.value]; }

    // The state that the edge carries, or none when it cannot be taken.
    [[nodiscard]] std::optional<State> edge(BlockId from, BlockId to) {
        const auto& terminator = function_.blocks[from.value].terminator;
        if (terminator.kind == TerminatorKind::ConditionalBranch && terminator.value &&
            terminator.successors.size() == 2 &&
            terminator.successors[0] != terminator.successors[1])
            return given(from, *terminator.value, terminator.successors[0] == to);
        return out_[from.value];
    }

private:
    // A branch on a short-circuit or conditional result tests a phi of its
    // operands' values, so each successor is reached only through the
    // incoming edges whose value can have that truth. A constant condition
    // never has the other truth.
    std::optional<State> given(BlockId block, ValueId condition, bool truth) {
        const auto [root, wanted] = truth_source(function_, module_, condition, truth);
        const auto& value = function_.values[root.value];
        if (value.kind == ValueKind::ConstantInteger)
            return constant_truth(value) == wanted
                ? std::optional<State>(out_[block.value]) : std::nullopt;
        if (value.kind != ValueKind::Phi || shape_.defined_in[root.value] != block)
            return out_[block.value];
        const auto key = std::tuple{block.value, root.value, wanted};
        if (const auto found = known_.find(key); found != known_.end()) return found->second;
        // A cycle through loop phis learns nothing.
        known_.emplace(key, out_[block.value]);
        std::optional<State> joined;
        for (const auto& incoming : value.incoming) {
            const auto from = incoming.predecessor;
            if (!shape_.reachable[from.value]) continue;
            const auto [source_root, source_truth] =
                truth_source(function_, module_, incoming.value, wanted);
            const auto& source = function_.values[source_root.value];
            std::optional<State> state;
            if (source.kind == ValueKind::ConstantInteger) {
                if (constant_truth(source) != source_truth) continue;
                state = edge(from, block);
            } else if (shape_.defined_in[incoming.value.value] == from &&
                       function_.blocks[from.value].terminator.kind == TerminatorKind::Branch) {
                state = given(from, incoming.value, wanted);
            } else {
                state = edge(from, block);
            }
            if (!state) continue;
            joined = joined ? join(std::move(*joined), *state) : std::move(state);
        }
        const auto result = joined
            ? std::optional<State>(transfer_(function_.blocks[block.value], std::move(*joined)))
            : std::nullopt;
        known_[key] = result;
        return result;
    }

    const ManagedFunction& function_;
    const hir::Module& module_;
    const FlowShape& shape_;
    Transfer transfer_;
    std::vector<State> in_;
    std::vector<State> out_;
    std::map<std::tuple<std::uint32_t, std::uint32_t, bool>, std::optional<State>> known_;
};

// The bits of a slot that a pointer access at `address` covers: a bit-field,
// or the stored or loaded value.
std::optional<BitRange> access_bits(const ManagedFunction& function,
                                    const hir::Module& module, const TargetInfo& target,
                                    const ManagedValue& value, const LocalAddress& address,
                                    bool write) {
    const auto slot_size =
        storage_size(module, function.slots[address.slot.value].type, target);
    if (!address.bytes || *address.bytes > slot_size) return std::nullopt;
    std::uint64_t begin = *address.bytes * 8;
    std::uint64_t end{};
    if (value.bit_field_region) {
        begin += value.bit_field_region->offset;
        end = begin + value.bit_field_region->width;
    } else {
        const auto type = write && value.operands.size() >= 2
            ? function.values[value.operands[1].value].type
            : value.type;
        end = begin + storage_size(module, type, target) * 8;
    }
    if (end > slot_size * 8 || begin >= end) return std::nullopt;
    return BitRange{begin, end};
}

// Whether a union that contains `range` of an object of `type` has one
// complete alternative in `assigned`: any member assigns a union.
bool union_assigned(const hir::Module& module, const TargetInfo& target,
                    hir::TypeId type, BitRange range, const BitRanges& assigned) {
    std::uint64_t base = 0;
    for (;;) {
        const auto& entity = module.type(type);
        if (entity.kind == hir::Type::Kind::Record && entity.record) {
            const auto& record = module.record(*entity.record);
            if (record.is_union && type_complete(module, target, type, base, assigned))
                return true;
            const auto inner = std::find_if(record.members.begin(), record.members.end(),
                [&](const hir::RecordMember& member) {
                    const auto begin = base + member.offset * 8;
                    return !member.bit_width && begin <= range.begin &&
                           range.end <= begin + storage_size(module, member.type, target) * 8;
                });
            if (inner == record.members.end()) return false;
            base += inner->offset * 8;
            type = inner->type;
        } else if (entity.kind == hir::Type::Kind::Array && entity.element) {
            const auto element_bits = storage_size(module, *entity.element, target) * 8;
            if (element_bits == 0) return false;
            const auto index = (range.begin - base) / element_bits;
            if (index >= entity.lanes) return false;
            base += index * element_bits;
            type = *entity.element;
        } else {
            return false;
        }
    }
}

// The part of an object of `type` named `name` that `range` selects, by
// member and constant index down to a union or a scalar.
std::string part_name(const hir::Module& module, const TargetInfo& target,
                      hir::TypeId type, std::string name, BitRange range) {
    std::uint64_t base = 0;
    for (;;) {
        const auto& entity = module.type(type);
        if (entity.kind == hir::Type::Kind::Record && entity.record) {
            const auto& record = module.record(*entity.record);
            if (record.is_union) return name;
            const auto inner = std::find_if(record.members.begin(), record.members.end(),
                [&](const hir::RecordMember& member) {
                    const auto begin = base + member.offset * 8 +
                                       (member.bit_width ? member.bit_offset : 0);
                    const auto end = member.bit_width
                        ? begin + *member.bit_width
                        : begin + storage_size(module, member.type, target) * 8;
                    return begin <= range.begin && range.end <= end;
                });
            if (inner == record.members.end()) return name;
            if (!inner->name.empty()) name += "." + inner->name;
            if (inner->bit_width) return name;
            base += inner->offset * 8;
            type = inner->type;
        } else if (entity.kind == hir::Type::Kind::Array && entity.element) {
            const auto element_bits = storage_size(module, *entity.element, target) * 8;
            if (element_bits == 0) return name;
            const auto index = (range.begin - base) / element_bits;
            if (index >= entity.lanes || range.end > base + (index + 1) * element_bits)
                return name;
            name += "[" + std::to_string(index) + "]";
            base += index * element_bits;
            type = *entity.element;
        } else {
            return name;
        }
    }
}

// The source position of a location outside expansions, or of a token that
// an expansion copied from source.
std::optional<SourceLocation> copied_position(SourceLocation location) {
    if (!location.valid() || !location.file->expansion_at(location.offset)) return location;
    const auto* token = location.file->token_origin_at(location.offset);
    if (token && token->span.valid() && token->identity.expansion.value == 0 &&
        !token->span.file->expansion_at(token->span.offset))
        return token->span;
    return std::nullopt;
}

// Where a note points: the source position of a copied token, and for other
// generated code the expansion that produced it.
SourceLocation source_position(SourceLocation location) {
    for (;;) {
        if (const auto copied = copied_position(location)) return *copied;
        location = location.file->expansion_at(location.offset)->invocation;
    }
}

// A generated label can mark a statement that the expansion copied from
// source, such as the `;` of the statement it replaces.
SourceLocation label_position(const hir::Label& label) {
    if (const auto copied = copied_position(label.location)) return *copied;
    if (label.definition && label.definition->first)
        if (const auto copied = copied_position(label.definition->first->location))
            return *copied;
    return source_position(label.location);
}

// The written spelling of the name declared at `location`: a generated
// identifier shows its prefix.
std::string spelling(const std::string& name, SourceLocation location) {
    const auto fresh = token_origin(location).fresh;
    return fresh ? fresh->prefix : name;
}

// A step of the proof for one automatic object, in program order.
struct Access {
    enum class Kind : std::uint8_t {
        // A declaration executes or the object's lifetime ends.
        Unassign,
        Assign,
        // A write the proof does not resolve, or an escaping address.
        AssignUnresolved,
        Read,
        // A read at a run-time index or another unresolved position.
        ReadUnresolved,
    };
    Kind kind{};
    SlotId slot;
    BlockId block;
    ValueId value;
    BitRange bits;
};

} // namespace

void check_out_assignment(const ManagedFunction& function,
                          const hir::Module& module, const TargetInfo& target,
                          Diagnostics& diagnostics) {
    const auto& source = module.function(function.source);
    if (std::none_of(source.parameters.begin(), source.parameters.end(),
                    [](const auto& parameter) { return parameter.mode == ParameterMode::Out; })) return;
    const LocalPointerAnalysis pointers(function, module, target);
    const FlowShape shape(function);
    for (const auto& slot : function.slots) {
        if (!slot.source_parameter ||
            *slot.source_parameter >= source.parameters.size() ||
            source.parameters[*slot.source_parameter].mode != ParameterMode::Out) {
            continue;
        }
        const auto& parameter = source.parameters[*slot.source_parameter];
        const auto slot_size = storage_size(module, slot.type, target);
        const auto pointer_region = [&](const ManagedValue& value,
                                        const LocalAddress& address, bool write) {
            return address.slot == slot.id
                ? access_bits(function, module, target, value, address, write)
                : std::nullopt;
        };
        const auto transfer_value = [&](const ManagedValue& value, BitRanges& assigned) {
            if (value.kind == ValueKind::LifetimeStart && value.slot == slot.id) {
                assigned.clear();
            } else if (value.kind == ValueKind::Store && value.slot == slot.id) {
                add_range(assigned, {0, slot_size * 8});
            } else if ((value.kind == ValueKind::PointerStore ||
                        (value.kind == ValueKind::Atomic && value.atomic == AtomicOperation::Store)) &&
                       !value.operands.empty()) {
                if (const auto address = pointers.targets(value.operands.front()).definite())
                    if (const auto region = pointer_region(value, *address, true))
                        add_range(assigned, *region);
            }
        };
        AssignmentFlow<AssignmentState> flow(function, module, shape, {},
            {{{0, slot_size * 8}}, {}, {}},
            [&](const ManagedBlock& block, AssignmentState state) {
                for (const auto id : block.values)
                    transfer_value(function.values[id.value], state.must);
                return state;
            });
        // A tail call that forwards the parameter's transport pointer leaves
        // the assignment to its callee.
        const auto forwarded = [&](const ManagedBlock& block) {
            const auto pointer = *slot.source_parameter <
                                         function.parameters.size()
                ? std::optional<ValueId>(
                      function.parameters[*slot.source_parameter])
                : std::nullopt;
            return pointer && std::any_of(
                block.values.begin(), block.values.end(), [&](ValueId id) {
                    const auto& value = function.values[id.value];
                    return value.kind == ValueKind::Call && value.must_tail &&
                           std::any_of(
                               value.call_arguments.begin(),
                               value.call_arguments.end(),
                               [&](const CallArgument& argument) {
                                   return argument.value == pointer;
                               });
                });
        };
        bool read_reported = false;
        bool return_reported = false;
        for (const auto& block : function.blocks) {
            if (!shape.reachable[block.id.value]) continue;
            auto assigned = flow.entry(block.id).must;
            for (const auto id : block.values) {
                const auto& value = function.values[id.value];
                const bool direct_read = value.kind == ValueKind::Load &&
                                         value.slot == slot.id &&
                                         !value.copy_out_read;
                const bool pointer_read = !value.partial_update_read &&
                    !value.copy_out_read &&
                    (value.kind == ValueKind::PointerLoad ||
                     (value.kind == ValueKind::Atomic &&
                      value.atomic != AtomicOperation::Store));
                bool pointer_read_ready = true;
                if (pointer_read && !value.operands.empty()) {
                    for (const auto& address : pointers.targets(value.operands.front()).addresses) {
                        if (address.slot != slot.id) continue;
                        const auto region = pointer_region(value, address, false);
                        if (!address.bytes) {
                            pointer_read_ready &= type_complete(module, target, slot.type, 0, assigned);
                        } else if (region) {
                            const auto kind = module.type(value.type).kind;
                            pointer_read_ready &= !value.bit_field_region &&
                                (kind == hir::Type::Kind::Record || kind == hir::Type::Kind::Array)
                                ? type_complete(module, target, value.type, region->begin, assigned)
                                : covers(assigned, *region);
                        }
                    }
                }
                if (!read_reported &&
                    ((direct_read &&
                      !type_complete(module, target, slot.type, 0, assigned)) ||
                     !pointer_read_ready)) {
                    diagnostics.error(value.location,
                        "read of 'out' parameter '" + parameter.name +
                        "' before assignment");
                    read_reported = true;
                }
                transfer_value(value, assigned);
            }
            if (!type_complete(module, target, slot.type, 0, assigned) &&
                !return_reported &&
                block.terminator.kind == TerminatorKind::Return &&
                !forwarded(block)) {
                diagnostics.error(block.terminator.location,
                    "normal return leaves 'out' parameter '" +
                    parameter.name + "' unassigned");
                return_reported = true;
            }
        }
    }
}

void check_automatic_assignment(const ManagedFunction& function,
                                const hir::Module& module, const TargetInfo& target,
                                Diagnostics& diagnostics) {
    const bool unassigned_warning = diagnostics.enabled(Warning::Uninitialized);
    const bool maybe_warning = diagnostics.enabled(Warning::MaybeUninitialized);
    if (!unassigned_warning && !maybe_warning) return;
    // A hard-bound register object is a view of the register, which always
    // holds a value.
    std::vector<bool> checked(function.slots.size());
    for (const auto& slot : function.slots)
        checked[slot.id.value] = slot.declared && !slot.physical_location;
    if (std::none_of(checked.begin(), checked.end(), [](bool value) { return value; }))
        return;
    const LocalPointerAnalysis pointers(function, module, target);
    const FlowShape shape(function);
    const auto whole = [&](SlotId slot) {
        return BitRange{0, storage_size(module, function.slots[slot.value].type, target) * 8};
    };
    const auto pointer_cell = [&](const LocalAddress& address) {
        return address.bytes == 0 &&
               module.type(function.slots[address.slot.value].type).kind ==
                   hir::Type::Kind::Pointer;
    };

    // The objects whose address each object may hold, wherever the function
    // stores one: what an escaping address reaches.
    std::vector<std::vector<std::uint32_t>> holds(function.slots.size());
    const auto hold = [&](SlotId cell, ValueId stored) {
        for (const auto& held : pointers.targets(stored).addresses)
            holds[cell.value].push_back(held.slot.value);
    };
    for (const auto& block : function.blocks) {
        if (!shape.reachable[block.id.value]) continue;
        for (const auto id : block.values) {
            const auto& value = function.values[id.value];
            if (value.kind == ValueKind::Store && value.slot && !value.operands.empty()) {
                hold(*value.slot, value.operands[0]);
            } else if ((value.kind == ValueKind::PointerStore || value.kind == ValueKind::Atomic) &&
                       !value.operands.empty()) {
                for (const auto& cell : pointers.targets(value.operands[0]).addresses)
                    for (std::size_t index = 1; index < value.operands.size(); ++index)
                        hold(cell.slot, value.operands[index]);
            }
        }
    }

    // In program order, so the steps of a block are contiguous.
    std::vector<Access> accesses;
    BlockId current;
    const auto record = [&](SlotId slot, Access::Kind kind, ValueId value, BitRange bits) {
        if (checked[slot.value]) accesses.push_back({kind, slot, current, value, bits});
    };
    // An escaping address may let the program assign anything it reaches,
    // which the proof counts as assigned.
    std::vector<bool> reached(function.slots.size());
    const auto escape_from = [&](std::vector<std::uint32_t> pending, ValueId at) {
        std::vector<std::uint32_t> seen;
        while (!pending.empty()) {
            const auto slot = pending.back();
            pending.pop_back();
            if (reached[slot]) continue;
            reached[slot] = true;
            seen.push_back(slot);
            pending.insert(pending.end(), holds[slot].begin(), holds[slot].end());
        }
        for (const auto slot : seen) {
            reached[slot] = false;
            record(SlotId{slot}, Access::Kind::AssignUnresolved, at, whole(SlotId{slot}));
        }
    };
    const auto escape = [&](ValueId escaping, ValueId at) {
        std::vector<std::uint32_t> roots;
        for (const auto& address : pointers.targets(escaping).addresses)
            roots.push_back(address.slot.value);
        if (!roots.empty()) escape_from(std::move(roots), at);
    };
    const auto read_through = [&](const LocalPointerTargets& targets, const ManagedValue& value) {
        if (targets.unknown || targets.addresses.empty()) return;
        const auto slot = targets.addresses.front().slot;
        if (std::any_of(targets.addresses.begin(), targets.addresses.end(),
                        [&](const LocalAddress& address) { return address.slot != slot; }))
            return;
        if (const auto address = targets.definite()) {
            if (const auto bits = access_bits(function, module, target, value, *address, false))
                record(slot, Access::Kind::Read, value.id, *bits);
        } else {
            record(slot, Access::Kind::ReadUnresolved, value.id, whole(slot));
        }
    };
    const auto write_through = [&](const LocalPointerTargets& targets, const ManagedValue& value) {
        if (const auto address = targets.definite()) {
            if (const auto bits = access_bits(function, module, target, value, *address, true)) {
                record(address->slot, Access::Kind::Assign, value.id, *bits);
                return;
            }
        }
        for (const auto& address : targets.addresses)
            record(address.slot, Access::Kind::AssignUnresolved, value.id, whole(address.slot));
    };
    const auto scalar = [&](hir::TypeId id) {
        const auto& type = module.type(id);
        return type.kind == hir::Type::Kind::Pointer || type.kind == hir::Type::Kind::Vector ||
               (type.kind == hir::Type::Kind::Builtin && type.builtin != BuiltinType::Void);
    };
    for (const auto& block : function.blocks) {
        if (!shape.reachable[block.id.value]) continue;
        current = block.id;
        for (const auto id : block.values) {
            const auto& value = function.values[id.value];
            // A copy of a record or array reads none of its scalars.
            const bool reads = !value.partial_update_read && !value.copy_out_read &&
                               scalar(value.type);
            switch (value.kind) {
            case ValueKind::LifetimeStart:
            case ValueKind::LifetimeEnd:
                if (value.slot) record(*value.slot, Access::Kind::Unassign, id, {});
                break;
            case ValueKind::Load:
                if (value.slot && reads)
                    record(*value.slot, Access::Kind::Read, id, whole(*value.slot));
                break;
            case ValueKind::Store:
                if (value.slot) record(*value.slot, Access::Kind::Assign, id, whole(*value.slot));
                break;
            case ValueKind::PointerLoad:
                if (reads) read_through(pointers.targets(value.operands[0]), value);
                break;
            case ValueKind::IndexedLoad:
                if (reads) read_through(pointers.indexed(value.operands[0], value.operands[1]), value);
                break;
            case ValueKind::PointerStore: {
                const auto& targets = pointers.targets(value.operands[0]);
                // A pointer stored in a pointer object stays visible to the proof.
                if (targets.unknown || targets.addresses.empty() ||
                    !std::all_of(targets.addresses.begin(), targets.addresses.end(), pointer_cell))
                    escape(value.operands[1], id);
                write_through(targets, value);
                break;
            }
            case ValueKind::Atomic: {
                if (value.atomic == AtomicOperation::ThreadFence ||
                    value.atomic == AtomicOperation::SignalFence) break;
                const auto& targets = pointers.targets(value.operands[0]);
                if (value.atomic != AtomicOperation::Store && reads) read_through(targets, value);
                for (std::size_t index = 1; index < value.operands.size(); ++index)
                    escape(value.operands[index], id);
                if (value.atomic != AtomicOperation::Load) write_through(targets, value);
                break;
            }
            case ValueKind::Call:
                for (const auto operand : value.operands) escape(operand, id);
                for (const auto& argument : value.call_arguments) {
                    if (argument.value) escape(*argument.value, id);
                    if (argument.cell) escape_from({argument.cell->value}, id);
                }
                break;
            case ValueKind::Cast: {
                const auto& type = module.type(value.type);
                if (type.kind != hir::Type::Kind::Pointer &&
                    !(type.kind == hir::Type::Kind::Builtin && type.builtin == BuiltinType::Bool))
                    escape(value.operands[0], id);
                break;
            }
            // Comparisons, pointer differences, and the address arithmetic
            // that the pointer analysis follows.
            case ValueKind::Unary:
            case ValueKind::Binary:
            case ValueKind::Select:
            case ValueKind::Phi:
            case ValueKind::IndexedAddress:
                break;
            default:
                for (const auto operand : value.operands) escape(operand, id);
                break;
            }
        }
    }

    struct Report {
        SourceLocation location;
        Warning warning{};
        std::string name;
        SlotId slot;
        // The computed goto and label on a path where the read is unassigned.
        std::optional<std::pair<BlockId, BlockId>> resume;
    };
    std::vector<Report> reports;
    const auto reads = [](const Access& access) {
        return access.kind == Access::Kind::Read || access.kind == Access::Kind::ReadUnresolved;
    };
    std::vector<std::pair<std::size_t, std::size_t>> in_block(function.blocks.size());
    std::vector<bool> read(function.slots.size());
    std::vector<bool> partial(function.slots.size());
    for (std::size_t index = 0; index < accesses.size(); ++index) {
        const auto& access = accesses[index];
        auto& span = in_block[access.block.value];
        if (span.first == span.second) span.first = index;
        span.second = index + 1;
        if (reads(access)) read[access.slot.value] = true;
        if ((access.kind == Access::Kind::Assign || access.kind == Access::Kind::Read) &&
            access.bits != whole(access.slot)) partial[access.slot.value] = true;
    }
    const auto unassigns = [&](std::size_t begin, std::size_t end, SlotId slot) {
        return std::any_of(accesses.begin() + static_cast<std::ptrdiff_t>(begin),
                           accesses.begin() + static_cast<std::ptrdiff_t>(end),
                           [&](const Access& access) {
                               return access.slot == slot && access.kind == Access::Kind::Unassign;
                           });
    };
    // The nearest computed-goto edge on a path to the read on which `open`
    // holds of the state, back to the entry or to an unassigning step.
    const auto computed_goto = [&](auto& flow, std::size_t read_index, const auto& open)
        -> std::optional<std::pair<BlockId, BlockId>> {
        const auto& access = accesses[read_index];
        if (unassigns(in_block[access.block.value].first, read_index, access.slot))
            return std::nullopt;
        std::vector<bool> visited(function.blocks.size());
        visited[access.block.value] = true;
        std::deque<BlockId> pending{access.block};
        while (!pending.empty()) {
            const auto to = pending.front();
            pending.pop_front();
            for (const auto from : function.blocks[to.value].predecessors) {
                if (!shape.reachable[from.value]) continue;
                const auto state = flow.edge(from, to);
                if (!state || !open(*state)) continue;
                if (function.blocks[from.value].terminator.kind == TerminatorKind::IndirectBranch)
                    return std::pair{from, to};
                if (visited[from.value]) continue;
                visited[from.value] = true;
                const auto [begin, end] = in_block[from.value];
                if (from != function.entry && !unassigns(begin, end, access.slot))
                    pending.push_back(from);
            }
        }
        return std::nullopt;
    };
    // A report of the read if its warning is enabled.
    const auto report = [&](std::size_t read_index, bool assigned_somewhere, auto& flow,
                            const auto& open) {
        const auto warning = assigned_somewhere ? Warning::MaybeUninitialized : Warning::Uninitialized;
        if (!(assigned_somewhere ? maybe_warning : unassigned_warning)) return false;
        const auto& access = accesses[read_index];
        const auto& slot = function.slots[access.slot.value];
        const auto name = spelling(slot.name, slot.location);
        reports.push_back({function.values[access.value.value].location, warning,
                           access.kind == Access::Kind::Read
                               ? part_name(module, target, slot.type, name, access.bits)
                               : name,
                           slot.id, computed_goto(flow, read_index, open)});
        return true;
    };

    // The objects that every access covers whole share one flow, a bit each.
    std::vector<std::size_t> bit(function.slots.size());
    std::size_t scalars = 0;
    for (const auto& slot : function.slots)
        if (read[slot.id.value] && !partial[slot.id.value]) bit[slot.id.value] = scalars++;
    if (scalars) {
        const auto words = (scalars + 63) / 64;
        const std::vector<std::uint64_t> zero(words);
        const ScalarState entry{zero, zero, zero};
        const ScalarState none{std::vector<std::uint64_t>(words, ~std::uint64_t{}), zero, zero};
        const auto apply = [&](const Access& access, ScalarState& state) {
            if (!read[access.slot.value] || partial[access.slot.value]) return;
            const auto index = bit[access.slot.value];
            const auto mask = std::uint64_t{1} << (index % 64);
            const auto word = index / 64;
            if (access.kind == Access::Kind::Unassign) {
                state.must[word] &= ~mask;
                state.may[word] &= ~mask;
                state.unresolved[word] &= ~mask;
            } else if (access.kind == Access::Kind::Assign ||
                       access.kind == Access::Kind::AssignUnresolved) {
                state.must[word] |= mask;
                state.may[word] |= mask;
                if (access.kind == Access::Kind::AssignUnresolved) state.unresolved[word] |= mask;
            }
        };
        AssignmentFlow<ScalarState> flow(function, module, shape, entry, none,
            [&](const ManagedBlock& block, ScalarState state) {
                const auto [begin, end] = in_block[block.id.value];
                for (auto index = begin; index < end; ++index) apply(accesses[index], state);
                return state;
            });
        std::vector<bool> done(function.slots.size());
        for (const auto& block : function.blocks) {
            if (!shape.reachable[block.id.value]) continue;
            auto state = flow.entry(block.id);
            const auto [begin, end] = in_block[block.id.value];
            for (auto index = begin; index < end; ++index) {
                const auto& access = accesses[index];
                const auto slot = access.slot.value;
                if (reads(access) && !partial[slot] && !done[slot]) {
                    const auto position = bit[slot];
                    // A run-time index can select any part: reading through
                    // one is diagnosed only when no path assigns any part.
                    const bool assigned = access.kind == Access::Kind::ReadUnresolved
                        ? has(state.may, position)
                        : has(state.must, position) || has(state.unresolved, position);
                    const auto open = [&](const ScalarState& edge) {
                        return !has(edge.must, position) && !has(edge.unresolved, position);
                    };
                    if (!assigned && report(index, access.kind == Access::Kind::Read &&
                                                       has(state.may, position),
                                            flow, open))
                        done[slot] = true;
                }
                apply(access, state);
            }
        }
    }

    // Any other object has a flow of its own over the blocks that its
    // accesses reach and that reach one of its reads.
    for (const auto& slot : function.slots) {
        if (!read[slot.id.value] || !partial[slot.id.value]) continue;
        std::vector<std::size_t> own;
        for (std::size_t index = 0; index < accesses.size(); ++index)
            if (accesses[index].slot == slot.id) own.push_back(index);
        std::vector<bool> after(function.blocks.size());
        std::vector<bool> before(function.blocks.size());
        std::vector<BlockId> forward;
        std::vector<BlockId> backward;
        for (const auto index : own) {
            forward.push_back(accesses[index].block);
            if (reads(accesses[index])) backward.push_back(accesses[index].block);
        }
        while (!forward.empty()) {
            const auto id = forward.back();
            forward.pop_back();
            if (after[id.value]) continue;
            after[id.value] = true;
            const auto& successors = function.blocks[id.value].terminator.successors;
            forward.insert(forward.end(), successors.begin(), successors.end());
        }
        while (!backward.empty()) {
            const auto id = backward.back();
            backward.pop_back();
            if (before[id.value]) continue;
            before[id.value] = true;
            const auto& predecessors = function.blocks[id.value].predecessors;
            backward.insert(backward.end(), predecessors.begin(), predecessors.end());
        }
        std::vector<bool> active(function.blocks.size());
        for (std::size_t block = 0; block < active.size(); ++block)
            active[block] = after[block] && before[block];
        std::vector<std::pair<std::size_t, std::size_t>> own_in_block(function.blocks.size());
        for (std::size_t index = 0; index < own.size(); ++index) {
            auto& span = own_in_block[accesses[own[index]].block.value];
            if (span.first == span.second) span.first = index;
            span.second = index + 1;
        }
        const auto apply = [](const Access& access, AssignmentState& state) {
            if (access.kind == Access::Kind::Unassign) state = {};
            else if (access.kind == Access::Kind::Assign) assign(state, access.bits, true);
            else if (access.kind == Access::Kind::AssignUnresolved) assign(state, access.bits, false);
        };
        AssignmentFlow<AssignmentState> flow(function, module, shape, {},
            {{whole(slot.id)}, {}, {}},
            [&](const ManagedBlock& block, AssignmentState state) {
                const auto [begin, end] = own_in_block[block.id.value];
                for (auto index = begin; index < end; ++index) apply(accesses[own[index]], state);
                return state;
            }, &active);
        const auto satisfied = [&](BitRange range, const BitRanges& assigned) {
            return covers(assigned, range) ||
                   union_assigned(module, target, slot.type, range, assigned);
        };
        // An unresolved write may have assigned what it covers on its paths.
        const auto possibly_assigned = [&](BitRange range, const AssignmentState& state) {
            return satisfied(range, unite_ranges(state.must, state.unresolved));
        };
        const auto find = [&] {
            for (const auto& block : function.blocks) {
                if (!shape.reachable[block.id.value] || !active[block.id.value]) continue;
                auto state = flow.entry(block.id);
                const auto [begin, end] = own_in_block[block.id.value];
                for (auto index = begin; index < end; ++index) {
                    const auto& access = accesses[own[index]];
                    if (reads(access)) {
                        const auto open = [&](const AssignmentState& edge) {
                            return !possibly_assigned(access.bits, edge);
                        };
                        if (access.kind == Access::Kind::ReadUnresolved) {
                            if (state.may.empty() && report(own[index], false, flow, open)) return;
                        } else if (!possibly_assigned(access.bits, state) &&
                                   report(own[index], satisfied(access.bits, state.may), flow, open)) {
                            return;
                        }
                    }
                    apply(access, state);
                }
            }
        };
        find();
    }
    std::stable_sort(reports.begin(), reports.end(), [](const Report& left, const Report& right) {
        const auto key = [](const SourceLocation& location) {
            return std::tuple(location.file ? location.file->path.generic_string() : std::string(),
                              location.line, location.column);
        };
        return key(left.location) < key(right.location);
    });
    for (const auto& entry : reports) {
        const auto& slot = function.slots[entry.slot.value];
        diagnostics.warning(entry.warning, entry.location,
            "'" + entry.name + (entry.warning == Warning::Uninitialized
                                     ? "' is used uninitialized"
                                     : "' may be used uninitialized"));
        diagnostics.note(source_position(slot.location),
                         "'" + spelling(slot.name, slot.location) + "' was declared here");
        if (!entry.resume) continue;
        const auto [from, to] = *entry.resume;
        const auto binding = std::find_if(function.labels.begin(), function.labels.end(),
            [&](const ManagedLabel& label) { return label.block == to; });
        const auto& label = module.labels[binding->label.value];
        const auto label_name = spelling(label.source_name, label.location);
        diagnostics.note(source_position(function.blocks[from.value].terminator.location),
            "this computed goto reaches label '" + label_name + "' with '" +
                entry.name + "' unassigned");
        diagnostics.note(label_position(label), "label '" + label_name + "' is here");
    }
}

} // namespace cross::mir

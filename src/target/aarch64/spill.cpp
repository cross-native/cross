// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/spill.hpp"

#include "target/aarch64/machine.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace cross::aarch64 {
namespace {

using machine::Instruction;
using machine::Register;

bool is_phi(const Instruction& instruction) {
    return instruction.kind == machine::InstructionKind::Target &&
           decode_opcode(instruction.opcode) == Opcode::Phi;
}

bool is_virtual(const Register& value) {
    return value.kind == machine::RegisterKind::Virtual;
}

class Spiller {
public:
    Spiller(machine::Function& function, const SpillPolicy& policy, Diagnostics& diagnostics)
        : function_(function), policy_(policy), diagnostics_(diagnostics) {}

    bool run() {
        homes_.assign(function_.virtual_registers.size(), std::nullopt);
        if (!split_critical_edges()) return false;
        eliminate_phis();
        for (auto& block : function_.blocks) {
            if (!rewrite(block)) return false;
        }
        return true;
    }

private:
    std::size_t index_of(machine::BlockId id) const {
        for (std::size_t index = 0; index < function_.blocks.size(); ++index) {
            if (function_.blocks[index].id == id) return index;
        }
        return function_.blocks.size();
    }

    static bool has_phi(const machine::Block& block) {
        return !block.instructions.empty() && is_phi(block.instructions.front());
    }

    // The position of the `occurrence`-th entry equal to `id`.
    template <typename Items, typename Match>
    static std::optional<std::size_t> nth(const Items& items, std::size_t occurrence, Match match) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (match(items[index]) && occurrence-- == 0) return index;
        }
        return std::nullopt;
    }

    // How many entries before `position` of a block's predecessors equal
    // the one at `position`.
    static std::size_t occurrence(const machine::Block& block, std::size_t position) {
        return static_cast<std::size_t>(std::count(block.predecessors.begin(),
                                                   block.predecessors.begin() +
                                                       static_cast<std::ptrdiff_t>(position),
                                                   block.predecessors[position]));
    }

    // A conditional edge into a block with PHIs gets a block of its own,
    // where the edge's copies go.
    bool split_critical_edges() {
        std::uint32_t next_id = 0;
        for (const auto& block : function_.blocks) next_id = std::max(next_id, block.id.value + 1U);
        for (std::size_t target = 0; target < function_.blocks.size(); ++target) {
            if (!has_phi(function_.blocks[target])) continue;
            for (std::size_t slot = 0; slot < function_.blocks[target].predecessors.size(); ++slot) {
                const auto predecessor_id = function_.blocks[target].predecessors[slot];
                const auto predecessor = index_of(predecessor_id);
                const auto& terminator = function_.blocks[predecessor].instructions.back();
                if (terminator.kind == machine::InstructionKind::IndirectBranch) {
                    diagnostics_.error(terminator.location,
                                       "AArch64 code generation does not yet support a "
                                       "computed goto to a block that merges values");
                    return false;
                }
                if (terminator.kind != machine::InstructionKind::ConditionalBranch) continue;
                const auto target_id = function_.blocks[target].id;
                const auto nth_edge = occurrence(function_.blocks[target], slot);
                machine::Block edge;
                edge.id = {next_id++};
                edge.location = terminator.location;
                edge.label = "edge." + std::to_string(edge.id.value);
                edge.reachable = true;
                edge.predecessors = {predecessor_id};
                edge.successors = {target_id};
                Instruction branch;
                branch.kind = machine::InstructionKind::Branch;
                branch.location = terminator.location;
                branch.inline_site = terminator.inline_site;
                branch.operands.push_back(machine::BlockOperand{target_id});
                edge.instructions.push_back(std::move(branch));
                // Retarget the edge in the predecessor, the target's
                // predecessors, and its PHIs.
                auto& from = function_.blocks[predecessor];
                auto& last = from.instructions.back();
                if (const auto position = nth(last.operands, nth_edge, [&](const machine::Operand& operand) {
                        const auto* block = std::get_if<machine::BlockOperand>(&operand);
                        return block && block->target == target_id;
                    })) {
                    std::get<machine::BlockOperand>(last.operands[*position]).target = edge.id;
                }
                if (const auto position = nth(from.successors, nth_edge,
                                              [&](machine::BlockId id) { return id == target_id; })) {
                    from.successors[*position] = edge.id;
                }
                auto& into = function_.blocks[target];
                into.predecessors[slot] = edge.id;
                for (auto& instruction : into.instructions) {
                    if (!is_phi(instruction)) break;
                    if (const auto position = nth(instruction.operands, nth_edge,
                                                  [&](const machine::Operand& operand) {
                                                      const auto* block = std::get_if<machine::BlockOperand>(&operand);
                                                      return block && block->target == predecessor_id;
                                                  })) {
                        std::get<machine::BlockOperand>(instruction.operands[*position]).target = edge.id;
                    }
                }
                const auto after = std::find(function_.layout.begin(), function_.layout.end(),
                                             predecessor_id);
                function_.layout.insert(after == function_.layout.end() ? after : after + 1,
                                        edge.id);
                function_.blocks.push_back(std::move(edge));
            }
        }
        return true;
    }

    machine::StackSlotId home(const Register& value) {
        if (value.id >= homes_.size()) homes_.resize(value.id + 1U);
        if (!homes_[value.id]) {
            machine::StackSlot slot;
            slot.id = {static_cast<std::uint32_t>(function_.stack_slots.size())};
            slot.kind = machine::StackSlotKind::Spill;
            slot.size = value.mode.bits > 32 ? 8U : 4U;
            slot.alignment = slot.size;
            slot.location = function_.location;
            slot.name = "$v" + std::to_string(value.id);
            slot.spill_for = machine::VirtualRegisterId{value.id};
            homes_[value.id] = slot.id;
            function_.stack_slots.push_back(std::move(slot));
        }
        return *homes_[value.id];
    }

    // A virtual register of `mode` that the physical register holds.
    Register scratch(std::uint32_t physical, machine::IntegerMode mode) {
        const machine::VirtualRegisterId id{
            static_cast<std::uint32_t>(function_.virtual_registers.size())};
        function_.virtual_registers.push_back(mode);
        function_.virtual_register_classes.push_back(machine::VirtualRegisterClass::Integer);
        function_.virtual_register_assignments.push_back(machine::PhysicalRegisterId{physical});
        function_.rematerialized_immediates.push_back(std::nullopt);
        return Register::virtual_register(id, mode);
    }

    bool assigned(const Register& value) const {
        return value.id < function_.virtual_register_assignments.size() &&
               function_.virtual_register_assignments[value.id].has_value();
    }

    // A load of the home of `value` into `to`, or a store of `from` to it.
    Instruction home_access(bool load, const Register& data, const Register& value,
                            const Instruction& near) {
        const bool wide = value.mode.bits > 32;
        Instruction result;
        result.kind = machine::InstructionKind::Target;
        result.opcode = machine_opcode(load ? (wide ? Opcode::Load64 : Opcode::Load32)
                                            : (wide ? Opcode::Store64 : Opcode::Store32));
        result.location = near.location;
        result.inline_site = near.inline_site;
        if (!load) {
            result.operands.push_back(machine::RegisterOperand{data});
            result.uses.push_back(data);
        }
        result.operands.push_back(machine::StackSlotOperand{home(value), 0, machine::i64});
        if (load) result.defs.push_back(data);
        result.may_load = load;
        result.may_store = !load;
        result.has_side_effects = !load;
        return result;
    }

    // Copies between homes in the order a parallel copy needs, with a new
    // home to break each cycle.
    void parallel_copy(std::vector<std::pair<Register, Register>> moves,
                       std::vector<Instruction>& code, const Instruction& near) {
        std::erase_if(moves, [](const auto& move) { return move.first == move.second; });
        const auto copy = [&](const Register& to, const Register& from) {
            const auto staged = scratch(policy_.scratches.front(), from.mode);
            code.push_back(home_access(true, staged, from, near));
            const auto stored = scratch(policy_.scratches.front(), to.mode);
            code.push_back(home_access(false, stored, to, near));
        };
        while (!moves.empty()) {
            const auto ready = std::find_if(moves.begin(), moves.end(), [&](const auto& move) {
                return std::none_of(moves.begin(), moves.end(), [&](const auto& other) {
                    return &other != &move && other.second == move.first;
                });
            });
            if (ready != moves.end()) {
                copy(ready->first, ready->second);
                moves.erase(ready);
                continue;
            }
            // Every destination is still read: keep one in a new home.
            const auto saved = moves.front().first;
            const auto temporary = new_value(saved.mode);
            copy(temporary, saved);
            for (auto& move : moves) {
                if (move.second == saved) move.second = temporary;
            }
        }
    }

    Register new_value(machine::IntegerMode mode) {
        const machine::VirtualRegisterId id{
            static_cast<std::uint32_t>(function_.virtual_registers.size())};
        function_.virtual_registers.push_back(mode);
        function_.virtual_register_classes.push_back(machine::VirtualRegisterClass::Integer);
        function_.virtual_register_assignments.push_back(std::nullopt);
        function_.rematerialized_immediates.push_back(std::nullopt);
        return Register::virtual_register(id, mode);
    }

    void eliminate_phis() {
        for (auto& block : function_.blocks) {
            if (!has_phi(block)) continue;
            for (std::size_t slot = 0; slot < block.predecessors.size(); ++slot) {
                const auto predecessor_id = block.predecessors[slot];
                const auto nth_edge = occurrence(block, slot);
                std::vector<std::pair<Register, Register>> moves;
                for (const auto& instruction : block.instructions) {
                    if (!is_phi(instruction)) break;
                    for (std::size_t operand = 0, seen = 0; operand + 1 < instruction.operands.size();
                         operand += 2) {
                        const auto* from = std::get_if<machine::BlockOperand>(&instruction.operands[operand]);
                        if (!from || from->target != predecessor_id || seen++ != nth_edge) continue;
                        moves.emplace_back(
                            instruction.defs.front(),
                            std::get<machine::RegisterOperand>(instruction.operands[operand + 1]).value);
                        break;
                    }
                }
                auto& predecessor = function_.blocks[index_of(predecessor_id)];
                std::vector<Instruction> code;
                parallel_copy(std::move(moves), code, predecessor.instructions.back());
                predecessor.instructions.insert(
                    predecessor.instructions.end() - 1, std::make_move_iterator(code.begin()),
                    std::make_move_iterator(code.end()));
            }
            std::erase_if(block.instructions, is_phi);
        }
    }

    static void replace(Instruction& instruction, const Register& from, const Register& to,
                        bool definition) {
        auto& registers = definition ? instruction.defs : instruction.uses;
        std::replace(registers.begin(), registers.end(), from, to);
        for (auto& operand : instruction.operands) {
            if (auto* reg = std::get_if<machine::RegisterOperand>(&operand);
                reg && reg->value == from) {
                reg->value = to;
            }
        }
    }

    bool rewrite(machine::Block& block) {
        std::vector<Instruction> result;
        result.reserve(block.instructions.size() * 3);
        for (auto& instruction : block.instructions) {
            if (instruction.kind == machine::InstructionKind::Copy) {
                rewrite_copy(std::move(instruction), result);
                continue;
            }
            std::vector<Register> reads;
            for (const auto& use : instruction.uses) {
                if (is_virtual(use) && !assigned(use) &&
                    std::find(reads.begin(), reads.end(), use) == reads.end()) {
                    reads.push_back(use);
                }
            }
            const bool call = instruction.kind == machine::InstructionKind::Call;
            if ((call && reads.size() > 1) || reads.size() > policy_.scratches.size()) {
                diagnostics_.error(instruction.location,
                                   "AArch64 instruction reads more values than the spill "
                                   "mode has scratch registers");
                return false;
            }
            for (std::size_t index = 0; index < reads.size(); ++index) {
                const auto staged = scratch(call ? policy_.call_target : policy_.scratches[index],
                                            reads[index].mode);
                result.push_back(home_access(true, staged, reads[index], instruction));
                replace(instruction, reads[index], staged, false);
            }
            std::vector<std::pair<Register, Register>> writes;
            for (std::size_t index = 0; index < instruction.defs.size(); ++index) {
                const auto def = instruction.defs[index];
                if (!is_virtual(def) || assigned(def)) continue;
                if (writes.size() >= policy_.scratches.size()) {
                    diagnostics_.error(instruction.location,
                                       "AArch64 instruction writes more values than the spill "
                                       "mode has scratch registers");
                    return false;
                }
                const auto staged = scratch(policy_.scratches[writes.size()], def.mode);
                writes.emplace_back(staged, def);
                replace(instruction, def, staged, true);
            }
            const auto near = instruction;
            result.push_back(std::move(instruction));
            for (const auto& [staged, def] : writes) {
                result.push_back(home_access(false, staged, def, near));
            }
        }
        block.instructions = std::move(result);
        return true;
    }

    void rewrite_copy(Instruction copy, std::vector<Instruction>& result) {
        const auto to = copy.defs.front();
        const auto from = copy.uses.front();
        const bool to_home = is_virtual(to) && !assigned(to);
        const bool from_home = is_virtual(from) && !assigned(from);
        if (!to_home && !from_home) {
            result.push_back(std::move(copy));
        } else if (to_home && from_home) {
            const auto staged = scratch(policy_.scratches.front(), from.mode);
            result.push_back(home_access(true, staged, from, copy));
            const auto stored = scratch(policy_.scratches.front(), to.mode);
            result.push_back(home_access(false, stored, to, copy));
        } else if (from_home) {
            // Placing an interface value: reload it into its register.
            result.push_back(home_access(true, to, from, copy));
        } else {
            // Capturing one: store its register.
            result.push_back(home_access(false, from, to, copy));
        }
    }

    machine::Function& function_;
    const SpillPolicy& policy_;
    Diagnostics& diagnostics_;
    std::vector<std::optional<machine::StackSlotId>> homes_;
};

} // namespace

bool assign_frame_homes(machine::Function& function, const SpillPolicy& policy,
                        Diagnostics& diagnostics) {
    return Spiller(function, policy, diagnostics).run();
}

} // namespace cross::aarch64

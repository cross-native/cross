// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/emission.hpp"

#include "backend/native/debug_info.hpp"
#include "backend/native/frame_cfi.hpp"
#include "target/aarch64/frame.hpp"
#include "target/aarch64/machine.hpp"
#include "target/assembly_format.hpp"

#include <algorithm>
#include <bit>
#include <sstream>
#include <string>

namespace cross::aarch64 {
namespace {

using machine::Instruction;
using machine::Register;

std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

constexpr std::uint64_t low_mask(unsigned bits) {
    return bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1U;
}

AssemblySymbolVisibility assembly_visibility(hir::SymbolVisibility visibility) {
    switch (visibility) {
    case hir::SymbolVisibility::Default: return AssemblySymbolVisibility::Default;
    case hir::SymbolVisibility::Hidden: return AssemblySymbolVisibility::Hidden;
    case hir::SymbolVisibility::Protected: return AssemblySymbolVisibility::Protected;
    case hir::SymbolVisibility::Internal: return AssemblySymbolVisibility::Internal;
    }
    return AssemblySymbolVisibility::Default;
}

// The instructions that put `value` of `bits` (32 or 64) in `rd`: one
// movz, movn, or orr where one does, otherwise movz or movn and movk.
std::vector<std::string> materialize(const std::string& rd, std::uint64_t value, unsigned bits) {
    value &= low_mask(bits);
    const unsigned pieces = bits / 16U;
    unsigned zeros = 0;
    unsigned ones = 0;
    for (unsigned piece = 0; piece < pieces; ++piece) {
        const auto chunk = (value >> (16U * piece)) & 0xffffU;
        zeros += chunk == 0;
        ones += chunk == 0xffffU;
    }
    const auto zero = bits > 32 ? std::string("xzr") : std::string("wzr");
    const auto piece_text = [](std::uint64_t chunk, unsigned piece) {
        return "#" + hex(chunk) + (piece != 0 ? ", lsl #" + std::to_string(16U * piece) : "");
    };
    std::vector<std::string> result;
    if (zeros < pieces - 1U && ones < pieces - 1U && logical_immediate(value, bits)) {
        result.push_back("orr " + rd + ", " + zero + ", #" + hex(value));
        return result;
    }
    const bool inverted = ones > zeros;
    const auto skip = inverted ? 0xffffU : 0U;
    bool first = true;
    for (unsigned piece = 0; piece < pieces; ++piece) {
        const auto chunk = (value >> (16U * piece)) & 0xffffU;
        if (chunk == skip) continue;
        if (first) {
            result.push_back((inverted ? "movn " : "movz ") + rd + ", " +
                             piece_text(inverted ? (~chunk & 0xffffU) : chunk, piece));
            first = false;
        } else {
            result.push_back("movk " + rd + ", " + piece_text(chunk, piece));
        }
    }
    if (first) {
        result.push_back((inverted ? "movn " : "movz ") + rd + ", #0");
    }
    return result;
}

class Emitter {
public:
    Emitter(const hir::Module& hir_module, const Subtarget& subtarget,
            const CompilerOptions& options, Diagnostics& diagnostics, native::DebugInfo* debug)
        : hir_(hir_module), subtarget_(subtarget), options_(options), diagnostics_(diagnostics),
          debug_(debug && debug->active() ? debug : nullptr), format_(subtarget.object_format()),
          cfi_frame_(dwarf_register(stack_pointer), 0,
                     [](Register reg) { return dwarf_register(reg.id); },
                     [](unsigned reg) { return std::to_string(reg); }) {}

    mir::ManagedAssembly run(const machine::Module& module) {
        mir::ManagedAssembly result;
        for (const auto& function : module.functions) {
            out_.str({});
            out_.clear();
            if (!emit_function(function)) continue;
            result.functions.push_back(
                {function.source, out_.str(),
                 {function.frame.program ? function.frame.program->stack_size : 0U, false}});
        }
        return result;
    }

private:
    // --- Operands ---

    std::uint32_t physical(const Register& value) {
        if (value.kind == machine::RegisterKind::Physical) return value.id;
        if (value.id < function_->virtual_register_assignments.size()) {
            if (const auto& assigned = function_->virtual_register_assignments[value.id]) {
                return assigned->value;
            }
        }
        diagnostics_.error(function_->location,
                           "internal error: AArch64 emission met an unallocated virtual register");
        return 0;
    }

    // The register as a value of `bits`: a w view for at most 32.
    std::string name(const Register& value, unsigned bits) {
        return register_name(physical(value), bits);
    }
    std::string name(const Register& value) { return name(value, value.mode.bits); }
    std::string wide(const Register& value) { return name(value, 64); }
    std::string narrow(const Register& value) { return name(value, 32); }

    static const Register& register_at(const Instruction& instruction, std::size_t index) {
        return std::get<machine::RegisterOperand>(instruction.operands[index]).value;
    }
    static std::uint64_t immediate_at(const Instruction& instruction, std::size_t index) {
        return std::get<machine::ImmediateOperand>(instruction.operands[index]).value;
    }

    std::string symbol(const machine::SymbolOperand& operand) const {
        auto text = assembly_symbol(format_, operand.name);
        if (operand.addend > 0) text += '+' + std::to_string(operand.addend);
        if (operand.addend < 0) text += std::to_string(operand.addend);
        return text;
    }

    std::string block_label(machine::BlockId block) const {
        return ".Lcross.a64." + stem_ + ".bb." + std::to_string(block.value);
    }

    std::int64_t frame_offset(const machine::StackSlotOperand& operand) const {
        const auto& slot = function_->stack_slots[operand.slot.value];
        return static_cast<std::int64_t>(slot.frame_offset.value_or(0)) + operand.offset;
    }

    // --- Output ---

    void row(const Instruction& instruction) {
        if (debug_) debug_->row(out_, instruction.location, instruction.inline_site);
    }

    void line(const std::string& text) { out_ << '\t' << text << '\n'; }

    // `add rd, base, #offset` for offsets up to 24 bits.
    void add_offset(const std::string& rd, const std::string& base, std::int64_t offset) {
        const auto magnitude = static_cast<std::uint64_t>(offset < 0 ? -offset : offset);
        const auto* mnemonic = offset < 0 ? "sub " : "add ";
        if (magnitude > 0xffffffU) {
            diagnostics_.error(function_->location,
                               "internal error: AArch64 frame offset exceeds 24 bits");
            return;
        }
        if (magnitude == 0) {
            if (rd != base) line("mov " + rd + ", " + base);
            return;
        }
        auto from = base;
        if (const auto high = magnitude & 0xfff000U; high != 0) {
            line(mnemonic + rd + ", " + from + ", #" + std::to_string(high));
            from = rd;
        }
        if (const auto low = magnitude & 0xfffU; low != 0) {
            line(mnemonic + rd + ", " + from + ", #" + std::to_string(low));
        }
    }

    // Prints a load or store whose address starts at operand `first`.
    void memory(const Instruction& instruction, std::string_view scaled,
                std::string_view unscaled, const std::string& data, std::size_t first,
                unsigned bytes) {
        const auto& address = instruction.operands[first];
        const auto print = [&](std::string_view mnemonic, const std::string& operand) {
            line(std::string(mnemonic) + ' ' + data + ", " + operand);
        };
        const auto immediate = [&](const std::string& base, std::int64_t offset) {
            if (offset == 0) {
                print(scaled, "[" + base + "]");
            } else if (offset > 0 && offset % bytes == 0 && offset / bytes <= 4095) {
                print(scaled, "[" + base + ", #" + std::to_string(offset) + "]");
            } else if (offset >= -256 && offset <= 255) {
                print(unscaled, "[" + base + ", #" + std::to_string(offset) + "]");
            } else {
                return false;
            }
            return true;
        };
        if (const auto* stack = std::get_if<machine::StackSlotOperand>(&address)) {
            const auto offset = frame_offset(*stack);
            if (immediate("sp", offset)) return;
            // A far slot: its address in the frame scratch register, which
            // the instruction clobbers.
            const auto scratch = gpr(frame_scratch, 64);
            if (std::find(instruction.clobbers.begin(), instruction.clobbers.end(), scratch) ==
                instruction.clobbers.end()) {
                diagnostics_.error(instruction.location,
                                   "internal error: AArch64 far frame access has no scratch");
                return;
            }
            const auto base = register_name(frame_scratch, 64);
            const auto low = offset & 0xfff;
            add_offset(base, "sp", offset - low);
            if (!immediate(base, low)) {
                add_offset(base, base, low);
                (void)immediate(base, 0);
            }
            return;
        }
        const auto base = wide(register_at(instruction, first));
        const auto& second = instruction.operands[first + 1];
        if (const auto* offset = std::get_if<machine::ImmediateOperand>(&second)) {
            if (!immediate(base, static_cast<std::int64_t>(offset->value))) {
                diagnostics_.error(instruction.location,
                                   "internal error: AArch64 memory offset out of range");
            }
            return;
        }
        if (const auto* index = std::get_if<machine::RegisterOperand>(&second)) {
            const auto shift = immediate_at(instruction, first + 2);
            print(scaled, "[" + base + ", " + wide(index->value) +
                              (shift != 0 ? ", lsl #" + std::to_string(shift) : "") + "]");
            return;
        }
        const auto& target = std::get<machine::SymbolOperand>(second);
        print(scaled, "[" + base + ", :lo12:" + symbol(target) + "]");
    }

    // --- Instructions ---

    void emit_target(const Instruction& instruction) {
        const auto opcode = decode_opcode(instruction.opcode);
        const auto def = [&](unsigned bits) { return name(instruction.defs.front(), bits); };
        const auto width = instruction.defs.empty()
                               ? 32U
                               : (instruction.defs.front().mode.bits > 32 ? 64U : 32U);
        const auto operand = [&](std::size_t index, unsigned bits) {
            return name(register_at(instruction, index), bits);
        };
        const auto three = [&](std::string_view mnemonic) {
            line(std::string(mnemonic) + ' ' + def(width) + ", " + operand(0, width) + ", " +
                 operand(1, width));
        };
        const auto with_immediate = [&](std::string_view mnemonic, bool logical) {
            const auto value = immediate_at(instruction, 1) & low_mask(width);
            line(std::string(mnemonic) + ' ' + def(width) + ", " + operand(0, width) + ", #" +
                 (logical ? hex(value) : std::to_string(value)));
        };
        const auto two = [&](std::string_view mnemonic, unsigned to, unsigned from) {
            line(std::string(mnemonic) + ' ' + def(to) + ", " + operand(0, from));
        };
        const auto condition = [&](std::size_t index) {
            return std::string(
                condition_name(static_cast<Condition>(immediate_at(instruction, index))));
        };
        switch (opcode) {
        case Opcode::MovImmediate:
            for (const auto& text :
                 materialize(def(width), immediate_at(instruction, 0), width)) {
                line(text);
            }
            return;
        case Opcode::StackAddress:
            add_offset(def(64), "sp",
                       frame_offset(std::get<machine::StackSlotOperand>(instruction.operands[0])));
            return;
        case Opcode::Adrp:
            line("adrp " + def(64) + ", " +
                 symbol(std::get<machine::SymbolOperand>(instruction.operands[0])));
            return;
        case Opcode::AdrpGot:
            line("adrp " + def(64) + ", :got:" +
                 symbol(std::get<machine::SymbolOperand>(instruction.operands[0])));
            return;
        case Opcode::AddLow:
            line("add " + def(64) + ", " + operand(0, 64) + ", :lo12:" +
                 symbol(std::get<machine::SymbolOperand>(instruction.operands[1])));
            return;
        case Opcode::LoadGot:
            line("ldr " + def(64) + ", [" + operand(0, 64) + ", :got_lo12:" +
                 symbol(std::get<machine::SymbolOperand>(instruction.operands[1])) + "]");
            return;
        case Opcode::Adr:
            line("adr " + def(64) + ", " +
                 block_label(std::get<machine::BlockOperand>(instruction.operands[0]).target));
            return;
        case Opcode::Add: three("add"); return;
        case Opcode::Sub: three("sub"); return;
        case Opcode::Mul: three("mul"); return;
        case Opcode::Sdiv: three("sdiv"); return;
        case Opcode::Udiv: three("udiv"); return;
        case Opcode::And: three("and"); return;
        case Opcode::Orr: three("orr"); return;
        case Opcode::Eor: three("eor"); return;
        case Opcode::Lslv: three("lslv"); return;
        case Opcode::Lsrv: three("lsrv"); return;
        case Opcode::Asrv: three("asrv"); return;
        case Opcode::AddImmediate: with_immediate("add", false); return;
        case Opcode::SubImmediate: with_immediate("sub", false); return;
        case Opcode::AndImmediate: with_immediate("and", true); return;
        case Opcode::OrrImmediate: with_immediate("orr", true); return;
        case Opcode::EorImmediate: with_immediate("eor", true); return;
        case Opcode::LslImmediate: with_immediate("lsl", false); return;
        case Opcode::RorImmediate: with_immediate("ror", false); return;
        case Opcode::Rorv: three("rorv"); return;
        case Opcode::Clz:
        case Opcode::Rbit:
        case Opcode::Rev: {
            // At the operand's width; a count fits the result's.
            const auto bits = register_at(instruction, 0).mode.bits > 32 ? 64U : 32U;
            line(std::string(opcode == Opcode::Clz ? "clz " : opcode == Opcode::Rbit ? "rbit "
                                                                                      : "rev ") +
                 def(bits) + ", " + operand(0, bits));
            return;
        }
        case Opcode::Rev16: two("rev16", 32, 32); return;
        case Opcode::LsrImmediate: with_immediate("lsr", false); return;
        case Opcode::AsrImmediate: with_immediate("asr", false); return;
        case Opcode::AddShifted: {
            const auto shift = immediate_at(instruction, 2);
            line("add " + def(width) + ", " + operand(0, width) + ", " + operand(1, width) +
                 (shift != 0 ? ", lsl #" + std::to_string(shift) : ""));
            return;
        }
        case Opcode::Neg: two("neg", width, width); return;
        case Opcode::Mvn: two("mvn", width, width); return;
        case Opcode::Madd:
        case Opcode::Msub:
            line(std::string(opcode == Opcode::Madd ? "madd " : "msub ") + def(width) + ", " +
                 operand(0, width) + ", " + operand(1, width) + ", " + operand(2, width));
            return;
        case Opcode::Sxtb: two("sxtb", width, 32); return;
        case Opcode::Sxth: two("sxth", width, 32); return;
        case Opcode::Sxtw: two("sxtw", 64, 32); return;
        case Opcode::Uxtb: two("uxtb", 32, 32); return;
        case Opcode::Uxth: two("uxth", 32, 32); return;
        case Opcode::Uxtw: two("mov", 32, 32); return;
        case Opcode::Cmp: {
            const auto bits = register_at(instruction, 0).mode.bits > 32 ? 64U : 32U;
            line("cmp " + operand(0, bits) + ", " + operand(1, bits));
            return;
        }
        case Opcode::CmpImmediate:
        case Opcode::CmnImmediate: {
            const auto bits = register_at(instruction, 0).mode.bits > 32 ? 64U : 32U;
            line(std::string(opcode == Opcode::CmpImmediate ? "cmp " : "cmn ") +
                 operand(0, bits) + ", #" + std::to_string(immediate_at(instruction, 1)));
            return;
        }
        case Opcode::TstImmediate:
            line("tst " + operand(0, 32) + ", #" + hex(immediate_at(instruction, 1)));
            return;
        case Opcode::Cset: line("cset " + def(width) + ", " + condition(0)); return;
        case Opcode::Csel:
            line("csel " + def(width) + ", " + operand(0, width) + ", " + operand(1, width) +
                 ", " + condition(2));
            return;
        case Opcode::LoadU8: memory(instruction, "ldrb", "ldurb", def(32), 0, 1); return;
        case Opcode::LoadS8: memory(instruction, "ldrsb", "ldursb", def(width), 0, 1); return;
        case Opcode::LoadU16: memory(instruction, "ldrh", "ldurh", def(32), 0, 2); return;
        case Opcode::LoadS16: memory(instruction, "ldrsh", "ldursh", def(width), 0, 2); return;
        case Opcode::Load32: memory(instruction, "ldr", "ldur", def(32), 0, 4); return;
        case Opcode::LoadS32: memory(instruction, "ldrsw", "ldursw", def(64), 0, 4); return;
        case Opcode::Load64: memory(instruction, "ldr", "ldur", def(64), 0, 8); return;
        case Opcode::Store8: memory(instruction, "strb", "sturb", operand(0, 32), 1, 1); return;
        case Opcode::Store16: memory(instruction, "strh", "sturh", operand(0, 32), 1, 2); return;
        case Opcode::Store32: memory(instruction, "str", "stur", operand(0, 32), 1, 4); return;
        case Opcode::Store64: memory(instruction, "str", "stur", operand(0, 64), 1, 8); return;
        case Opcode::Brk: line("brk #" + std::to_string(immediate_at(instruction, 0))); return;
        case Opcode::Nop: line("nop"); return;
        default:
            diagnostics_.error(instruction.location,
                               "internal error: AArch64 emission met opcode '" +
                                   std::string(opcode_name(instruction.opcode)) + "'");
            return;
        }
    }

    // --- Frames ---

    void emit_frame(const Instruction& instruction) {
        const auto& effect = *instruction.frame_effect;
        const auto reg = [](const Register& value) { return register_name(value.id, 64); };
        switch (effect.operation) {
        case machine::FrameOperation::AdjustStack:
            add_offset("sp", "sp", effect.stack_delta);
            break;
        case machine::FrameOperation::CopyBase:
            line("mov " + reg(*effect.destination) + ", " + reg(effect.base));
            break;
        case machine::FrameOperation::Save:
        case machine::FrameOperation::Restore: {
            const bool save = effect.operation == machine::FrameOperation::Save;
            const auto& transfers = effect.transfers;
            const bool pair = transfers.size() == 2;
            const auto registers = pair ? reg(transfers[0].reg) + ", " + reg(transfers[1].reg)
                                        : reg(transfers[0].reg);
            const auto* mnemonic = pair ? (save ? "stp " : "ldp ") : (save ? "str " : "ldr ");
            std::string address;
            if (effect.update == machine::FrameUpdate::BeforeMemory) {
                address = "[sp, #" + std::to_string(effect.stack_delta) + "]!";
            } else if (effect.update == machine::FrameUpdate::AfterMemory) {
                address = "[sp], #" + std::to_string(effect.stack_delta);
            } else {
                address = "[sp, #" + std::to_string(transfers[0].offset) + "]";
            }
            line(mnemonic + registers + ", " + address);
            break;
        }
        }
        if (cfi_) {
            cfi_frame_.transition(out_, cfi_state_, cfi_frame_.after(cfi_state_, effect));
        }
    }

    void emit_epilogue() {
        if (!function_->frame.program) return;
        for (const auto& instruction : function_->frame.program->epilogue) emit_frame(instruction);
    }

    // --- Control ---

    void emit_branch_to(machine::BlockId target, std::optional<machine::BlockId> next) {
        if (next && *next == target) return;
        line("b " + block_label(target));
    }

    void emit_control(const Instruction& instruction, std::optional<machine::BlockId> next) {
        switch (instruction.kind) {
        case machine::InstructionKind::Copy: {
            const auto to = physical(instruction.defs.front());
            const auto from = physical(instruction.uses.front());
            if (to != from) {
                const auto bits = instruction.defs.front().mode.bits > 32 ? 64U : 32U;
                line("mov " + register_name(to, bits) + ", " + register_name(from, bits));
            }
            return;
        }
        case machine::InstructionKind::Call: {
            const auto& callee = instruction.operands.front();
            const auto* direct = std::get_if<machine::SymbolOperand>(&callee);
            if (instruction.must_tail) {
                emit_epilogue();
                if (direct) line("b " + symbol(*direct));
                else line("br " + wide(std::get<machine::RegisterOperand>(callee).value));
                tail_transfer_ = true;
                return;
            }
            if (direct) line("bl " + symbol(*direct));
            else line("blr " + wide(std::get<machine::RegisterOperand>(callee).value));
            return;
        }
        case machine::InstructionKind::Branch:
            emit_branch_to(std::get<machine::BlockOperand>(instruction.operands.front()).target,
                           next);
            return;
        case machine::InstructionKind::ConditionalBranch: {
            const auto condition = static_cast<Condition>(immediate_at(instruction, 0));
            const auto truth = std::get<machine::BlockOperand>(instruction.operands[1]).target;
            const auto falsity = std::get<machine::BlockOperand>(instruction.operands[2]).target;
            if (next && *next == truth) {
                line(std::string(branch_mnemonic(invert(condition))) + ' ' +
                     block_label(falsity));
                return;
            }
            line(std::string(branch_mnemonic(condition)) + ' ' + block_label(truth));
            emit_branch_to(falsity, next);
            return;
        }
        case machine::InstructionKind::IndirectBranch:
            line("br " + wide(register_at(instruction, 0)));
            return;
        case machine::InstructionKind::Return:
            if (tail_transfer_) return;
            emit_epilogue();
            line("ret");
            return;
        case machine::InstructionKind::Unreachable: return;
        case machine::InstructionKind::Trap: line("brk #1000"); return;
        case machine::InstructionKind::Target: emit_target(instruction); return;
        }
    }

    // --- Functions ---

    bool own_section(const hir::Function& entity) const {
        return options_.function_sections || entity.retain || entity.mergeable ||
               entity.temperature != hir::FunctionTemperature::Normal;
    }

    bool emit_function(const machine::Function& function) {
        function_ = &function;
        const auto& entity = hir_.function(function.source);
        stem_ = hir::label_stem(entity);
        const auto symbol_name = assembly_symbol(format_, function.symbol);
        const auto section =
            entity.section ? *entity.section
            : own_section(entity)
                ? std::string(entity.temperature == hir::FunctionTemperature::Hot    ? ".text.hot."
                              : entity.temperature == hir::FunctionTemperature::Cold ? ".text.unlikely."
                                                                                     : ".text.") +
                      function.symbol
                : std::string(".text");
        std::string error;
        const auto directive = assembly_section_directive(
            format_,
            {section, AssemblySectionKind::Code, entity.section.has_value(),
             entity.retain || hir_.retained_code_sections.contains(section),
             entity.mergeable ? std::string_view(symbol_name) : std::string_view{}},
            error);
        if (!directive) {
            diagnostics_.error(function.location, error);
            return false;
        }
        out_ << *directive << '\n';
        unsigned alignment = 2;
        if (options_.function_alignment != 0) {
            alignment = static_cast<unsigned>(std::countr_zero(options_.function_alignment));
        } else if (const auto preferred = subtarget_.function_alignment();
                   preferred && options_.optimize_for != OptimizationGoal::Size &&
                   options_.optimize_for != OptimizationGoal::MinimumSize) {
            alignment = std::max(alignment, static_cast<unsigned>(std::countr_zero(*preferred)));
        }
        alignment = std::max(alignment,
                             static_cast<unsigned>(std::countr_zero(entity.minimum_alignment)));
        out_ << ".p2align " << alignment << '\n';
        const auto directives = assembly_symbol_directives(
            format_,
            {symbol_name, entity.linkage == Linkage::Global, true, entity.weak,
             assembly_visibility(entity.visibility), entity.mergeable},
            error);
        if (!directives) {
            diagnostics_.error(function.location, error);
            return false;
        }
        if (!directives->empty()) out_ << *directives << '\n';
        out_ << ".type " << symbol_name << ",@function\n" << symbol_name << ":\n";
        cfi_ = (options_.unwind_tables || options_.asynchronous_unwind_tables ||
                (debug_ && debug_->frames())) &&
               assembly_uses_dwarf_cfi(format_);
        cfi_state_ = cfi_frame_.entry();
        cfi_body_ = cfi_state_;
        if (function.frame.program) {
            for (const auto& instruction : function.frame.program->prologue) {
                cfi_body_ = cfi_frame_.after(cfi_body_, *instruction.frame_effect);
            }
        }
        if (debug_) debug_->begin_function(function.source);
        if (cfi_) out_ << ".cfi_startproc\n";
        if (function.frame.program) {
            for (const auto& instruction : function.frame.program->prologue) {
                if (debug_) debug_->row(out_, function.location);
                emit_frame(instruction);
            }
        }
        if (debug_) debug_->end_prologue();
        for (std::size_t index = 0; index < function.layout.size(); ++index) {
            const auto id = function.layout[index];
            const auto found = std::find_if(function.blocks.begin(), function.blocks.end(),
                                            [&](const machine::Block& block) { return block.id == id; });
            if (found == function.blocks.end()) continue;
            const auto next = index + 1 < function.layout.size()
                                  ? std::optional<machine::BlockId>(function.layout[index + 1])
                                  : std::nullopt;
            out_ << block_label(id) << ":\n";
            for (const auto& label : function.labels) {
                if (label.block != id) continue;
                const auto& label_entity = hir_.labels.at(label.label.value);
                out_ << hir::local_label_symbol(hir_, label_entity) << ":\n";
                if (!label_entity.is_global) continue;
                const auto label_symbol = assembly_symbol(format_, label_entity.link_symbol);
                out_ << ".globl " << label_symbol << '\n'
                     << ".type " << label_symbol << ",@function\n"
                     << label_symbol << ":\n";
            }
            if (cfi_) cfi_frame_.transition(out_, cfi_state_, cfi_body_);
            tail_transfer_ = false;
            for (const auto& instruction : found->instructions) {
                row(instruction);
                emit_control(instruction, next);
            }
        }
        if (cfi_) out_ << ".cfi_endproc\n";
        if (debug_) {
            describe_homes(function);
            out_ << debug_->end_function(function.source, symbol_name, cfi_) << ":\n";
        }
        out_ << ".size " << symbol_name << ", .-" << symbol_name << '\n';
        return diagnostics_.errors() == 0;
    }

    // Frame slots that hold source objects for all of the body.
    void describe_homes(const machine::Function& function) {
        if (!debug_->variables() || !function.frame.program) return;
        const auto base = dwarf_register(stack_pointer);
        if (cfi_) debug_->frame_base(base, function.frame.program->stack_size);
        for (std::uint32_t index = 0; index < function.stack_slots.size(); ++index) {
            const auto& slot = function.stack_slots[index];
            if (slot.kind == machine::StackSlotKind::Local && !slot.elided && slot.frame_offset) {
                debug_->slot_home(index, slot.name, base, *slot.frame_offset);
            }
        }
    }

    const hir::Module& hir_;
    const Subtarget& subtarget_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    native::DebugInfo* debug_;
    ObjectFormat format_;
    native::FrameCfi cfi_frame_;
    native::CfiState cfi_state_;
    native::CfiState cfi_body_;
    bool cfi_{};
    const machine::Function* function_{};
    std::string stem_;
    std::ostringstream out_;
    // A [[musttail]] transfer left the function; the return after it emits
    // nothing.
    bool tail_transfer_{};
};

} // namespace

mir::ManagedAssembly emit_assembly(const machine::Module& module, const hir::Module& hir_module,
                                   const Subtarget& subtarget, const CompilerOptions& options,
                                   Diagnostics& diagnostics, native::DebugInfo* debug) {
    return Emitter(hir_module, subtarget, options, diagnostics, debug).run(module);
}

} // namespace cross::aarch64

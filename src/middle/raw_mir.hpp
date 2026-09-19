// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/mir.hpp"
#include "target/target.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace cross::mir {

struct RegisterOperand {
    std::string name;
    std::string storage;
    unsigned bits{};
};

struct ImmediateOperand {
    std::uint64_t value{};
    unsigned bits{};
    bool patch{};
    std::optional<PatchSink> patch_sink;
    std::uint32_t patch_id{};
};

struct LabelOperand {
    BlockId target;
    std::string source_name;
};

struct MemoryOperand {
    RegisterOperand base;
    std::optional<RegisterOperand> index;
    unsigned scale{1};
    std::int64_t displacement{};
    unsigned bits{};
    bool is_volatile{};
    bool is_atomic{};
};

struct Operand {
    enum class Kind { Register, Immediate, Label, Memory } kind{Kind::Immediate};
    SourceLocation location;
    RegisterOperand reg;
    ImmediateOperand immediate;
    LabelOperand label;
    MemoryOperand memory;
};

struct Instruction {
    SourceLocation location;
    const InstructionEntry* form{};
    std::vector<Operand> operands;
};

enum class BlockExitKind { None, Fallthrough, Instruction };

struct RawBlock {
    BlockId id;
    SourceLocation location;
    std::string source_label;
    std::vector<Instruction> instructions;
    std::vector<BlockId> predecessors;
    std::vector<BlockId> successors;
    BlockExitKind exit{BlockExitKind::None};
    bool reachable{};
};

struct RawFunction {
    hir::FunctionId source;
    SourceLocation location;
    std::string symbol;
    Linkage linkage{Linkage::Group};
    std::optional<std::string> section;
    unsigned minimum_alignment{1};
    BlockId entry;
    std::vector<RawBlock> blocks;
    std::vector<BlockId> layout;
};

struct RawModule {
    [[nodiscard]] bool owns(hir::FunctionId id) const {
        return definitions.contains(id.value);
    }

    std::vector<RawFunction> functions;
    std::unordered_set<std::uint32_t> definitions;
    std::unordered_set<std::uint32_t> object_definitions;
};

struct AssemblyBundle {
    struct PatchRelocation {
        PatchSink sink;
        std::string end_label;
        unsigned field_bytes{};
    };

    [[nodiscard]] bool owns(hir::FunctionId id) const {
        return definitions.contains(id.value);
    }
    [[nodiscard]] bool owns(hir::ObjectId id) const {
        return object_definitions.contains(id.value);
    }

    std::unordered_set<std::uint32_t> definitions;
    std::unordered_set<std::uint32_t> object_definitions;
    std::vector<PatchRelocation> patch_relocations;
    std::string module_assembly;
};

} // namespace cross::mir

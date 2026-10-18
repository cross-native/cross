// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Property values of model entries as the parser reads them, shared by the
// parts of the model loader.
namespace cross {

struct ModelValue {
    enum class Kind { Text, Integer, Boolean, List } kind{Kind::Text};
    std::string text;
    // The magnitude of an integer; `negative` gives its sign.
    std::uint64_t integer{};
    bool negative{};
    bool boolean{};
    std::vector<std::string> list;
};

struct ModelProperty {
    ModelValue value;
    unsigned line{};
};

using ModelProperties = std::unordered_map<std::string, ModelProperty>;

// The compiled-in target of an architecture name that `cc --print-targets`
// prints.
const TargetInfo* architecture_target(std::string_view architecture);

// An error at a line of the file being read.
struct ModelProblem {
    unsigned line{};
    std::string message;
};

// The `instruction` entry `name` of `origin` with `properties`, checked on
// its own; the checks of the whole table follow loading.
std::optional<ModelInstruction> make_instruction(
    std::string name, unsigned line, const std::string& origin,
    const ModelProperties& properties, ModelProblem& problem);

} // namespace cross

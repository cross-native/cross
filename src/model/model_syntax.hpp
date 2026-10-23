// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model/model.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Property values of model entries as the parser reads them, shared by the
// parts of the model loader.
namespace cross {

// A declaration's source range and the names needed before target selection.
struct ModelEntrySource {
    std::size_t offset{};
    std::size_t size{};
    unsigned line{};
    std::string domain;
    std::string name;
    std::string architecture;
    std::vector<std::string> aliases;
    bool function_selectable{true};
};

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

// The properties of an entry or block in source order. Entries have few,
// so a lookup scans them.
class ModelProperties {
public:
    using Item = std::pair<std::string, ModelProperty>;

    [[nodiscard]] auto begin() const { return items_.begin(); }
    [[nodiscard]] auto end() const { return items_.end(); }
    [[nodiscard]] std::vector<Item>::const_iterator find(
        std::string_view name) const {
        return std::find_if(items_.begin(), items_.end(),
                            [&](const Item& item) { return item.first == name; });
    }
    [[nodiscard]] bool contains(std::string_view name) const {
        return find(name) != items_.end();
    }
    [[nodiscard]] const ModelProperty& at(std::string_view name) const {
        const auto found = find(name);
        if (found == items_.end()) throw std::out_of_range("model property");
        return found->second;
    }
    // False when the entry already has the property.
    bool add(std::string name, ModelProperty property) {
        if (contains(name)) return false;
        items_.emplace_back(std::move(name), std::move(property));
        return true;
    }

private:
    std::vector<Item> items_;
};

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

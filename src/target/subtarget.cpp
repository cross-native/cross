// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/subtarget.hpp"

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace cross {
namespace {

ObjectFormat format_for_triple(std::string_view triple) {
    if (triple.find("darwin") != std::string_view::npos ||
        triple.find("apple") != std::string_view::npos) return ObjectFormat::MachO;
    if (triple.find("windows") != std::string_view::npos ||
        triple.find("mingw") != std::string_view::npos) return ObjectFormat::Coff;
    if (triple.find('-') != std::string_view::npos) return ObjectFormat::Elf;
    return ObjectFormat::Unsupported;
}

const SubtargetFeature* find_feature(const SubtargetTable& table, std::string_view name) {
    for (const auto& feature : table.features) if (feature.name == name) return &feature;
    return nullptr;
}

const SubtargetCpu* find_cpu(const SubtargetTable& table, std::string_view name) {
    for (const auto& cpu : table.cpus) if (cpu.name == name) return &cpu;
    return nullptr;
}

} // namespace

bool Subtarget::has_feature(TargetFeatureId feature) const {
    if (!feature.valid() || !table_ || feature.value >= table_->features.size()) {
        return false;
    }
    const auto word = static_cast<std::size_t>(feature.value) / 64U;
    const auto bit = static_cast<unsigned>(feature.value) % 64U;
    return word < enabled_feature_words_.size() &&
           (enabled_feature_words_[word] & (std::uint64_t{1} << bit)) != 0;
}

unsigned Subtarget::integer_constant_materialization_cost(
    const IntegerConstantCostQuery& query) const {
    if (target_ && target_->cost_model.integer_constant_materialization) {
        return target_->cost_model.integer_constant_materialization(
            *this, query);
    }

    // Preserve the generic policy used before targets supplied costs.  Small
    // integers normally fold into an instruction immediate; wide values need
    // a separately materialized register on common 64-bit machines.
    if (query.bits > 64 || query.high != 0) return 2;
    if (query.bits < 64) return 1;
    const auto value = static_cast<std::int64_t>(query.low);
    return value < std::numeric_limits<std::int32_t>::min() ||
                   value > std::numeric_limits<std::int32_t>::max()
               ? 2U
               : 1U;
}

std::optional<TargetFeatureId> Subtarget::feature_id(
    std::string_view name) const {
    if (!table_) return std::nullopt;
    for (std::size_t index = 0; index < table_->features.size(); ++index) {
        if (table_->features[index].name == name) {
            if (index >= TargetFeatureId::invalid_value) return std::nullopt;
            return TargetFeatureId{static_cast<std::uint16_t>(index)};
        }
    }
    return std::nullopt;
}

std::string_view Subtarget::feature_name(TargetFeatureId feature) const {
    if (!feature.valid() || !table_ || feature.value >= table_->features.size()) {
        return {};
    }
    return table_->features[feature.value].name;
}

bool Subtarget::has_feature(std::string_view name) const {
    const auto feature = feature_id(name);
    return feature && has_feature(*feature);
}

const SubtargetTable* subtarget_table_for(const TargetInfo& target) {
    return target.subtargets;
}

bool normalize_subtarget_options(const TargetInfo& target,
                                 CompilerOptions& options,
                                 Diagnostics& diagnostics) {
    const auto* table = subtarget_table_for(target);
    if (!table) {
        diagnostics.command_error(
            "target '" + std::string(target.architecture) +
            "' has no registered subtarget description");
        return false;
    }
    const auto* arch_option = find_resolved_option(options, "m.arch");
    const auto arch = std::string(
        resolved_text(options, "m.arch", options.cpu));
    const auto* cpu = find_cpu(*table, arch);
    if (!cpu) {
        diagnostics.command_error(
            "unknown " + std::string(target.architecture) +
            " architecture CPU '" + arch + "'");
        return false;
    }

    auto* tune_option = [&]() -> ResolvedOption* {
        const auto found = options.resolved_options.find("m.tune");
        return found == options.resolved_options.end() ? nullptr
                                                       : &found->second;
    }();
    if (tune_option && tune_option->origin == OptionOrigin::Default &&
        tune_option->source == "default" && arch != "generic") {
        tune_option->value = arch;
        tune_option->origin = arch_option ? arch_option->origin
                                          : OptionOrigin::Default;
        tune_option->source = "implied by m.arch=" + arch;
    }
    const auto tune = std::string(
        resolved_text(options, "m.tune", options.tune));
    if (!find_cpu(*table, tune)) {
        diagnostics.command_error(
            "unknown " + std::string(target.architecture) +
            " tuning CPU '" + tune + "'");
        return false;
    }

    std::unordered_map<std::string, bool> states;
    for (const auto& feature : table->features) {
        states.emplace(std::string(feature.name),
                       feature.enabled_by_default);
    }
    for (const auto feature : cpu->enabled_features) {
        states[std::string(feature)] = true;
    }

    const auto arch_origin = arch_option ? arch_option->origin
                                         : OptionOrigin::Default;
    std::unordered_map<std::string, bool> explicit_features;
    std::unordered_map<std::string, bool> explicit_values;
    const auto rank = [](OptionOrigin origin) {
        return static_cast<unsigned>(origin);
    };
    for (const auto& feature : table->features) {
        if (!feature.selectable) continue;
        const auto name = "m." + std::string(feature.name);
        auto found = options.resolved_options.find(name);
        if (found == options.resolved_options.end()) continue;
        auto& selection = found->second;
        const bool explicitly_selected =
            rank(selection.origin) > rank(arch_origin) ||
            (selection.origin == arch_origin && selection.source != "default");
        if (explicitly_selected) {
            if (const auto* value = std::get_if<bool>(&selection.value)) {
                states[std::string(feature.name)] = *value;
                explicit_features[std::string(feature.name)] = true;
                explicit_values[std::string(feature.name)] = *value;
            }
        } else {
            selection.value = states[std::string(feature.name)];
            selection.origin = arch_origin;
            selection.source = "m.arch=" + arch;
        }
    }

    for (const auto& feature : table->features) {
        for (const auto required : feature.prerequisites) {
            if (!find_feature(*table, required)) {
                diagnostics.command_error(
                    "target description feature '" +
                    std::string(feature.name) + "' requires unknown feature '" +
                    std::string(required) + "'");
                return false;
            }
        }
        for (const auto conflict : feature.conflicts) {
            if (!find_feature(*table, conflict)) {
                diagnostics.command_error(
                    "target description feature '" +
                    std::string(feature.name) + "' conflicts with unknown feature '" +
                    std::string(conflict) + "'");
                return false;
            }
        }
    }
    const auto known_instruction_feature = [&](std::string_view name) {
        return name.empty() || name == "base" ||
               name == target.architecture || find_feature(*table, name);
    };
    for (const auto& instruction : target.instructions) {
        if (!known_instruction_feature(instruction.feature)) {
            diagnostics.command_error(
                "target instruction '" + std::string(instruction.name) +
                "' requires unknown feature '" +
                std::string(instruction.feature) + "'");
            return false;
        }
        for (const auto required : instruction.required_features) {
            if (known_instruction_feature(required)) continue;
            diagnostics.command_error(
                "target instruction '" + std::string(instruction.name) +
                "' requires unknown feature '" + std::string(required) +
                "'");
            return false;
        }
    }

    const auto explicit_value = [&](std::string_view name,
                                    bool value) {
        const auto selected = explicit_features.find(std::string(name));
        if (selected == explicit_features.end()) return false;
        const auto found = explicit_values.find(std::string(name));
        return found != explicit_values.end() && found->second == value;
    };
    const auto update_implied_option = [&](std::string_view name, bool value,
                                           std::string source,
                                           OptionOrigin origin) {
        auto found = options.resolved_options.find("m." + std::string(name));
        if (found == options.resolved_options.end() ||
            explicit_features.contains(std::string(name))) {
            return;
        }
        found->second.value = value;
        found->second.origin = origin;
        found->second.source = std::move(source);
    };

    // A negative selection makes every transitive dependent unavailable.
    // Remember the explicit root rather than only the current false state:
    // otherwise a sibling dependent can re-enable an intermediate feature and
    // make the fixed-point loop oscillate (for example haswell
    // -mno-sse4.2: avx disables itself, then f16c re-enables avx).
    std::unordered_map<std::string, std::string> unavailable_by;
    for (const auto& [name, value] : explicit_values) {
        if (!value) unavailable_by.emplace(name, name);
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& feature : table->features) {
            const auto name = std::string(feature.name);
            if (!states[name]) continue;
            for (const auto required_view : feature.prerequisites) {
                const auto required = std::string(required_view);
                if (states[required]) continue;
                const auto unavailable = unavailable_by.find(required);
                if (unavailable != unavailable_by.end()) {
                    const auto& root = unavailable->second;
                    if (explicit_value(name, true)) {
                        if (explicit_value(root, false)) {
                            diagnostics.command_error(
                                "target option '-m" + name +
                                "' requires '-m" + root +
                                "' and conflicts with explicit '-mno-" +
                                root + "'");
                        } else {
                            diagnostics.command_error(
                                "target option '-m" + name +
                                "' requires unavailable feature '-m" +
                                root + "'");
                        }
                        return false;
                    }
                    states[name] = false;
                    unavailable_by.emplace(name, root);
                    const auto* required_option =
                        find_resolved_option(options, "m." + root);
                    update_implied_option(
                        name, false, "disabled with m." + root,
                        required_option ? required_option->origin : arch_origin);
                    changed = true;
                    break;
                }
                states[required] = true;
                const auto* requiring_option =
                    find_resolved_option(options, "m." + name);
                update_implied_option(
                    required, true, "required by m." + name,
                    requiring_option ? requiring_option->origin : arch_origin);
                changed = true;
            }
            if (!states[name]) continue;
            for (const auto conflict_view : feature.conflicts) {
                const auto conflict = std::string(conflict_view);
                if (!states[conflict]) continue;
                if (explicit_value(name, true) &&
                    explicit_value(conflict, true)) {
                    diagnostics.command_error(
                        "target options '-m" + name + "' and '-m" + conflict +
                        "' conflict");
                    return false;
                }
                if (explicit_value(conflict, true)) {
                    states[name] = false;
                    unavailable_by.emplace(name, name);
                    update_implied_option(
                        name, false, "conflicts with m." + conflict,
                        arch_origin);
                } else {
                    states[conflict] = false;
                    unavailable_by.emplace(conflict, conflict);
                    update_implied_option(
                        conflict, false, "conflicts with m." + name,
                        arch_origin);
                }
                changed = true;
            }
        }
    }

    options.cpu = arch;
    options.tune = tune;
    options.target_features.clear();
    for (const auto& feature : table->features) {
        if (!feature.selectable) continue;
        options.target_features.push_back(
            std::string(states[std::string(feature.name)] ? "+" : "-") +
            std::string(feature.name));
    }
    return true;
}

std::optional<Subtarget> resolve_subtarget(const TargetInfo& target,
                                           const CompilerOptions& options,
                                           const SubtargetTable& table,
                                           Diagnostics& diagnostics) {
    if (table.architecture != target.architecture) {
        diagnostics.command_error("subtarget table architecture '" + std::string(table.architecture) +
                                  "' does not describe target '" + std::string(target.architecture) + "'");
        return std::nullopt;
    }
    const auto format = format_for_triple(options.target);
    if (format == ObjectFormat::Unsupported) {
        diagnostics.command_error("object format for target '" + options.target +
                                  "' is not implemented by " + std::string(target.architecture));
        return std::nullopt;
    }
    const auto* abi = find_abi(target, options.abi, options.target);
    if (!abi) {
        diagnostics.command_error("unknown ABI '" + options.abi + "' for target '" + options.target + "'");
        return std::nullopt;
    }
    if (!abi->compilation_selectable) {
        diagnostics.command_error("ABI '" + std::string(abi->canonical_name) +
                                  "' cannot be selected for a compilation");
        return std::nullopt;
    }
    const auto* cpu = find_cpu(table, options.cpu);
    if (!cpu) {
        diagnostics.command_error("unknown " + std::string(target.architecture) + " CPU '" +
                                  options.cpu + "'");
        return std::nullopt;
    }
    const auto* tune = find_cpu(table, options.tune);
    if (!tune) {
        diagnostics.command_error("unknown " +
                                  std::string(target.architecture) +
                                  " tuning CPU '" + options.tune + "'");
        return std::nullopt;
    }

    std::unordered_map<std::string, bool> states;
    for (const auto& feature : table.features) states.emplace(std::string(feature.name), feature.enabled_by_default);
    for (const auto feature : cpu->enabled_features) states[std::string(feature)] = true;
    std::unordered_map<std::string, char> selections;
    std::unordered_map<std::string, std::string> unavailable_by;

    bool valid = true;
    for (const auto& spelling : options.target_features) {
        if (spelling.size() < 2 || (spelling.front() != '+' && spelling.front() != '-')) {
            diagnostics.command_error("malformed target feature selection '" + spelling + "'");
            valid = false;
            continue;
        }
        const auto name = std::string_view(spelling).substr(1);
        const auto* feature = find_feature(table, name);
        if (!feature) {
            diagnostics.command_error("unknown or unsupported " + std::string(target.architecture) +
                                      " feature '" + std::string(name) + "'");
            valid = false;
            continue;
        }
        if (!feature->selectable) {
            diagnostics.command_error("required " + std::string(target.architecture) + " feature '" +
                                      std::string(name) + "' cannot be disabled or enabled explicitly");
            valid = false;
            continue;
        }
        // The option parser preserves source order; assignment here makes the
        // final occurrence authoritative, including -m/-mno interleaving.
        states[std::string(name)] = spelling.front() == '+';
        selections[std::string(name)] = spelling.front();
        if (spelling.front() == '-') {
            unavailable_by[std::string(name)] = std::string(name);
        } else {
            unavailable_by.erase(std::string(name));
        }
    }
    if (!valid) return std::nullopt;

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& feature : table.features) {
            const auto name = std::string(feature.name);
            if (!states[name]) continue;
            for (const auto required_view : feature.prerequisites) {
                const auto required = std::string(required_view);
                if (!find_feature(table, required)) {
                    diagnostics.command_error(
                        "target description feature '" + name +
                        "' requires unknown feature '" + required + "'");
                    return std::nullopt;
                }
                if (states[required]) continue;
                const auto unavailable = unavailable_by.find(required);
                if (unavailable != unavailable_by.end()) {
                    const auto& root = unavailable->second;
                    if (selections[name] == '+') {
                        if (selections[root] == '-') {
                            diagnostics.command_error(
                                "target feature '" + name + "' requires '" +
                                root + "' but it is explicitly disabled");
                        } else {
                            diagnostics.command_error(
                                "target feature '" + name + "' requires "
                                "unavailable feature '" + root + "'");
                        }
                        return std::nullopt;
                    }
                    states[name] = false;
                    unavailable_by.emplace(name, root);
                } else {
                    states[required] = true;
                }
                changed = true;
                break;
            }
            if (!states[name]) continue;
            for (const auto conflict_view : feature.conflicts) {
                const auto conflict = std::string(conflict_view);
                if (!find_feature(table, conflict)) {
                    diagnostics.command_error(
                        "target description feature '" + name +
                        "' conflicts with unknown feature '" + conflict + "'");
                    return std::nullopt;
                }
                if (!states[conflict]) continue;
                if (selections[name] == '+' && selections[conflict] == '+') {
                    diagnostics.command_error(
                        "target features '" + name + "' and '" + conflict +
                        "' conflict");
                    return std::nullopt;
                }
                if (selections[conflict] == '+') {
                    states[name] = false;
                    unavailable_by.emplace(name, name);
                } else {
                    states[conflict] = false;
                    unavailable_by.emplace(conflict, conflict);
                }
                changed = true;
            }
        }
    }

    Subtarget result;
    result.target_ = &target;
    result.abi_ = abi;
    result.table_ = &table;
    result.object_format_ = format;
    result.cpu_ = std::string(cpu->name);
    result.tune_ = std::string(tune->name);
    result.enabled_feature_words_.assign((table.features.size() + 63U) / 64U,
                                         0);
    for (std::size_t index = 0; index < table.features.size(); ++index) {
        const auto& feature = table.features[index];
        if (states[std::string(feature.name)]) {
            result.enabled_features_.emplace_back(feature.name);
            result.enabled_feature_words_[index / 64U] |=
                std::uint64_t{1} << (index % 64U);
        }
    }
    return result;
}

std::optional<Subtarget> resolve_subtarget(const TargetInfo& target,
                                           const CompilerOptions& options,
                                           Diagnostics& diagnostics) {
    const auto* table = subtarget_table_for(target);
    if (!table) {
        diagnostics.command_error("target '" + std::string(target.architecture) +
                                  "' has no registered subtarget description");
        return std::nullopt;
    }
    return resolve_subtarget(target, options, *table, diagnostics);
}

} // namespace cross

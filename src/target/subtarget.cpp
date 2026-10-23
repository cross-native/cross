// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/subtarget.hpp"

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "model/model.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
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

const CpuEntry* find_cpu(const TargetInfo& target, std::string_view name) {
    return model_registry().find_cpu(target.architecture, name);
}

// Whether `a` or `b` lists the other as a conflict.
bool conflicting(const SubtargetFeature& a, const SubtargetFeature& b) {
    return std::ranges::find(a.conflicts, b.name) != a.conflicts.end() ||
           std::ranges::find(b.conflicts, a.name) != b.conflicts.end();
}

// The first list of `feature`'s alternative prerequisites of which
// `enabled` has none.
template <typename Enabled>
const std::vector<std::string_view>* missing_alternative(
    const SubtargetFeature& feature, Enabled enabled) {
    for (const auto& alternatives : feature.any_prerequisites) {
        if (std::ranges::none_of(alternatives, enabled)) return &alternatives;
    }
    return nullptr;
}

// Whether `feature` is `other` or requires it, directly or through its
// prerequisites.
bool requires_feature(const SubtargetTable& table, std::string_view feature,
                      std::string_view other) {
    if (feature == other) return true;
    const auto* entry = find_feature(table, feature);
    return entry && std::ranges::any_of(entry->prerequisites,
                                        [&](std::string_view required) {
                                            return requires_feature(table, required, other);
                                        });
}

// "'-mA' or '-mB'".
std::string alternative_options(const std::vector<std::string_view>& names) {
    std::string result;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) result += index + 1 == names.size() ? " or " : ", ";
        result += "'-m" + std::string(names[index]) + "'";
    }
    return result;
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

std::optional<unsigned> Subtarget::integer_operation_cost(
    CostedIntegerOperation operation,
    const IntegerOperationCostQuery& query) const {
    if (!target_ || !target_->cost_model.integer_operation) return std::nullopt;
    return path_latency(
        target_->cost_model.integer_operation(*this, operation, query));
}

std::optional<unsigned> Subtarget::bit_operation_cost(
    CostedBitOperation operation, unsigned bits) const {
    if (!target_ || !target_->cost_model.bit_operation) return std::nullopt;
    return path_latency(
        target_->cost_model.bit_operation(*this, operation, bits));
}

std::optional<unsigned> Subtarget::select_cost(
    const SelectCostQuery& query) const {
    if (!target_ || !target_->cost_model.select) return std::nullopt;
    return path_latency(target_->cost_model.select(*this, query));
}

std::optional<std::vector<InstructionCostId>> Subtarget::vector_operation(
    const VectorOperationCostQuery& query) const {
    if (!target_ || !target_->cost_model.vector_operation) return std::nullopt;
    auto instructions = target_->cost_model.vector_operation(*this, query);
    if (std::any_of(instructions.begin(), instructions.end(),
                    [&](InstructionCostId instruction) {
                        return !has_instruction(instruction);
                    })) {
        instructions.clear();
    }
    return instructions;
}

std::optional<unsigned> Subtarget::path_latency(
    const std::vector<InstructionCostId>& instructions) const {
    if (instructions.empty()) return std::nullopt;
    unsigned cost{};
    for (const auto instruction : instructions) {
        if (!has_instruction(instruction)) return std::nullopt;
        cost += latency(instruction);
    }
    return cost;
}

unsigned Subtarget::integer_register_bits() const {
    return target_ && target_->cost_model.integer_register_bits
        ? target_->cost_model.integer_register_bits(*this) : 64U;
}

std::optional<InstructionFeatureConflict> Subtarget::form_conflict(
    const InstructionEntry& form) const {
    if (auto conflict = instruction_feature_conflict(
            form, [&](std::string_view feature) {
                return supports_registry_feature(feature);
            })) {
        return conflict;
    }
    if (lacks_instructions_ &&
        !has_instruction(target_->form_instructions[instruction_form_id(
            *target_, form).value])) {
        return InstructionFeatureConflict{form.assembly_mnemonic, false, true};
    }
    return std::nullopt;
}

floating::NanEncoding Subtarget::nan_encoding() const {
    const auto& layout = target_->data_layout;
    return !layout.nan2008_feature.empty() && has_feature(layout.nan2008_feature)
        ? layout.nan2008_encoding : layout.nan_encoding;
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

bool Subtarget::supports_registry_feature(std::string_view name) const {
    return name.empty() || name == "base" || name == target_->architecture || has_feature(name);
}

const SubtargetTable* subtarget_table_for(const TargetInfo& target) {
    return target.subtargets;
}

std::optional<CpuSelection> parse_cpu_selection(
    const TargetInfo& target, std::string_view text, std::string& error) {
    // The longest prefix, so that a name with a '+', such as octeon+, stays
    // whole.
    auto end = text.size();
    while (!find_cpu(target, text.substr(0, end))) {
        end = text.substr(0, end).rfind('+');
        if (end == std::string_view::npos) return std::nullopt;
    }
    CpuSelection result{std::string(text.substr(0, end)), {}};
    const auto* table = subtarget_table_for(target);
    const auto architecture = std::string(target.architecture);
    while (end < text.size()) {
        const auto begin = end + 1;
        end = std::min(text.find('+', begin), text.size());
        const auto modifier = text.substr(begin, end - begin);
        bool enable = true;
        const auto* feature = table ? find_feature(*table, modifier) : nullptr;
        if (!feature && modifier.starts_with("no") && table) {
            feature = find_feature(*table, modifier.substr(2));
            enable = false;
        }
        if (!feature) {
            error = modifier.empty()
                        ? "missing feature after '+' in '" + std::string(text) + "'"
                        : "unknown " + architecture + " feature '" +
                              std::string(modifier) + "' in '" +
                              std::string(text) + "'";
            return std::nullopt;
        }
        if (!feature->selectable) {
            error = architecture + " feature '" + std::string(feature->name) +
                    "' in '" + std::string(text) +
                    "' is a CPU fact that a modifier cannot change";
            return std::nullopt;
        }
        result.modifiers.push_back({std::string(feature->name), enable});
    }
    return result;
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
    const auto arch_text = std::string(
        resolved_text(options, "m.arch", options.cpu));
    std::string selection_error;
    const auto cpu_selection =
        parse_cpu_selection(target, arch_text, selection_error);
    if (!cpu_selection) {
        diagnostics.command_error(
            !selection_error.empty()
                ? selection_error
                : "unknown " + std::string(target.architecture) +
                      " architecture CPU '" +
                      arch_text.substr(0, arch_text.find('+')) + "'");
        return false;
    }
    const auto arch = cpu_selection->cpu;
    const auto* cpu = find_cpu(target, arch);

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
    if (!find_cpu(target, tune)) {
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
    const auto cpu_features = model_registry().resolve_cpu(target, *cpu).features;
    for (const auto& feature : cpu_features) states[feature] = true;
    // The -march modifiers in order. +FEATURE selects the feature and what
    // its explicit selection selects; +noFEATURE disables it, what its
    // explicit disabling disables, and every feature that requires one of
    // them. Implications do not enable again what a modifier disabled.
    std::unordered_set<std::string> modifier_disabled;
    for (const auto& modifier : cpu_selection->modifiers) {
        const auto& feature = *find_feature(*table, modifier.feature);
        std::unordered_set<std::string> changed{modifier.feature};
        for (const auto other :
             modifier.enable ? feature.explicit_on : feature.explicit_off) {
            changed.emplace(other);
        }
        for (bool grew = !modifier.enable; grew;) {
            grew = false;
            for (const auto& other : table->features) {
                if (!changed.contains(std::string(other.name)) &&
                    std::ranges::any_of(other.prerequisites,
                                        [&](std::string_view required) {
                                            return changed.contains(
                                                std::string(required));
                                        })) {
                    changed.emplace(other.name);
                    grew = true;
                }
            }
        }
        for (const auto& name : changed) {
            states[name] = modifier.enable;
            if (modifier.enable) modifier_disabled.erase(name);
            else modifier_disabled.insert(name);
        }
    }
    // A feature that options cannot select is a fact of the -march CPU,
    // which no option may take away, also not through a prerequisite or a
    // conflict.
    const auto required_fact = [&](const SubtargetFeature& feature) {
        return !feature.selectable &&
               std::find(cpu_features.begin(), cpu_features.end(),
                         feature.name) != cpu_features.end();
    };
    const auto baseline_error = [&](std::string_view feature,
                                    std::string_view reason) {
        diagnostics.command_error(
            "feature '" + std::string(feature) + "' of CPU '" + arch + "' " +
            std::string(reason) +
            "; the CPU baseline cannot be disabled with -m overrides");
    };
    // The ABI's features, in order, replace the CPU's conflicting ones
    // where the features enabled so far imply their prerequisites.
    const auto* abi =
        model_registry().find_abi(target.architecture, options.abi,
                                  options.target);
    const auto implied = [&] {
        std::unordered_set<std::string> result;
        std::vector<std::string_view> pending;
        for (const auto& [name, enabled] : states) {
            if (enabled) pending.push_back(name);
        }
        while (!pending.empty()) {
            const auto name = std::string(pending.back());
            pending.pop_back();
            if (!result.insert(name).second) continue;
            const auto& feature = *find_feature(*table, name);
            pending.insert(pending.end(), feature.prerequisites.begin(),
                           feature.prerequisites.end());
            for (const auto other : feature.implies) {
                if (!modifier_disabled.contains(std::string(other))) {
                    pending.push_back(other);
                }
            }
        }
        return result;
    };
    // An ABI feature applies only where the CPU allows it: the features
    // enabled so far imply its prerequisites, and it conflicts with no
    // feature of the CPU that options cannot select.
    std::unordered_set<std::string> abi_features;
    for (const auto& name : abi ? abi->features : std::vector<std::string>{}) {
        const auto* feature = find_feature(*table, name);
        const auto available = implied();
        const auto has = [&](std::string_view required) {
            return available.contains(std::string(required));
        };
        if (!std::ranges::all_of(feature->prerequisites, has) ||
            missing_alternative(*feature, has)) {
            continue;
        }
        const auto conflicts = [&](const SubtargetFeature& other) {
            return states[std::string(other.name)] && conflicting(*feature, other);
        };
        if (std::ranges::any_of(table->features, [&](const SubtargetFeature& other) {
                return conflicts(other) && required_fact(other);
            })) {
            continue;
        }
        for (const auto& other : table->features) {
            if (conflicts(other)) states[std::string(other.name)] = false;
        }
        states[name] = true;
        abi_features.insert(name);
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
            selection.source = abi_features.contains(std::string(feature.name))
                ? "abi=" + abi->canonical_name : "m.arch=" + arch_text;
        }
    }

    for (const auto& feature : table->features) {
        auto required_names = feature.prerequisites;
        for (const auto& alternatives : feature.any_prerequisites) {
            required_names.insert(required_names.end(), alternatives.begin(),
                                  alternatives.end());
        }
        for (const auto required : required_names) {
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
        for (const auto forbidden : instruction.forbidden_features) {
            if (known_instruction_feature(forbidden)) continue;
            diagnostics.command_error(
                "target instruction '" + std::string(instruction.name) +
                "' excludes unknown feature '" + std::string(forbidden) +
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
    // What an explicit selection also selects or disables, unless that is
    // explicit too.
    for (const auto& feature : table->features) {
        const auto name = std::string(feature.name);
        const auto found = explicit_values.find(name);
        if (found == explicit_values.end()) continue;
        const bool value = found->second;
        const auto* option = find_resolved_option(options, "m." + name);
        for (const auto other_view :
             value ? feature.explicit_on : feature.explicit_off) {
            const auto other = std::string(other_view);
            if (explicit_features.contains(other)) continue;
            states[other] = value;
            if (!value) unavailable_by.emplace(other, name);
            update_implied_option(
                other, value,
                (value ? "selected with m." : "disabled with m.") + name,
                option ? option->origin : arch_origin);
        }
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
                        const bool disabled = explicit_value(root, false);
                        if (!requires_feature(*table, required, root)) {
                            // An explicit_off of `root` disabled `required`.
                            diagnostics.command_error(
                                "target option '-m" + name + "' requires '-m" +
                                required + "', which explicit '-mno-" + root +
                                "' disables");
                        } else if (disabled) {
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
                    if (required_fact(feature)) {
                        baseline_error(name, "requires '" + root +
                                                 "', which the options disable");
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
                    if (required_fact(feature)) {
                        baseline_error(name, "conflicts with '-m" + conflict +
                                                 "'");
                        return false;
                    }
                    states[name] = false;
                    unavailable_by.emplace(name, name);
                    update_implied_option(
                        name, false, "conflicts with m." + conflict,
                        arch_origin);
                } else {
                    if (const auto* lost = find_feature(*table, conflict);
                        lost && required_fact(*lost)) {
                        baseline_error(conflict, "conflicts with '-m" + name +
                                                     "'");
                        return false;
                    }
                    states[conflict] = false;
                    unavailable_by.emplace(conflict, conflict);
                    update_implied_option(
                        conflict, false, "conflicts with m." + name,
                        arch_origin);
                }
                changed = true;
            }
            if (!states[name]) continue;
            // Implications enable only what nothing has disabled.
            for (const auto implied_view : feature.implies) {
                const auto other = std::string(implied_view);
                if (states[other] || unavailable_by.contains(other) ||
                    modifier_disabled.contains(other)) {
                    continue;
                }
                states[other] = true;
                const auto* implying = find_resolved_option(options, "m." + name);
                update_implied_option(
                    other, true, "implied by m." + name,
                    implying ? implying->origin : arch_origin);
                changed = true;
            }
        }
        // Alternative prerequisites, once implications have settled: the
        // resolver does not choose one, so without any the feature goes.
        if (changed) continue;
        for (const auto& feature : table->features) {
            const auto name = std::string(feature.name);
            if (!states[name]) continue;
            const auto* alternatives = missing_alternative(
                feature, [&](std::string_view other) { return states[std::string(other)]; });
            if (!alternatives) continue;
            const auto needed = alternative_options(*alternatives);
            if (explicit_value(name, true)) {
                diagnostics.command_error("target option '-m" + name + "' requires " + needed);
                return false;
            }
            if (required_fact(feature)) {
                baseline_error(name, "requires " + needed);
                return false;
            }
            states[name] = false;
            unavailable_by.emplace(name, name);
            update_implied_option(name, false, "requires " + needed, arch_origin);
            changed = true;
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
    const auto* cpu = find_cpu(target, options.cpu);
    if (!cpu) {
        diagnostics.command_error("unknown " + std::string(target.architecture) + " CPU '" +
                                  options.cpu + "'");
        return std::nullopt;
    }
    const auto* tune = find_cpu(target, options.tune);
    if (!tune) {
        diagnostics.command_error("unknown " +
                                  std::string(target.architecture) +
                                  " tuning CPU '" + options.tune + "'");
        return std::nullopt;
    }
    const auto arch_model = model_registry().resolve_cpu(target, *cpu);
    const auto tune_model = model_registry().resolve_cpu(target, *tune);

    std::unordered_map<std::string, bool> states;
    for (const auto& feature : table.features) states.emplace(std::string(feature.name), feature.enabled_by_default);
    for (const auto& feature : arch_model.features) states[feature] = true;
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
        if (changed) continue;
        for (const auto& feature : table.features) {
            const auto name = std::string(feature.name);
            if (!states[name]) continue;
            const auto* alternatives = missing_alternative(
                feature, [&](std::string_view other) { return states[std::string(other)]; });
            if (!alternatives) continue;
            if (selections[name] == '+') {
                diagnostics.command_error("target feature '" + name + "' requires " +
                                          alternative_options(*alternatives));
                return std::nullopt;
            }
            states[name] = false;
            unavailable_by.emplace(name, name);
            changed = true;
        }
    }

    Subtarget result;
    result.target_ = &target;
    result.abi_ = abi;
    result.table_ = &table;
    result.object_format_ = format;
    result.cpu_ = cpu->canonical_name;
    result.tune_ = tune->canonical_name;
    result.issue_width_ = tune_model.issue_width;
    result.function_alignment_ = tune_model.function_alignment;
    result.instruction_costs_ = tune_model.instructions;
    for (std::size_t index = 0; index < result.instruction_costs_.size();
         ++index) {
        result.instruction_costs_[index].available =
            arch_model.instructions[index].available;
        result.lacks_instructions_ = result.lacks_instructions_ ||
                                     !arch_model.instructions[index].available;
    }
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

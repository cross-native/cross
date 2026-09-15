// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/manual_abi_plan.hpp"

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"
#include "target/x86_64/features.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross::x86_64 {
namespace {

unsigned type_bits(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return 64;
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto size = module.record(*type.record).size;
        return size <= std::numeric_limits<unsigned>::max() / 8U
                   ? static_cast<unsigned>(size * 8U)
                   : 0;
    }
    if ((type.kind == hir::Type::Kind::Vector ||
         type.kind == hir::Type::Kind::Array) &&
        (type.kind != hir::Type::Kind::Vector || !type.scalable) &&
        type.element && type.lanes != 0) {
        const auto element = type_bits(module, *type.element);
        return element != 0 &&
                       type.lanes <=
                           std::numeric_limits<unsigned>::max() / element
                   ? element * type.lanes
                   : 0;
    }
    switch (type.builtin) {
    case BuiltinType::Void: return 0;
    case BuiltinType::Bool:
    case BuiltinType::I8:
    case BuiltinType::U8: return 8;
    case BuiltinType::I16:
    case BuiltinType::U16: return 16;
    case BuiltinType::I32:
    case BuiltinType::U32:
    case BuiltinType::F32: return 32;
    case BuiltinType::I64:
    case BuiltinType::U64:
    case BuiltinType::Iptr:
    case BuiltinType::Uptr:
    case BuiltinType::F64:
    case BuiltinType::Fptr:
    case BuiltinType::Label: return 64;
    case BuiltinType::F80: return 80;
    case BuiltinType::I128:
    case BuiltinType::U128:
    case BuiltinType::F128: return 128;
    }
    return 0;
}

unsigned storage_size(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return 8;
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto size = module.record(*type.record).size;
        return size <= std::numeric_limits<unsigned>::max()
                   ? static_cast<unsigned>(size)
                   : 0;
    }
    if (type.kind == hir::Type::Kind::Array) {
        if (!type.element || type.lanes == 0) return 0;
        const auto element = storage_size(module, *type.element);
        return element != 0 &&
                       type.lanes <=
                           std::numeric_limits<unsigned>::max() / element
                   ? element * type.lanes
                   : 0;
    }
    const auto bits = type_bits(module, id);
    if (bits == 80) return 16;
    return std::max(1U, (bits + 7U) / 8U);
}

unsigned natural_alignment(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Record && type.record) {
        return module.record(*type.record).alignment;
    }
    if (type.kind == hir::Type::Kind::Array && type.element) {
        return natural_alignment(module, *type.element);
    }
    const auto size = storage_size(module, id);
    if (size >= 16) return 16;
    return std::max(1U, std::min(8U, size));
}

bool void_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin == BuiltinType::Void;
}

bool integer_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin >= BuiltinType::Bool &&
           type.builtin <= BuiltinType::Uptr;
}

bool floating_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin >= BuiltinType::F32 &&
           type.builtin <= BuiltinType::Fptr;
}

bool f80_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin == BuiltinType::F80;
}

bool label_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin == BuiltinType::Label;
}

bool aggregate_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Record && type.record &&
           module.record(*type.record).complete;
}

const FunctionDecl* representative(const hir::Function& function) {
    if (function.definition) return function.definition;
    return function.declarations.empty() ? nullptr
                                         : function.declarations.back();
}

std::string attribute_string(const Attribute* attribute) {
    if (!attribute || attribute->arguments.size() != 1) return {};
    return decode_string_literal(attribute->arguments.front())
        .value_or(std::string{});
}

bool is_manual(const hir::Function& function) {
    if (function.result_location && *function.result_location != "auto") {
        return true;
    }
    return std::any_of(
        function.parameters.begin(), function.parameters.end(),
        [](const hir::Parameter& parameter) {
            return parameter.physical_location &&
                   *parameter.physical_location != "auto";
        });
}

const RegisterView* integer_view(std::string_view storage, unsigned bits) {
    for (const auto& view : register_views()) {
        if (view.register_class == RegisterClass::integer &&
            view.storage_name == storage && view.bits == bits &&
            view.bit_offset == 0) {
            return &view;
        }
    }
    return nullptr;
}

ManualBoundary direct_boundary(const RegisterView* view) {
    ManualBoundary result;
    result.kind = ManualBoundaryKind::DirectRegister;
    result.register_view = view;
    return result;
}

ManualBoundary indirect_boundary(const RegisterView* view) {
    ManualBoundary result;
    result.kind = ManualBoundaryKind::IndirectRegister;
    result.register_view = view;
    return result;
}

ManualBoundary stack_boundary(bool indirect, std::uint64_t offset,
                              bool has_offset, bool fixed,
                              LifoEndpointForm lifo =
                                  LifoEndpointForm::none) {
    ManualBoundary result;
    result.kind = indirect ? ManualBoundaryKind::IndirectStack
                           : ManualBoundaryKind::Stack;
    result.stack_offset = offset;
    result.has_stack_offset = has_offset;
    result.fixed_stack_offset = fixed;
    result.source_lifo = lifo;
    return result;
}

struct AutomaticPlacement {
    ManualBoundary value;
    ManualBoundary channel;
};

std::optional<AbiValue> abi_value(const hir::Module& module,
                                  hir::TypeId type,
                                  const AbiEntry& abi,
                                  ValueTransport transport =
                                      ValueTransport::Direct) {
    const auto& item = module.type(type);
    const auto bits = type_bits(module, type);
    if (bits == 0 || bits > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }

    AbiValue result;
    result.transport = transport;
    result.alignment_bits = static_cast<std::uint16_t>(
        std::min<unsigned>(
            natural_alignment(module, type) * 8U,
            std::numeric_limits<std::uint16_t>::max()));
    if (item.kind == hir::Type::Kind::Record && item.record) {
        const auto& record = module.record(*item.record);
        result.mode = ScalarMode::aggregate(
            static_cast<std::uint16_t>(bits));
        result.elements.reserve(record.members.size());
        result.element_offsets_bits.reserve(record.members.size());
        std::optional<std::pair<std::uint64_t, unsigned>> bit_field_unit;
        for (const auto& member : record.members) {
            if (member.bit_width && *member.bit_width == 0) continue;
            if (member.bit_width && !record.is_union) {
                const auto unit = std::pair{
                    member.offset, type_bits(module, member.type)};
                if (bit_field_unit == unit) continue;
                bit_field_unit = unit;
            } else {
                bit_field_unit.reset();
            }
            auto field = abi_value(module, member.type, abi);
            if (!field) return std::nullopt;
            field->alignment_bits = static_cast<std::uint16_t>(
                std::min<unsigned>(
                    std::max(member.alignment,
                             natural_alignment(module, member.type)) * 8U,
                    std::numeric_limits<std::uint16_t>::max()));
            result.elements.push_back(std::move(*field));
            result.element_offsets_bits.push_back(
                static_cast<std::uint32_t>(member.offset * 8U));
        }
        return result;
    }
    if (item.kind == hir::Type::Kind::Array && item.element) {
        auto element = abi_value(module, *item.element, abi);
        if (!element) return std::nullopt;
        result.mode = ScalarMode::array(static_cast<std::uint16_t>(bits));
        result.element_count = item.lanes;
        result.elements.push_back(std::move(*element));
        return result;
    }
    if (item.kind == hir::Type::Kind::Pointer ||
        label_type(module, type)) {
        result.mode = ScalarMode::pointer(
            static_cast<std::uint16_t>(abi.address_bits));
    } else if (item.kind == hir::Type::Kind::Vector && !item.scalable) {
        result.mode = ScalarMode::vector(static_cast<std::uint16_t>(bits));
    } else if (floating_type(module, type)) {
        result.mode = ScalarMode::floating(static_cast<std::uint16_t>(bits));
    } else if (integer_type(module, type)) {
        result.mode = ScalarMode::integer(static_cast<std::uint16_t>(bits));
    } else {
        return std::nullopt;
    }
    return result;
}

ManualBoundary automatic_boundary(
    const hir::Module& module, hir::TypeId type,
    std::span<const ValuePiece> pieces, bool indirect,
    std::size_t stack_base) {
    if (pieces.empty()) return {};
    if (indirect && pieces.size() != 1) return {};

    if (pieces.size() == 1 &&
        pieces.front().location.kind == LocationKind::Stack) {
        auto offset =
            static_cast<std::uint64_t>(pieces.front().location.stack_offset);
        if (offset < stack_base) return {};
        return stack_boundary(indirect, offset - stack_base, true, true);
    }

    const bool all_registers = std::all_of(
        pieces.begin(), pieces.end(), [](const ValuePiece& piece) {
            return piece.location.kind == LocationKind::Register;
        });
    if (!all_registers) return {};

    if (indirect) {
        return indirect_boundary(
            find_register_view(pieces.front().location.reg));
    }

    if (pieces.size() == 1 && !aggregate_type(module, type)) {
        const auto& piece = pieces.front();
        const auto* physical = find_register_view(piece.location.reg);
        if (!physical) return {};
        const auto* view =
            physical->register_class == RegisterClass::integer
                ? integer_view(physical->storage_name, type_bits(module, type))
                : physical;
        if (view) return direct_boundary(view);
    }

    ManualBoundary result;
    result.kind = ManualBoundaryKind::RegisterPieces;
    result.register_pieces.reserve(pieces.size());
    for (const auto& piece : pieces) {
        const auto* view = find_register_view(piece.location.reg);
        if (!view || piece.value_bits == 0) return {};
        result.register_pieces.push_back(
            {view, piece.value_bit_offset, piece.value_bits,
             piece.carrier_bits});
    }
    return result;
}

struct AutomaticLayout {
    std::vector<AutomaticPlacement> parameters;
    ManualBoundary result;
    std::string indirect_result_register;
};

AutomaticLayout automatic_layout(
    const hir::Module& module, const hir::Function& function,
    const AbiEntry& abi, AbiFeatureSet features) {
    AutomaticLayout result;
    result.parameters.resize(function.parameters.size());
    std::vector<AbiValue> values;
    values.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
        auto value = abi_value(
            module, parameter.type, abi,
            parameter.mode == ParameterMode::In
                ? ValueTransport::Direct
                : ValueTransport::ByReference);
        if (!value) return result;
        values.push_back(std::move(*value));
    }
    std::vector<AbiValue> results;
    if (!void_type(module, function.result_type)) {
        auto value = abi_value(module, function.result_type, abi);
        if (!value) return result;
        results.push_back(std::move(*value));
    }
    const auto classified =
        classify_signature(abi, values, results, features);
    if (!classified ||
        classified.layout.call.arguments.size() !=
            function.parameters.size() ||
        classified.layout.results.size() != results.size()) {
        return result;
    }
    for (std::size_t index = 0; index < result.parameters.size(); ++index) {
        const auto& parameter = function.parameters[index];
        const auto& assignment = classified.layout.call.arguments[index];
        const bool channel = parameter.mode != ParameterMode::In;
        const bool indirect = channel || assignment.indirect;
        auto boundary = automatic_boundary(
            module, parameter.type, assignment.pieces, indirect,
            classified.layout.call.argument_stack_base_size);
        if (channel) result.parameters[index].channel = std::move(boundary);
        else result.parameters[index].value = std::move(boundary);
    }
    if (!results.empty()) {
        const auto& assignment = classified.layout.results.front();
        result.result = automatic_boundary(
            module, function.result_type, assignment.pieces,
            assignment.indirect,
            classified.layout.call.argument_stack_base_size);
        result.indirect_result_register =
            assignment.indirect_result_reg;
    }
    return result;
}

ManualBoundary parsed_boundary(ManualEndpointKind kind,
                               const RegisterView* view,
                               std::uint64_t offset, bool has_offset,
                               LifoEndpointForm lifo) {
    switch (kind) {
    case ManualEndpointKind::direct_register:
        return direct_boundary(view);
    case ManualEndpointKind::indirect_register:
        return indirect_boundary(view);
    case ManualEndpointKind::stack:
        return stack_boundary(false, offset, has_offset, has_offset);
    case ManualEndpointKind::indirect_stack:
        return stack_boundary(true, offset, has_offset, has_offset);
    case ManualEndpointKind::lifo:
        return stack_boundary(false, offset, has_offset, has_offset, lifo);
    default:
        return {};
    }
}

const RegisterEntry* register_entry(const TargetInfo& target,
                                    const ManualBoundary& boundary) {
    return boundary.register_view
               ? find_register(target, boundary.register_view->name)
               : nullptr;
}

const RegisterEntry* register_entry(const TargetInfo& target,
                                    const RegisterView* view) {
    return view ? find_register(target, view->name) : nullptr;
}

template <typename Callback>
void for_each_register_view(const ManualBoundary& boundary,
                            Callback&& callback) {
    if (boundary.kind == ManualBoundaryKind::RegisterPieces) {
        for (const auto& piece : boundary.register_pieces) {
            callback(piece.register_view);
        }
    } else if (boundary.is_register()) {
        callback(boundary.register_view);
    }
}

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t& result) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return false;
    }
    result = left + right;
    return true;
}

class Planner {
public:
    Planner(const hir::Module& module, const TargetInfo& target,
            const Subtarget& subtarget, const CompilerOptions& options,
            Diagnostics& diagnostics)
        : module_(module), target_(target), subtarget_(subtarget),
          diagnostics_(diagnostics) {
        (void)options;
    }

    ManualAbiPlans run() {
        std::vector<ManualAbiPlan> entries;
        for (const auto& function : module_.functions) {
            if (!is_manual(function)) continue;
            entries.push_back(plan_function(function));
        }
        return ManualAbiPlans(std::move(entries));
    }

private:
    ManualAbiPlan plan_function(const hir::Function& function) {
        ManualAbiPlan plan;
        plan.function = function.id;
        plan.manual = true;
        plan.abi_info = find_abi(target_, function.abi);
        if (!plan.abi_info) {
            diagnostics_.error(
                function.location,
                "unsupported x86-64 manual ABI id " +
                    std::to_string(function.abi.value));
            plan.valid = false;
            return plan;
        }
        plan.stack.home_space_size =
            plan.abi_info->argument_stack_base;
        plan.callee_cleanup = cleanup(function, plan);

        auto automatic =
            automatic_layout(module_, function, *plan.abi_info,
                             subtarget_.enabled_features());
        plan.parameters.reserve(function.parameters.size());
        for (std::size_t index = 0; index < function.parameters.size();
             ++index) {
            plan.parameters.push_back(
                resolve_parameter(function, index,
                                  automatic.parameters[index], plan));
        }
        resolve_result(function, automatic, plan);
        build_stack(function, plan);
        build_x87(function, plan);
        validate(function, plan);
        return plan;
    }

    bool cleanup(const hir::Function& function, ManualAbiPlan& plan) {
        const auto* declaration = representative(function);
        const auto* attribute =
            declaration ? declaration->attribute("stack_cleanup")
                        : nullptr;
        if (!attribute) return false;
        const auto value = attribute_string(attribute);
        if (attribute->arguments.size() != 1 ||
            (value != "caller" && value != "callee")) {
            diagnostics_.error(
                attribute->location,
                "stack_cleanup requires \"caller\" or \"callee\"");
            plan.valid = false;
            return false;
        }
        return value == "callee";
    }

    ManualParameterPlan resolve_parameter(
        const hir::Function& function, std::size_t index,
        const AutomaticPlacement& automatic, ManualAbiPlan& plan) {
        const auto& parameter = function.parameters[index];
        ManualParameterPlan result;
        result.parameter_index = index;
        result.type = parameter.type;
        result.mode = parameter.mode;
        const auto spelling =
            parameter.physical_location
                ? *parameter.physical_location
                : std::string("auto");
        const auto parsed = parse_manual_endpoint(spelling);
        if (!parsed) {
            diagnostics_.error(
                parameter.location,
                std::string(manual_endpoint_issue_message(parsed.issue)) +
                    " '" + spelling + "'");
            plan.valid = false;
            return result;
        }
        const auto& endpoint = parsed.endpoint;
        const bool top_auto =
            endpoint.kind == ManualEndpointKind::automatic;
        if (parameter.mode != ParameterMode::Out) {
            if (top_auto ||
                endpoint.input_kind == ManualEndpointKind::automatic) {
                result.input =
                    parameter.mode == ParameterMode::In
                        ? automatic.value
                        : automatic.channel;
            } else {
                result.input = parsed_boundary(
                    endpoint.input_kind, endpoint.input_register_view,
                    endpoint.input_stack_offset,
                    endpoint.input_has_fixed_stack_offset,
                    endpoint.input_lifo_form);
            }
        }
        if (parameter.mode != ParameterMode::In) {
            if (top_auto ||
                endpoint.output_kind == ManualEndpointKind::automatic) {
                result.output = automatic.channel;
            } else {
                result.output = parsed_boundary(
                    endpoint.output_kind, endpoint.output_register_view,
                    endpoint.output_stack_offset,
                    endpoint.output_has_fixed_stack_offset,
                    endpoint.output_lifo_form);
            }
        }
        return result;
    }

    void resolve_result(const hir::Function& function,
                        const AutomaticLayout& automatic,
                        ManualAbiPlan& plan) {
        plan.result.type = function.result_type;
        if (void_type(module_, function.result_type)) return;
        plan.result.present = true;
        if (!function.result_location ||
            *function.result_location == "auto") {
            plan.result.output = automatic.result;
            plan.result.indirect_result_register =
                automatic.indirect_result_register;
            return;
        }
        const auto parsed =
            parse_manual_endpoint(*function.result_location);
        if (!parsed) {
            diagnostics_.error(
                function.location,
                "ordinary result requires a supported direct x86-64 "
                "register '" +
                    *function.result_location + "'");
            plan.valid = false;
            return;
        }
        if (parsed.endpoint.kind == ManualEndpointKind::stack) {
            plan.result.output = stack_boundary(
                false, parsed.endpoint.stack_offset,
                parsed.endpoint.has_fixed_stack_offset,
                parsed.endpoint.has_fixed_stack_offset);
            return;
        }
        if (parsed.endpoint.kind ==
            ManualEndpointKind::direct_register) {
            plan.result.output =
                direct_boundary(parsed.endpoint.register_view);
            return;
        }
        diagnostics_.error(
            function.location,
            "ordinary result requires a supported direct x86-64 register '" +
                *function.result_location + "'");
        plan.valid = false;
    }

    void build_stack(const hir::Function& function,
                     ManualAbiPlan& plan) {
        struct Atom {
            std::size_t parameter{};
            bool input{};
            bool output{};
            bool result{};
            bool fixed{};
            std::uint64_t requested{};
            std::uint64_t offset{};
            std::uint64_t size{};
            std::uint64_t alignment{};
        };
        struct Range {
            std::uint64_t begin{};
            std::uint64_t end{};
        };
        auto& layout = plan.stack;
        layout.input_offsets.resize(function.parameters.size());
        layout.output_offsets.resize(function.parameters.size());
        std::vector<Atom> atoms;
        for (const auto& parameter : plan.parameters) {
            const bool input_stack = parameter.input.is_stack();
            const bool output_stack = parameter.output.is_stack();
            if (!input_stack && !output_stack) continue;
            layout.has_stack = true;
            const bool indirect =
                parameter.input.kind ==
                    ManualBoundaryKind::IndirectStack ||
                parameter.output.kind ==
                    ManualBoundaryKind::IndirectStack;
            const auto size =
                indirect ? std::uint64_t{8}
                         : static_cast<std::uint64_t>(
                               storage_size(module_, parameter.type));
            const auto alignment =
                indirect ? std::uint64_t{8}
                         : static_cast<std::uint64_t>(
                               natural_alignment(module_, parameter.type));
            const bool same =
                input_stack && output_stack &&
                parameter.input.fixed_stack_offset &&
                parameter.output.fixed_stack_offset &&
                parameter.input.stack_offset ==
                    parameter.output.stack_offset;
            const bool shared_spelling =
                input_stack && output_stack &&
                function.parameters[parameter.parameter_index]
                    .physical_location &&
                function.parameters[parameter.parameter_index]
                        .physical_location->find("=>") ==
                    std::string::npos;
            const bool shared_lifo =
                input_stack && output_stack &&
                parameter.input.source_lifo !=
                    LifoEndpointForm::none &&
                parameter.input.source_lifo ==
                    parameter.output.source_lifo;
            if (input_stack && output_stack &&
                (same || shared_spelling || shared_lifo)) {
                atoms.push_back(
                    {parameter.parameter_index, true, true, false,
                     parameter.input.fixed_stack_offset,
                     parameter.input.stack_offset, 0, size, alignment});
            } else {
                if (input_stack) {
                    atoms.push_back(
                        {parameter.parameter_index, true, false, false,
                         parameter.input.fixed_stack_offset,
                         parameter.input.stack_offset, 0, size,
                         alignment});
                }
                if (output_stack) {
                    atoms.push_back(
                        {parameter.parameter_index, false, true, false,
                         parameter.output.fixed_stack_offset,
                         parameter.output.stack_offset, 0, size,
                         alignment});
                }
            }
        }
        if (plan.result.present && plan.result.output.is_stack()) {
            layout.has_stack = true;
            const bool indirect = plan.result.output.is_indirect();
            atoms.push_back(
                {0, false, false, true,
                 plan.result.output.fixed_stack_offset,
                 plan.result.output.stack_offset, 0,
                 indirect
                     ? std::uint64_t{8}
                     : static_cast<std::uint64_t>(
                           storage_size(module_, plan.result.type)),
                 indirect
                     ? std::uint64_t{8}
                     : static_cast<std::uint64_t>(
                           natural_alignment(module_, plan.result.type))});
        }

        for (const auto& atom : atoms) {
            layout.outgoing_area_alignment = std::max(
                layout.outgoing_area_alignment, atom.alignment);
        }

        std::vector<Range> occupied;
        const auto reserve = [&](Atom& atom, std::uint64_t offset) {
            std::uint64_t end = 0;
            const auto location =
                atom.result ? function.location
                            : function.parameters[atom.parameter].location;
            if (!checked_add(offset, atom.size, end)) {
                diagnostics_.error(
                    location, "manual stack endpoint range overflows");
                plan.valid = false;
                return false;
            }
            const auto collision = std::find_if(
                occupied.begin(), occupied.end(),
                [&](const Range& range) {
                    return offset < range.end &&
                           range.begin < end;
                });
            if (collision != occupied.end()) {
                diagnostics_.error(
                    location, "overlapping manual stack endpoint range");
                plan.valid = false;
                return false;
            }
            atom.offset = offset;
            occupied.push_back({offset, end});
            layout.extent = std::max(layout.extent, end);
            return true;
        };
        for (auto& atom : atoms) {
            if (!atom.fixed) continue;
            if (atom.requested % atom.alignment != 0) {
                diagnostics_.error(
                    atom.result
                        ? function.location
                        : function.parameters[atom.parameter].location,
                    "manual stack endpoint offset is not naturally aligned");
                plan.valid = false;
                continue;
            }
            (void)reserve(atom, atom.requested);
        }
        for (auto& atom : atoms) {
            if (atom.fixed) continue;
            std::uint64_t offset = 0;
            for (;;) {
                const auto remainder = offset % atom.alignment;
                if (remainder != 0) {
                    if (!checked_add(offset,
                                     atom.alignment - remainder,
                                     offset)) {
                        diagnostics_.error(
                            atom.result
                                ? function.location
                                : function.parameters[atom.parameter]
                                      .location,
                            "manual stack endpoint range overflows");
                        plan.valid = false;
                        break;
                    }
                }
                std::uint64_t end = 0;
                if (!checked_add(offset, atom.size, end)) {
                    diagnostics_.error(
                        atom.result
                            ? function.location
                            : function.parameters[atom.parameter].location,
                        "manual stack endpoint range overflows");
                    plan.valid = false;
                    break;
                }
                const auto collision = std::find_if(
                    occupied.begin(), occupied.end(),
                    [&](const Range& range) {
                        return offset < range.end &&
                               range.begin < end;
                    });
                if (collision == occupied.end()) {
                    (void)reserve(atom, offset);
                    break;
                }
                offset = collision->end;
            }
        }
        for (const auto& atom : atoms) {
            if (atom.input) {
                layout.input_offsets[atom.parameter] = atom.offset;
                plan.parameters[atom.parameter].input.stack_offset =
                    atom.offset;
                plan.parameters[atom.parameter].input.has_stack_offset =
                    true;
            }
            if (atom.output) {
                layout.output_offsets[atom.parameter] = atom.offset;
                plan.parameters[atom.parameter].output.stack_offset =
                    atom.offset;
                plan.parameters[atom.parameter].output.has_stack_offset =
                    true;
            }
            if (atom.result) {
                layout.result_offset = atom.offset;
                plan.result.output.stack_offset = atom.offset;
                plan.result.output.has_stack_offset = true;
            }
        }
        std::uint64_t extent_with_home = 0;
        if (!checked_add(layout.home_space_size, layout.extent,
                         extent_with_home)) {
            diagnostics_.error(
                function.location, "manual stack endpoint range overflows");
            plan.valid = false;
            return;
        }
        if (extent_with_home != 0) {
            const auto alignment = layout.outgoing_area_alignment;
            if (extent_with_home >
                std::numeric_limits<std::uint64_t>::max() -
                    (alignment - 1)) {
                diagnostics_.error(
                    function.location,
                    "manual stack endpoint range overflows");
                plan.valid = false;
                return;
            }
            layout.outgoing_area_size =
                (extent_with_home + alignment - 1) &
                ~(alignment - 1);
        }
    }

    std::optional<unsigned> x87_position(
        const ManualBoundary& boundary) const {
        if (!boundary.is_x87()) return std::nullopt;
        const auto name = boundary.register_view->name;
        if (name.size() != 3 || !name.starts_with("st") ||
            name[2] < '0' || name[2] > '7') {
            return std::nullopt;
        }
        return static_cast<unsigned>(name[2] - '0');
    }

    void build_x87(const hir::Function& function,
                   ManualAbiPlan& plan) {
        const auto insert = [&](auto& slots, unsigned position,
                                std::size_t parameter, bool input) {
            plan.x87.has_x87 = true;
            if (slots[position]) {
                diagnostics_.error(
                    function.parameters[parameter].location,
                    std::string("duplicate x87 ") +
                        (input ? "input" : "output") +
                        " endpoint 'st" + std::to_string(position) + "'");
                plan.valid = false;
                return;
            }
            slots[position] = parameter;
        };
        for (const auto& parameter : plan.parameters) {
            if (const auto position =
                    x87_position(parameter.input)) {
                insert(plan.x87.inputs, *position,
                       parameter.parameter_index, true);
            }
            if (const auto position =
                    x87_position(parameter.output)) {
                insert(plan.x87.outputs, *position,
                       parameter.parameter_index, false);
            }
        }
        if (plan.result.present) {
            if (const auto position =
                    x87_position(plan.result.output)) {
                plan.x87.has_x87 = true;
                if (plan.x87.outputs[*position]) {
                    diagnostics_.error(
                        function.location,
                        "ordinary result overlaps x87 output endpoint 'st" +
                            std::to_string(*position) + "'");
                    plan.valid = false;
                } else {
                    plan.x87.result = *position;
                }
            }
        }
        const auto depth = [&](const auto& slots,
                               std::optional<unsigned> result,
                               bool input) {
            int maximum = -1;
            for (unsigned index = 0; index < slots.size(); ++index) {
                if (slots[index]) maximum = static_cast<int>(index);
            }
            if (result) {
                maximum = std::max(maximum,
                                   static_cast<int>(*result));
            }
            for (int index = 0; index <= maximum; ++index) {
                const auto slot = static_cast<std::size_t>(index);
                if (!slots[slot] &&
                    !(result &&
                      *result == static_cast<unsigned>(index))) {
                    diagnostics_.error(
                        function.location,
                        std::string("x87 ") +
                            (input ? "input" : "output") +
                            " endpoints must form a dense prefix from st0");
                    plan.valid = false;
                    break;
                }
            }
            return maximum < 0
                       ? 0U
                       : static_cast<unsigned>(maximum + 1);
        };
        plan.x87.input_depth =
            depth(plan.x87.inputs, std::nullopt, true);
        plan.x87.output_depth =
            depth(plan.x87.outputs, plan.x87.result, false);
    }

    bool feature_enabled(RegisterFeature feature) const {
        switch (feature) {
        case RegisterFeature::base: return true;
        case RegisterFeature::avx:
            return subtarget_.has_feature(Feature::Avx);
        case RegisterFeature::avx512f:
            return subtarget_.has_feature(Feature::Avx512f);
        }
        return false;
    }

    bool valid_direct_type(hir::TypeId type,
                           const RegisterView& view) const {
        // Opmask registers are exposed for raw instruction programming. A
        // managed endpoint requires an ABI model bank and native boundary
        // transport before it can become a stable interface location.
        if (view.register_class == RegisterClass::mask) return false;
        if (view.register_class == RegisterClass::x87) {
            return floating_type(module_, type);
        }
        if (view.register_class == RegisterClass::simd) {
            const auto bits = type_bits(module_, type);
            const auto& item = module_.type(type);
            if (item.kind == hir::Type::Kind::Vector) {
                return !item.scalable && bits == view.bits;
            }
            if (aggregate_type(module_, type)) {
                return (bits == 32 || bits == 64 || bits == 128) &&
                       bits <= view.bits;
            }
            const bool bit_transport =
                bits == 32 || bits == 64
                    ? integer_type(module_, type) ||
                          item.kind == hir::Type::Kind::Pointer ||
                          label_type(module_, type)
                    : false;
            return ((floating_type(module_, type) &&
                     !f80_type(module_, type)) ||
                    bit_transport) &&
                   bits <= view.bits;
        }
        const auto& item = module_.type(type);
        if (aggregate_type(module_, type)) {
            const auto bits = type_bits(module_, type);
            return view.bit_offset == 0 && bits != 0 && bits <= view.bits &&
                   bits <= 64;
        }
        return view.bit_offset == 0 &&
               (integer_type(module_, type) ||
                item.kind == hir::Type::Kind::Pointer ||
                label_type(module_, type)) &&
               type_bits(module_, type) == view.bits;
    }

    bool valid_register_piece(const ManualRegisterPiece& piece,
                              hir::TypeId type) const {
        if (!piece.register_view || piece.value_bits == 0 ||
            piece.value_bit_offset % 8U != 0 ||
            piece.value_bits > piece.register_view->bits ||
            static_cast<unsigned>(piece.value_bit_offset) +
                    piece.value_bits >
                type_bits(module_, type)) {
            return false;
        }
        return piece.register_view->register_class ==
                   RegisterClass::integer ||
               piece.register_view->register_class == RegisterClass::simd;
    }

    bool abi_clobbers(const ManualAbiPlan& plan,
                      std::string_view storage) const {
        if (plan.abi_info &&
            std::find(plan.abi_info->call_clobbers.begin(),
                      plan.abi_info->call_clobbers.end(),
                      storage) != plan.abi_info->call_clobbers.end()) {
            return true;
        }
        if (plan.function.value >= module_.functions.size()) return false;
        const auto& function = module_.function(plan.function);
        const auto* sought = find_register(target_, storage);
        return sought && std::any_of(
            function.clobbers.begin(), function.clobbers.end(),
            [&](const std::string& name) {
                const auto* declared = find_register(target_, name);
                return declared && declared->storage == sought->storage;
            });
    }

    bool validate_boundary(const hir::Function& function,
                           const hir::Parameter& parameter,
                           const ManualBoundary& boundary, bool output,
                           ManualAbiPlan& plan) {
        if (boundary.kind == ManualBoundaryKind::None) {
            diagnostics_.error(
                parameter.location,
                "unsupported x86-64 register endpoint '" +
                    parameter.physical_location.value_or("auto") + "'");
            plan.valid = false;
            return false;
        }
        if (!boundary.is_register()) return true;
        if (boundary.kind == ManualBoundaryKind::RegisterPieces) {
            bool valid = !boundary.register_pieces.empty();
            for (const auto& piece : boundary.register_pieces) {
                const auto* entry = register_entry(target_, piece.register_view);
                if (!entry || !valid_register_piece(piece, parameter.type)) {
                    valid = false;
                    continue;
                }
                if (!function.naked &&
                    (entry->storage == "rsp" || entry->storage == "rbp")) {
                    diagnostics_.error(
                        parameter.location,
                        "manual endpoint cannot use compiler-owned register '" +
                            std::string(entry->storage) + "'");
                    valid = false;
                }
                if (!feature_enabled(piece.register_view->required_feature)) {
                    diagnostics_.error(
                        parameter.location,
                        "automatic register piece '" +
                            std::string(piece.register_view->name) +
                            "' requires an unavailable target feature");
                    valid = false;
                }
                if (output && !abi_clobbers(plan, entry->storage)) {
                    diagnostics_.error(
                        parameter.location,
                        "manual output register '" +
                            std::string(entry->storage) +
                            "' is preserved by the selected ABI");
                    valid = false;
                }
            }
            if (!valid) {
                diagnostics_.error(
                    parameter.location,
                    "automatic register pieces do not match parameter type '" +
                        hir::type_name(module_, parameter.type) + "'");
                plan.valid = false;
            }
            return valid;
        }
        const auto* entry = register_entry(target_, boundary);
        if (!boundary.register_view || !entry) {
            diagnostics_.error(
                parameter.location,
                "unsupported x86-64 register endpoint '" +
                    parameter.physical_location.value_or("auto") + "'");
            plan.valid = false;
            return false;
        }
        if (!function.naked &&
            (entry->storage == "rsp" || entry->storage == "rbp")) {
            diagnostics_.error(
                parameter.location,
                "manual endpoint cannot use compiler-owned register '" +
                    std::string(entry->storage) + "'");
            plan.valid = false;
            return false;
        }
        if (!feature_enabled(boundary.register_view->required_feature)) {
            diagnostics_.error(
                parameter.location,
                "register endpoint '" +
                    std::string(boundary.register_view->name) +
                    "' requires target feature '" +
                    std::string(
                        boundary.register_view->required_feature ==
                                RegisterFeature::avx
                            ? "avx"
                            : "avx512f") +
                    "'");
            plan.valid = false;
            return false;
        }
        if (!boundary.is_indirect() &&
            !valid_direct_type(parameter.type,
                               *boundary.register_view)) {
            diagnostics_.error(
                parameter.location,
                "register endpoint '" +
                    std::string(boundary.register_view->name) +
                    "' does not match parameter type '" +
                    hir::type_name(module_, parameter.type) + "'");
            plan.valid = false;
            return false;
        }
        if (output && !boundary.is_indirect() &&
            !abi_clobbers(plan, entry->storage)) {
            diagnostics_.error(
                parameter.location,
                "manual output register '" +
                    std::string(entry->storage) +
                    "' is preserved by the selected ABI");
            plan.valid = false;
            return false;
        }
        return true;
    }

    void validate(const hir::Function& function,
                  ManualAbiPlan& plan) {
        if (plan.callee_cleanup && plan.stack.has_stack) {
            if (plan.stack.outgoing_area_size > 65535) {
                diagnostics_.error(
                    function.location,
                    "callee stack cleanup exceeds the x86-64 ret immediate");
                plan.valid = false;
            }
            for (const auto& parameter : plan.parameters) {
                if (parameter.output.is_stack()) {
                    diagnostics_.error(
                        function.parameters[parameter.parameter_index]
                            .location,
                        "callee stack cleanup forbids stack output endpoints");
                    plan.valid = false;
                }
            }
            if (plan.result.output.is_stack()) {
                diagnostics_.error(
                    function.location,
                    "callee stack cleanup forbids a stack ordinary result");
                plan.valid = false;
            }
            if (plan.stack.outgoing_area_size > 65535) {
                diagnostics_.error(
                    function.location,
                    "callee stack cleanup area exceeds x86-64 ret immediate");
                plan.valid = false;
            }
        }

        std::unordered_set<std::string_view> input_storage;
        std::unordered_set<std::string_view> output_storage;
        for (const auto& parameter_plan : plan.parameters) {
            const auto& parameter =
                function.parameters[parameter_plan.parameter_index];
            const bool input_valid =
                parameter.mode == ParameterMode::Out ||
                validate_boundary(function, parameter,
                                  parameter_plan.input, false, plan);
            const bool output_valid =
                parameter.mode == ParameterMode::In ||
                validate_boundary(function, parameter,
                                  parameter_plan.output, true, plan);
            if (input_valid) {
                for_each_register_view(
                    parameter_plan.input,
                    [&](const RegisterView* view) {
                        const auto* input = register_entry(target_, view);
                        if (input &&
                            !input_storage.insert(input->storage).second) {
                            diagnostics_.error(
                                parameter.location,
                                "overlapping manual input register '" +
                                    std::string(input->storage) + "'");
                            plan.valid = false;
                        }
                    });
            }
            if (output_valid) {
                for_each_register_view(
                    parameter_plan.output,
                    [&](const RegisterView* view) {
                        const auto* output = register_entry(target_, view);
                        if (!output) return;
                        if (parameter_plan.output.is_indirect()) {
                            const auto* input = register_entry(
                                target_, parameter_plan.input);
                            const bool shared =
                                input_valid && input &&
                                parameter_plan.input.is_indirect() &&
                                input->storage == output->storage;
                            if (!shared &&
                                !input_storage.insert(output->storage)
                                     .second) {
                                diagnostics_.error(
                                    parameter.location,
                                    "overlapping manual input register '" +
                                        std::string(output->storage) + "'");
                                plan.valid = false;
                            }
                        } else if (!output_storage.insert(output->storage)
                                        .second) {
                            diagnostics_.error(
                                parameter.location,
                                "overlapping manual output register '" +
                                    std::string(output->storage) + "'");
                            plan.valid = false;
                        }
                    });
            }
        }

        if (!plan.result.present) return;
        const bool explicit_result =
            function.result_location &&
            *function.result_location != "auto";
        if (plan.result.output.is_stack()) {
            return;
        }
        if (plan.result.output.is_indirect()) {
            const auto* entry = register_entry(target_, plan.result.output);
            const auto* view = plan.result.output.register_view;
            if (explicit_result || !entry || !view ||
                view->register_class != RegisterClass::integer ||
                view->bits < 64 || view->bit_offset != 0) {
                diagnostics_.error(
                    function.location,
                    "automatic indirect result requires a supported x86-64 "
                    "pointer register");
                plan.valid = false;
                return;
            }
            if (!function.naked &&
                (entry->storage == "rsp" || entry->storage == "rbp")) {
                diagnostics_.error(
                    function.location,
                    "automatic indirect result cannot use compiler-owned "
                    "register '" + std::string(entry->storage) + "'");
                plan.valid = false;
            } else if (!input_storage.insert(entry->storage).second) {
                diagnostics_.error(
                    function.location,
                    "automatic indirect result overlaps manual input "
                    "register '" + std::string(entry->storage) + "'");
                plan.valid = false;
            }
            return;
        }
        if (plan.result.output.kind ==
            ManualBoundaryKind::RegisterPieces) {
            if (explicit_result ||
                plan.result.output.register_pieces.empty()) {
                diagnostics_.error(
                    function.location,
                    "ordinary result has invalid automatic register pieces");
                plan.valid = false;
                return;
            }
            for (const auto& piece :
                 plan.result.output.register_pieces) {
                const auto* entry = register_entry(target_, piece.register_view);
                if (!entry || !valid_register_piece(piece, plan.result.type) ||
                    !feature_enabled(piece.register_view->required_feature)) {
                    diagnostics_.error(
                        function.location,
                        "ordinary result has an unsupported automatic "
                        "register piece");
                    plan.valid = false;
                    continue;
                }
                if (!abi_clobbers(plan, entry->storage)) {
                    diagnostics_.error(
                        function.location,
                        "manual result register '" +
                            std::string(entry->storage) +
                            "' is preserved by the selected ABI");
                    plan.valid = false;
                }
                if (!output_storage.insert(entry->storage).second) {
                    diagnostics_.error(
                        function.location,
                        "ordinary result overlaps manual output register '" +
                            std::string(entry->storage) + "'");
                    plan.valid = false;
                }
            }
            return;
        }
        const auto* entry =
            register_entry(target_, plan.result.output);
        const auto* view = plan.result.output.register_view;
        const auto result_name =
            explicit_result ? *function.result_location
                            : std::string("auto");
        if (plan.result.output.kind !=
                ManualBoundaryKind::DirectRegister ||
            !entry || !view) {
            diagnostics_.error(
                function.location,
                "ordinary result requires a supported direct x86-64 "
                "register '" +
                    result_name + "'");
            plan.valid = false;
            return;
        }
        if (!function.naked &&
            (entry->storage == "rsp" || entry->storage == "rbp")) {
            diagnostics_.error(
                function.location,
                "ordinary result cannot use compiler-owned register '" +
                    std::string(entry->storage) + "'");
            plan.valid = false;
        } else if (!feature_enabled(view->required_feature)) {
            diagnostics_.error(
                function.location,
                "result register '" + result_name +
                    "' requires an unavailable target feature");
            plan.valid = false;
        } else if (!valid_direct_type(plan.result.type, *view)) {
            diagnostics_.error(
                function.location,
                "result register '" + result_name +
                    "' does not match result type '" +
                    hir::type_name(module_, plan.result.type) + "'");
            plan.valid = false;
        } else {
            if (!abi_clobbers(plan, entry->storage)) {
                diagnostics_.error(
                    function.location,
                    "manual result register '" +
                        std::string(entry->storage) +
                        "' is preserved by the selected ABI");
                plan.valid = false;
            }
            if (!output_storage.insert(entry->storage).second) {
                diagnostics_.error(
                    function.location,
                    "ordinary result overlaps manual output register '" +
                        std::string(entry->storage) + "'");
                plan.valid = false;
            }
        }
    }

    const hir::Module& module_;
    const TargetInfo& target_;
    const Subtarget& subtarget_;
    Diagnostics& diagnostics_;
};

} // namespace

const ManualAbiPlan* ManualAbiPlans::find(
    hir::FunctionId function) const {
    const auto found = std::find_if(
        entries_.begin(), entries_.end(),
        [function](const ManualAbiPlan& plan) {
            return plan.function == function;
        });
    return found == entries_.end() ? nullptr : &*found;
}

ManualAbiPlans build_manual_abi_plans(
    const hir::Module& module, const TargetInfo& target,
    const Subtarget& subtarget, const CompilerOptions& options,
    Diagnostics& diagnostics) {
    return Planner(module, target, subtarget, options, diagnostics).run();
}

} // namespace cross::x86_64

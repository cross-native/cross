// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/hir.hpp"

#include "frontend/semantic.hpp"
#include "model/model.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cross::hir {
namespace {

std::string decode_attribute_string(const Attribute* attribute) {
    if (!attribute || attribute->arguments.size() != 1) return {};
    return decode_string_literal(attribute->arguments.front()).value_or(std::string{});
}

std::string sanitize(std::string_view text) {
    std::string result;
    for (const char ch : text) {
        const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9');
        result.push_back(safe ? ch : '_');
    }
    return result;
}

std::uint64_t stable_hash(std::string_view text) {
    std::uint64_t value = 1469598103934665603ULL;
    for (const char ch : text) {
        value ^= static_cast<unsigned char>(ch);
        value *= 1099511628211ULL;
    }
    return value;
}

std::string entity_key(Linkage linkage, std::string_view source_unit,
                       std::string_view name) {
    if (linkage != Linkage::Static) return std::string(name);
    return std::string(source_unit) + '\n' + std::string(name);
}

std::string_view parameter_mode_name(ParameterMode mode) {
    switch (mode) {
    case ParameterMode::In: return "in";
    case ParameterMode::Out: return "out";
    case ParameterMode::InOut: return "inout";
    }
    return {};
}

std::string resolved_link_name(const FunctionDecl& function,
                               const CompilerOptions& options) {
    if (const auto exact = decode_attribute_string(function.attribute("link_name"));
        !exact.empty()) return exact;
    if (function.linkage == Linkage::Global ||
        (function.linkage != Linkage::Static && !function.definition())) {
        std::vector<ManglingParameter> parameters;
        parameters.reserve(function.parameters.size());
        for (const auto& parameter : function.parameters) {
            parameters.push_back(
                {.spelling = canonical_type_name(parameter.type),
                 .mode = std::string(parameter_mode_name(parameter.mode))});
        }
        return encode_model_link_name(
            {.qualified_name = function.name,
             .kind = "function",
             .result = canonical_type_name(function.return_type),
             .parameters = parameters,
             .variadic = function.variadic},
            false, options.mangling);
    }
    if (function.linkage == Linkage::Static) {
        return "__cross_static_" + std::to_string(stable_hash(function.source_unit)) + '_' +
               sanitize(function.name);
    }
    return "__cross_group_" + sanitize(function.name);
}

const Attribute* object_attribute(const ObjectDecl& object, std::string_view name) {
    for (const auto& attribute : object.attributes) {
        if (attribute.name == name) return &attribute;
    }
    return nullptr;
}

std::string resolved_link_name(const ObjectDecl& object,
                               const CompilerOptions& options) {
    if (const auto exact = decode_attribute_string(object_attribute(object, "link_name"));
        !exact.empty()) return exact;
    if (object.linkage == Linkage::Global ||
        (object.linkage != Linkage::Static && !object.initializer)) {
        return encode_model_link_name(
            {.qualified_name = object.name,
             .kind = "object",
             .result = canonical_type_name(object.type),
             .parameters = {},
             .variadic = false},
            false, options.mangling);
    }
    if (object.linkage == Linkage::Static) {
        return "__cross_static_" + std::to_string(stable_hash(object.source_unit)) + '_' +
               sanitize(object.name);
    }
    return "__cross_group_" + sanitize(object.name);
}

std::vector<std::string> decoded_clobbers(const FunctionDecl& function) {
    std::vector<std::string> result;
    for (const auto& attribute : function.attributes) {
        if (attribute.name != "clobber") continue;
        for (const auto& argument : attribute.arguments) {
            if (const auto value = decode_string_literal(argument)) result.push_back(*value);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

TypePtr variadic_state_type(std::string_view spelling) {
    unsigned pointers{};
    while (spelling.ends_with('*')) {
        ++pointers;
        spelling.remove_suffix(1);
    }
    static constexpr std::pair<std::string_view, BuiltinType> builtins[] = {
        {"void", BuiltinType::Void}, {"bool", BuiltinType::Bool},
        {"i8", BuiltinType::I8}, {"u8", BuiltinType::U8},
        {"i16", BuiltinType::I16}, {"u16", BuiltinType::U16},
        {"i32", BuiltinType::I32}, {"u32", BuiltinType::U32},
        {"i64", BuiltinType::I64}, {"u64", BuiltinType::U64},
        {"i128", BuiltinType::I128}, {"u128", BuiltinType::U128},
        {"iptr", BuiltinType::Iptr}, {"uptr", BuiltinType::Uptr},
        {"f32", BuiltinType::F32}, {"f64", BuiltinType::F64},
        {"f80", BuiltinType::F80}, {"f128", BuiltinType::F128},
        {"fptr", BuiltinType::Fptr}, {"label", BuiltinType::Label},
    };
    const auto found = std::find_if(
        std::begin(builtins), std::end(builtins),
        [&](const auto& item) { return item.first == spelling; });
    if (found == std::end(builtins)) return {};
    auto result = builtin_type(found->second);
    while (pointers-- != 0) result = pointer_type(result);
    return result;
}

bool source_identifier(std::string_view spelling) {
    if (spelling.empty()) return false;
    const auto initial = [](unsigned char ch) {
        return (ch >= 'A' && ch <= 'Z') ||
               (ch >= 'a' && ch <= 'z') || ch == '_';
    };
    const auto continuation = [&](unsigned char ch) {
        return initial(ch) || (ch >= '0' && ch <= '9');
    };
    return initial(static_cast<unsigned char>(spelling.front())) &&
           std::all_of(spelling.begin() + 1, spelling.end(), continuation);
}

std::string source_namespace(std::string_view name) {
    const auto separator = name.rfind("::");
    return separator == std::string_view::npos
               ? std::string{}
               : std::string(name.substr(0, separator));
}

class Builder {
public:
    Builder(Program& program, const CompilerOptions& options,
            const TargetInfo& target, Diagnostics& diagnostics)
        : program_(program), options_(options), target_(target), diagnostics_(diagnostics) {
        const auto* abi = find_abi(target_, options_.abi, options_.target);
        if (abi) module_.default_abi = abi->id;
        for (const auto& entry : model_registry().abis()) {
            if (entry.architecture != target_.architecture) continue;
            module_.abi_names.emplace(entry.canonical_name, entry.id);
            for (const auto& alias : entry.aliases)
                module_.abi_names.emplace(alias, entry.id);
        }
        module_.abi_names["default"] = module_.default_abi;
        module_.address_bits = abi && abi->address_bits != 0
                                   ? abi->address_bits
                                   : 64U;
        address_bytes_ = std::max(1U, (module_.address_bits + 7U) / 8U);
        for (unsigned kind = static_cast<unsigned>(BuiltinType::Void);
             kind <= static_cast<unsigned>(BuiltinType::Label); ++kind) {
            (void)intern_type(builtin_type(static_cast<BuiltinType>(kind)));
        }
    }

    Module run() {
        collect_record_shells();
        finish_records();
        collect_functions();
        collect_objects();
        finish_functions();
        finish_global_labels();
        finish_objects();
        finish_patch_sink_indices();
        diagnose_symbol_collisions();
        return std::move(module_);
    }

private:
    TypeId intern_type(const TypePtr& source) {
        return module_.intern_type(source);
    }

    static bool power_of_two(unsigned value) {
        return value != 0 && (value & (value - 1)) == 0;
    }

    static std::optional<std::uint64_t> align_up(std::uint64_t value,
                                                  unsigned alignment) {
        if (!power_of_two(alignment)) return std::nullopt;
        const auto mask = static_cast<std::uint64_t>(alignment - 1);
        if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
            return std::nullopt;
        }
        return (value + mask) & ~mask;
    }

    void finish_patch_sink_designator(Expr& expression,
                                      std::string_view source_namespace) {
        if (expression.kind == Expr::Kind::Parenthesized &&
            expression.left) {
            finish_patch_sink_designator(*expression.left,
                                         source_namespace);
            return;
        }
        if (expression.kind != Expr::Kind::Binary || !expression.left) {
            return;
        }
        finish_patch_sink_designator(*expression.left, source_namespace);
        if (expression.text != "index" || !expression.right ||
            expression.right->evaluated_integer) {
            return;
        }
        const auto layout = [&](const TypePtr& source)
            -> std::optional<std::pair<std::uint64_t, unsigned>> {
            const auto id = intern_type(source);
            const auto& type = module_.type(id);
            if (type.kind == Type::Kind::Record &&
                (!type.record ||
                 !layout_record(*type.record, expression.location))) {
                return std::nullopt;
            }
            const auto result = storage_layout(id, expression.location);
            return result.first == 0 ? std::nullopt
                                     : std::optional(result);
        };
        const LayoutQuery size_of = [&](const TypePtr& source)
            -> std::optional<std::uint64_t> {
            const auto result = layout(source);
            return result ? std::optional(result->first) : std::nullopt;
        };
        const LayoutQuery align_of = [&](const TypePtr& source)
            -> std::optional<std::uint64_t> {
            const auto result = layout(source);
            return result ? std::optional<std::uint64_t>(result->second)
                          : std::nullopt;
        };
        expression.right->evaluated_integer =
            evaluate_target_integer_constant(
                program_, *expression.right, diagnostics_, size_of, align_of,
                source_namespace);
    }

    void finish_patch_sink_expression(Expr& expression,
                                      std::string_view source_namespace) {
        if (expression.kind == Expr::Kind::Call && expression.left &&
            expression.left->kind == Expr::Kind::Name &&
            expression.left->text == "$::patch" &&
            expression.arguments.size() == 2) {
            finish_patch_sink_designator(*expression.arguments[1],
                                         source_namespace);
        }
        if (expression.left) {
            finish_patch_sink_expression(*expression.left,
                                         source_namespace);
        }
        if (expression.right) {
            finish_patch_sink_expression(*expression.right,
                                         source_namespace);
        }
        if (expression.third) {
            finish_patch_sink_expression(*expression.third,
                                         source_namespace);
        }
        for (auto& argument : expression.arguments) {
            finish_patch_sink_expression(*argument, source_namespace);
        }
        for (auto& argument : expression.generic_arguments) {
            if (argument.value) {
                finish_patch_sink_expression(*argument.value,
                                             source_namespace);
            }
        }
        for (auto& entry : expression.initializer_entries) {
            for (auto& designator : entry.designators) {
                if (designator.index) {
                    finish_patch_sink_expression(*designator.index,
                                                 source_namespace);
                }
            }
            if (entry.value) {
                finish_patch_sink_expression(*entry.value,
                                             source_namespace);
            }
        }
    }

    void finish_patch_sink_statement(Statement& statement,
                                     std::string_view source_namespace) {
        for (auto& child : statement.statements) {
            finish_patch_sink_statement(*child, source_namespace);
        }
        if (statement.declaration) {
            if (statement.declaration->dynamic_array_bound) {
                finish_patch_sink_expression(
                    *statement.declaration->dynamic_array_bound,
                    source_namespace);
            }
            if (statement.declaration->initializer) {
                finish_patch_sink_expression(
                    *statement.declaration->initializer, source_namespace);
            }
        }
        if (statement.expression) {
            finish_patch_sink_expression(*statement.expression,
                                         source_namespace);
        }
        if (statement.condition) {
            finish_patch_sink_expression(*statement.condition,
                                         source_namespace);
        }
        if (statement.increment) {
            finish_patch_sink_expression(*statement.increment,
                                         source_namespace);
        }
        if (statement.first) {
            finish_patch_sink_statement(*statement.first, source_namespace);
        }
        if (statement.second) {
            finish_patch_sink_statement(*statement.second, source_namespace);
        }
    }

    void finish_patch_sink_indices() {
        for (auto& function : program_.functions) {
            if (function->body) {
                finish_patch_sink_statement(*function->body,
                                            function->source_namespace);
            }
        }
    }

    unsigned parse_alignment(const std::vector<Attribute>& attributes,
                             std::string_view subject,
                             std::string_view source_namespace) {
        unsigned result = 1;
        for (const auto& attribute : attributes) {
            if (attribute.name != "aligned") continue;
            const auto layout = [&](const TypePtr& source)
                -> std::optional<std::pair<std::uint64_t, unsigned>> {
                const auto id = intern_type(source);
                const auto& type = module_.type(id);
                if (type.kind == Type::Kind::Record &&
                    (!type.record ||
                     !layout_record(*type.record, attribute.location))) {
                    return std::nullopt;
                }
                const auto resolved = storage_layout(id, attribute.location);
                return resolved.first == 0 ? std::nullopt
                                           : std::optional(resolved);
            };
            const LayoutQuery size_of = [&](const TypePtr& source)
                -> std::optional<std::uint64_t> {
                const auto resolved = layout(source);
                return resolved ? std::optional(resolved->first) : std::nullopt;
            };
            const LayoutQuery align_of = [&](const TypePtr& source)
                -> std::optional<std::uint64_t> {
                const auto resolved = layout(source);
                return resolved ? std::optional<std::uint64_t>(resolved->second)
                                : std::nullopt;
            };
            const auto value = evaluate_alignment_attribute(
                program_, attribute, diagnostics_, size_of, align_of,
                subject, source_namespace);
            if (value) result = std::max(result, *value);
        }
        return result;
    }

    bool parse_packed(const std::vector<Attribute>& attributes,
                      std::string_view subject) {
        bool result = false;
        for (const auto& attribute : attributes) {
            if (attribute.name == "packed") {
                result = true;
                if (!attribute.arguments.empty()) {
                    diagnostics_.error(
                        attribute.location,
                        "packed on " + std::string(subject) +
                            " does not take arguments");
                }
            } else if (attribute.name != "aligned") {
                diagnostics_.error(
                    attribute.location,
                    "attribute '" + attribute.name + "' is not valid on " +
                        std::string(subject));
            }
        }
        return result;
    }

    void collect_record_shells() {
        for (const auto& declaration : program_.records) {
            auto found = module_.record_ids.find(declaration.name);
            RecordId id;
            if (found == module_.record_ids.end()) {
                id = {static_cast<std::uint32_t>(module_.records.size())};
                module_.record_ids.emplace(declaration.name, id);
                Record record;
                record.id = id;
                record.location = declaration.location;
                record.source_name = declaration.name;
                record.is_union = declaration.is_union;
                module_.records.push_back(std::move(record));
            } else {
                id = found->second;
                if (module_.record(id).is_union != declaration.is_union) {
                    diagnostics_.error(
                        declaration.location,
                        "record '" + declaration.name +
                            "' was redeclared with the other record kind");
                }
            }
            auto& record = module_.record(id);
            record.declarations.push_back(&declaration);
            if (declaration.complete) {
                if (record.definition) {
                    diagnostics_.error(
                        declaration.location,
                        "duplicate definition of record '" +
                            declaration.name + "'");
                } else {
                    record.definition = &declaration;
                }
            } else if (!declaration.attributes.empty()) {
                for (const auto& attribute : declaration.attributes) {
                    diagnostics_.error(
                        attribute.location,
                        "layout attribute '" + attribute.name +
                            "' requires a complete record definition");
                }
            }
        }
    }

    std::pair<std::uint64_t, unsigned> storage_layout(TypeId id,
                                                       SourceLocation location) {
        const auto type = module_.type(id);
        if (type.kind == Type::Kind::Pointer) {
            return {address_bytes_, std::max(
                                       1U, std::min(address_bytes_,
                                                    target_.data_layout
                                                        .natural_alignment_limit))};
        }
        if (type.kind == Type::Kind::Record) {
            if (!type.record || !layout_record(*type.record, location)) {
                return {0, 1};
            }
            const auto& record = module_.record(*type.record);
            return {record.size, record.alignment};
        }
        if (type.kind == Type::Kind::Array) {
            if (!type.element || type.lanes == 0) {
                diagnostics_.error(location,
                                   "record member cannot have variable-length or incomplete array type");
                return {0, 1};
            }
            const auto [element_size, element_alignment] =
                storage_layout(*type.element, location);
            if (element_size == 0 ||
                type.lanes > std::numeric_limits<std::uint64_t>::max() /
                                 element_size) {
                diagnostics_.error(location,
                                   "record member array size overflows target storage");
                return {0, element_alignment};
            }
            return {element_size * type.lanes, element_alignment};
        }
        if (type.kind == Type::Kind::Vector) {
            if (type.scalable || !type.element || type.lanes == 0) {
                diagnostics_.error(location,
                                   "record member cannot have scalable or incomplete vector type");
                return {0, 1};
            }
            const auto [element_size, unused] =
                storage_layout(*type.element, location);
            (void)unused;
            if (element_size == 0 ||
                type.lanes > std::numeric_limits<std::uint64_t>::max() /
                                 element_size) {
                diagnostics_.error(location,
                                   "record member vector size overflows target storage");
                return {0, 1};
            }
            const auto size = element_size * type.lanes;
            return {size, std::max(
                              1U, std::min(
                                      static_cast<unsigned>(std::min<
                                          std::uint64_t>(
                                          size,
                                          std::numeric_limits<unsigned>::max())),
                                      target_.data_layout
                                          .natural_alignment_limit))};
        }
        if (type.kind != Type::Kind::Builtin ||
            type.builtin == BuiltinType::Void) {
            diagnostics_.error(location,
                               "record member has an incomplete or non-object type");
            return {0, 1};
        }
        std::uint64_t size{};
        switch (type.builtin) {
        case BuiltinType::Bool:
        case BuiltinType::I8:
        case BuiltinType::U8: size = 1; break;
        case BuiltinType::I16:
        case BuiltinType::U16: size = 2; break;
        case BuiltinType::I32:
        case BuiltinType::U32:
        case BuiltinType::F32: size = 4; break;
        case BuiltinType::I64:
        case BuiltinType::U64:
        case BuiltinType::F64: size = 8; break;
        case BuiltinType::Iptr:
        case BuiltinType::Uptr:
        case BuiltinType::Fptr:
        case BuiltinType::Label: size = address_bytes_; break;
        case BuiltinType::I128:
        case BuiltinType::U128:
        case BuiltinType::F128: size = 16; break;
        case BuiltinType::F80:
            size = target_.data_layout.f80_storage_bytes;
            break;
        case BuiltinType::Void: break;
        }
        const auto alignment = type.builtin == BuiltinType::F80
                                   ? target_.data_layout.f80_alignment
                                   : std::max(
                                         1U, std::min(
                                                 static_cast<unsigned>(size),
                                                 target_.data_layout
                                                     .natural_alignment_limit));
        return {size, alignment};
    }

    bool set_bit_field_width(RecordMember& member,
                             const Expr::IntegerConstant& width,
                             SourceLocation location) {
        const auto width_type = width.type;
        const bool signed_width =
            width_type == BuiltinType::I8 ||
            width_type == BuiltinType::I16 ||
            width_type == BuiltinType::I32 ||
            width_type == BuiltinType::I64 ||
            width_type == BuiltinType::I128 ||
            width_type == BuiltinType::Iptr;
        auto width_bits = type_bits(builtin_type(width_type));
        if (width_type == BuiltinType::Iptr ||
            width_type == BuiltinType::Uptr) {
            width_bits = module_.address_bits;
        }
        const bool negative =
            signed_width && width_bits != 0 &&
            (shift_right(width.value, width_bits - 1).low & 1U) != 0;
        const auto storage_bits = static_cast<unsigned>(
            storage_layout(member.type, location).first * 8U);
        if (negative || width.value.high != 0) {
            diagnostics_.error(location,
                               "bit-field width must be nonnegative");
            return false;
        }
        if (width.value.low > storage_bits) {
            diagnostics_.error(location,
                               "bit-field width exceeds its base type");
            return false;
        }
        member.bit_width = static_cast<unsigned>(width.value.low);
        if (*member.bit_width == 0 && !member.name.empty()) {
            diagnostics_.error(location,
                               "a zero-width bit-field must be unnamed");
            return false;
        }
        return true;
    }

    bool resolve_bit_field_width(RecordMember& member,
                                 std::string_view source_namespace) {
        if (!member.pending_bit_width) return member.bit_width.has_value();
        const auto* expression = member.pending_bit_width;
        member.pending_bit_width = nullptr;
        const auto layout = [&](const TypePtr& source)
            -> std::optional<std::pair<std::uint64_t, unsigned>> {
            const auto id = intern_type(source);
            const auto& type = module_.type(id);
            if (type.kind == Type::Kind::Record &&
                (!type.record ||
                 !layout_record(*type.record, expression->location))) {
                return std::nullopt;
            }
            const auto result = storage_layout(id, expression->location);
            return result.first == 0 ? std::nullopt
                                     : std::optional(result);
        };
        const LayoutQuery size_of = [&](const TypePtr& source)
            -> std::optional<std::uint64_t> {
            const auto result = layout(source);
            return result ? std::optional(result->first) : std::nullopt;
        };
        const LayoutQuery align_of = [&](const TypePtr& source)
            -> std::optional<std::uint64_t> {
            const auto result = layout(source);
            return result ? std::optional<std::uint64_t>(result->second)
                          : std::nullopt;
        };
        const auto evaluated = evaluate_target_integer_constant(
            program_, *expression, diagnostics_, size_of, align_of,
            source_namespace);
        return evaluated &&
               set_bit_field_width(member, *evaluated, member.location);
    }

    bool layout_record(RecordId id, SourceLocation use_location) {
        if (id.value >= layout_state_.size()) {
            layout_state_.resize(module_.records.size());
        }
        if (layout_state_[id.value] == 2) return true;
        if (layout_state_[id.value] == 3) return false;
        if (layout_state_[id.value] == 1) {
            diagnostics_.error(
                use_location,
                "record contains itself by value through a member cycle");
            return false;
        }
        auto& record = module_.record(id);
        if (!record.complete) {
            diagnostics_.error(
                use_location,
                "incomplete record type '" + record.source_name +
                    "' cannot be used as an object or member");
            return false;
        }
        layout_state_[id.value] = 1;
        std::uint64_t extent{};
        unsigned record_alignment = 1;
        bool valid = true;
        struct ActiveBitFieldUnit {
            TypeId type;
            std::uint64_t offset{};
            unsigned bits{};
            unsigned used{};
            unsigned alignment{1};
        };
        std::optional<ActiveBitFieldUnit> active_bit_field;
        const auto current_namespace = source_namespace(record.source_name);
        for (auto& member : record.members) {
            if (member.pending_bit_width &&
                !resolve_bit_field_width(member, current_namespace)) {
                valid = false;
            }
            const auto [size, natural_alignment] =
                storage_layout(member.type, member.location);
            if (size == 0) valid = false;
            const auto placement_alignment =
                std::max(member.alignment,
                         (record.packed || member.packed)
                             ? 1U
                             : natural_alignment);
            member.alignment = placement_alignment;
            record_alignment = std::max(record_alignment,
                                        placement_alignment);
            if (member.bit_width) {
                const auto unit_bits = size <=
                                               std::numeric_limits<unsigned>::max() /
                                                   8U
                                           ? static_cast<unsigned>(size * 8U)
                                           : 0U;
                const auto width = *member.bit_width;
                if (width == 0) {
                    active_bit_field.reset();
                    if (record.is_union) {
                        member.offset = 0;
                    } else if (const auto offset =
                                   align_up(extent, placement_alignment)) {
                        member.offset = *offset;
                        extent = *offset;
                    } else {
                        diagnostics_.error(
                            member.location,
                            "record layout overflows target storage");
                        valid = false;
                    }
                    continue;
                }
                if (unit_bits == 0 || width > unit_bits) {
                    valid = false;
                    continue;
                }
                if (record.is_union) {
                    member.offset = 0;
                    member.bit_offset =
                        target_.data_layout.bit_field_order ==
                                BitFieldOrder::LeastSignificantFirst
                            ? 0U
                            : unit_bits - width;
                    extent = std::max(extent, size);
                    continue;
                }
                const auto unqualified = module_.unqualified(member.type);
                const bool shares =
                    active_bit_field &&
                    active_bit_field->alignment == placement_alignment &&
                    (target_.data_layout.bit_field_unit_sharing ==
                             BitFieldUnitSharing::SameStorageSize
                         ? active_bit_field->bits == unit_bits
                         : active_bit_field->type == unqualified) &&
                    width <= active_bit_field->bits -
                                 active_bit_field->used;
                if (!shares) {
                    const auto offset = align_up(extent, placement_alignment);
                    if (!offset || *offset >
                                       std::numeric_limits<std::uint64_t>::max() -
                                           size) {
                        diagnostics_.error(
                            member.location,
                            "record layout overflows target storage");
                        valid = false;
                        active_bit_field.reset();
                        continue;
                    }
                    active_bit_field = ActiveBitFieldUnit{
                        unqualified, *offset, unit_bits, 0,
                        placement_alignment};
                    extent = *offset + size;
                }
                member.offset = active_bit_field->offset;
                member.bit_offset =
                    target_.data_layout.bit_field_order ==
                            BitFieldOrder::LeastSignificantFirst
                        ? active_bit_field->used
                        : active_bit_field->bits -
                              active_bit_field->used - width;
                active_bit_field->used += width;
                if (active_bit_field->used == active_bit_field->bits) {
                    active_bit_field.reset();
                }
                continue;
            }
            active_bit_field.reset();
            if (record.is_union) {
                member.offset = 0;
                extent = std::max(extent, size);
                continue;
            }
            const auto offset = align_up(extent, placement_alignment);
            if (!offset || *offset >
                               std::numeric_limits<std::uint64_t>::max() -
                                   size) {
                diagnostics_.error(member.location,
                                   "record layout overflows target storage");
                valid = false;
                continue;
            }
            member.offset = *offset;
            extent = *offset + size;
        }
        record_alignment = std::max(record_alignment,
                                    record.explicit_alignment);
        const auto rounded = align_up(extent, record_alignment);
        if (!rounded) {
            diagnostics_.error(record.location,
                               "record size overflows target storage");
            valid = false;
        }
        record.alignment = record_alignment;
        record.size = rounded.value_or(0);
        layout_state_[id.value] = valid ? 2 : 3;
        return valid;
    }

    void finish_records() {
        // Intern every referenced type before retaining references into the
        // record table. An implicit incomplete tag may append a shell.
        for (const auto& declaration : program_.records) {
            for (const auto& member : declaration.members) {
                (void)intern_type(member.type);
            }
        }
        for (std::size_t index = 0; index < module_.records.size(); ++index) {
            const auto* definition = module_.records[index].definition;
            if (!definition) continue;
            auto& record = module_.records[index];
            record.location = definition->location;
            record.complete = true;
            record.packed = parse_packed(definition->attributes,
                                         "a record definition");
            record.explicit_alignment =
                parse_alignment(definition->attributes,
                                "a record definition",
                                source_namespace(record.source_name));
            if (definition->members.empty()) {
                diagnostics_.error(definition->location,
                                   "a complete record requires at least one member");
            }
            std::unordered_set<std::string> names;
            for (const auto& source : definition->members) {
                if (!source.name.empty() &&
                    !names.insert(source.name).second) {
                    diagnostics_.error(
                        source.location,
                        "duplicate record member '" + source.name + "'");
                    continue;
                }
                RecordMember member;
                member.location = source.location;
                member.name = source.name;
                member.type = intern_type(source.type);
                validate_atomic_type(member.type, source.location);
                if (source.bit_width) {
                    const auto& type = module_.type(member.type);
                    bool valid_base = true;
                    if (type.kind != Type::Kind::Builtin ||
                        type.builtin < BuiltinType::Bool ||
                        type.builtin > BuiltinType::Uptr) {
                        diagnostics_.error(
                            source.location,
                            "bit-field base type must be bool, an integer, or an enumeration");
                        valid_base = false;
                    } else if (type.is_atomic) {
                        diagnostics_.error(
                            source.location,
                            "a bit-field cannot have atomic type");
                        valid_base = false;
                    }
                    const auto* width = source.bit_width.get();
                    while (width &&
                           width->kind == Expr::Kind::Parenthesized &&
                           width->left) {
                        width = width->left.get();
                    }
                    if (!width) {
                        diagnostics_.error(
                            source.location,
                            "bit-field width must be a nonnegative integer constant expression");
                    } else if (valid_base && width->evaluated_integer) {
                        (void)set_bit_field_width(
                            member, *width->evaluated_integer,
                            source.location);
                    } else if (valid_base) {
                        member.pending_bit_width = width;
                    }
                }
                member.packed = parse_packed(source.attributes,
                                             "a record member");
                member.alignment = parse_alignment(source.attributes,
                                                   "a record member",
                                                   source_namespace(record.source_name));
                record.members.push_back(std::move(member));
            }
        }
        layout_state_.assign(module_.records.size(), 0);
        for (std::size_t index = 0; index < module_.records.size(); ++index) {
            if (module_.records[index].complete) {
                (void)layout_record(
                    RecordId{static_cast<std::uint32_t>(index)},
                    module_.records[index].location);
            }
        }
    }

    void validate_atomic_type(TypeId id, SourceLocation location) {
        const auto& type = module_.type(id);
        if (type.kind == Type::Kind::Function) {
            if (!type.function || !type.function->abi.valid()) {
                diagnostics_.error(
                    location, "function-pointer type has no registered ABI");
                return;
            }
            const auto* abi = find_abi(target_, type.function->abi);
            if (!abi || abi->address_bits != module_.address_bits ||
                !abi->function_selectable) {
                diagnostics_.error(location,
                                   "function-pointer ABI is incompatible "
                                   "with the compilation data model");
            }
            if (type.is_const || type.is_volatile || type.is_atomic) {
                diagnostics_.error(
                    location, "function type cannot have object qualifiers");
            }
            for (const auto& parameter : type.function->parameters) {
                if (parameter.physical_location &&
                    *parameter.physical_location != "auto") {
                    diagnostics_.error(parameter.location,
                                       "manual function-pointer endpoints are "
                                       "not implemented yet");
                }
                validate_atomic_type(parameter.type, parameter.location);
            }
            validate_atomic_type(type.function->result_type, location);
            return;
        }
        if (type.kind == Type::Kind::Array) {
            if (!type.element || type.lanes == 0) {
                diagnostics_.error(location,
                                   "array type requires a positive fixed bound");
            } else {
                const auto& element = module_.type(*type.element);
                if (element.kind == Type::Kind::Builtin &&
                    element.builtin == BuiltinType::Void) {
                    diagnostics_.error(location,
                                       "array element type cannot be void");
                }
                if (element.kind == Type::Kind::Vector &&
                    element.scalable) {
                    diagnostics_.error(
                        location,
                        "array element type cannot be a scalable vector");
                }
            }
        }
        if (type.is_atomic &&
            (type.kind == Type::Kind::Vector || type.kind == Type::Kind::Array ||
             type.kind == Type::Kind::Record ||
             (type.kind == Type::Kind::Builtin &&
              (type.builtin == BuiltinType::Void ||
               type.builtin == BuiltinType::Label)))) {
            diagnostics_.error(
                location,
                "atomic qualifier requires an integer, floating, or pointer object type");
        }
        if ((type.kind == Type::Kind::Vector ||
             type.kind == Type::Kind::Array) && type.element) {
            if (module_.type(*type.element).is_atomic) {
                diagnostics_.error(
                    location,
                    type.kind == Type::Kind::Vector
                        ? "a vector element type cannot be atomic-qualified"
                        : "an array element type cannot be atomic-qualified");
            }
            validate_atomic_type(*type.element, location);
        } else if (type.kind == Type::Kind::Pointer && type.pointee) {
            validate_atomic_type(*type.pointee, location);
        }
    }

    static bool automatic(const std::optional<std::string>& location) {
        return !location || *location == "auto";
    }

    bool complete_object_type(TypeId id, SourceLocation location) {
        const auto& type = module_.type(id);
        if (type.kind == Type::Kind::Record) {
            if (!type.record || !module_.record(*type.record).complete) {
                diagnostics_.error(
                    location,
                    "object has incomplete type '" + type_name(module_, id) +
                        "'");
                return false;
            }
            if (module_.record(*type.record).size == 0) {
                diagnostics_.error(
                    location,
                    "object type '" + type_name(module_, id) +
                        "' has no valid storage layout");
                return false;
            }
            return true;
        }
        if (type.kind == Type::Kind::Array && type.element) {
            return complete_object_type(*type.element, location);
        }
        return true;
    }

    bool compatible(const Function& canonical, const FunctionDecl& declaration) {
        if (canonical.result_type != intern_type(declaration.return_type) ||
            canonical.parameters.size() != declaration.parameters.size() ||
            canonical.variadic != declaration.variadic) return false;
        const auto same_location = [](const std::optional<std::string>& left,
                                      const std::optional<std::string>& right) {
            const auto is_auto = [](const std::optional<std::string>& value) {
                return !value || *value == "auto";
            };
            return (is_auto(left) && is_auto(right)) || left == right;
        };
        if (!same_location(canonical.result_location, declaration.result_location)) return false;
        if (!canonical.declarations.empty()) {
            const auto& previous = *canonical.declarations.front();
            const auto previous_naked = previous.attribute("naked") != nullptr;
            const auto declaration_naked = declaration.attribute("naked") != nullptr;
            if (previous_naked != declaration_naked ||
                decode_attribute_string(previous.attribute("abi")) !=
                    decode_attribute_string(declaration.attribute("abi")) ||
                decode_attribute_string(previous.attribute("stack_cleanup")) !=
                    decode_attribute_string(declaration.attribute("stack_cleanup")) ||
                decoded_clobbers(previous) != decoded_clobbers(declaration)) return false;
        }
        for (std::size_t index = 0; index < canonical.parameters.size(); ++index) {
            const auto& left = canonical.parameters[index];
            const auto& right = declaration.parameters[index];
            if (left.type != intern_type(right.type) || left.mode != right.mode ||
                !same_location(left.physical_location, right.location_name)) return false;
        }
        return true;
    }

    Function make_function(const FunctionDecl& declaration) {
        Function function;
        function.id = {static_cast<std::uint32_t>(module_.functions.size())};
        function.location = declaration.location;
        function.source_name = declaration.name;
        function.source_unit = declaration.source_unit;
        function.linkage = declaration.linkage;
        function.result_type = intern_type(declaration.return_type);
        validate_atomic_type(function.result_type, declaration.location);
        if (module_.type(function.result_type).is_atomic) {
            diagnostics_.error(declaration.location,
                               "a function result cannot be atomic-qualified");
        }
        function.result_location = declaration.result_location;
        function.variadic = declaration.variadic;
        for (const auto& parameter : declaration.parameters) {
            const auto type = intern_type(parameter.type);
            validate_atomic_type(type, parameter.location);
            function.parameters.push_back({parameter.location, parameter.name,
                                           type, parameter.mode,
                                           parameter.location_name});
        }
        return function;
    }

    void collect_functions() {
        for (const auto& source : program_.functions) {
            const auto key = entity_key(source->linkage, source->source_unit, source->name);
            const auto found = function_keys_.find(key);
            FunctionId id;
            if (found == function_keys_.end()) {
                id = {static_cast<std::uint32_t>(module_.functions.size())};
                function_keys_.emplace(key, id);
                module_.functions.push_back(make_function(*source));
            } else {
                id = found->second;
                auto& canonical = module_.function(id);
                if (!compatible(canonical, *source)) {
                    diagnostics_.error(source->location,
                                       "incompatible redeclaration of function '" +
                                           source->name + "'");
                }
            }
            auto& canonical = module_.function(id);
            canonical.declarations.push_back(source.get());
            module_.function_ids.emplace(source.get(), id);
            if (source->definition()) {
                if (canonical.definition) {
                    diagnostics_.error(source->location,
                                       "duplicate definition of function '" + source->name + "'");
                } else {
                    canonical.definition = source.get();
                }
            }
        }
    }

    void finish_functions() {
        for (auto& function : module_.functions) {
            for (const auto* declaration : function.declarations) {
                if (declaration->definition()) continue;
                for (const auto& attribute : declaration->attributes) {
                    if (attribute.name == "aligned") {
                        diagnostics_.error(attribute.location,
                                           "aligned requires a function definition");
                    }
                }
            }
            const auto* representative = function.definition
                                             ? function.definition
                                             : function.declarations.back();
            function.location = representative->location;
            function.source_unit = representative->source_unit;
            function.linkage = representative->linkage;
            function.link_symbol = resolved_link_name(*representative, options_);
            if (function.link_symbol.empty()) {
                diagnostics_.error(
                    representative->location,
                    "selected mangling model produced an empty function "
                    "link name");
            }
            function.result_type = intern_type(representative->return_type);
            function.result_location = representative->result_location;
            function.parameters.clear();
            for (const auto& parameter : representative->parameters) {
                function.parameters.push_back({parameter.location, parameter.name,
                                               intern_type(parameter.type), parameter.mode,
                                               parameter.location_name});
            }

            const auto abi_text = decode_attribute_string(representative->attribute("abi"));
            const auto selected = abi_text.empty() ? options_.abi : abi_text;
            function.abi_explicit = !abi_text.empty();
            function.abi_contract =
                !function.variadic && !function.abi_explicit && function.definition &&
                        function.linkage != Linkage::Global
                    ? AbiContract::Dynamic
                    : AbiContract::Registered;
            const auto* selected_abi =
                find_abi(target_, selected, options_.target);
            if (selected_abi) {
                const auto* abi = selected_abi;
                function.abi = abi->id;
                if (!abi_text.empty() && !abi->function_selectable) {
                    diagnostics_.error(
                        representative->location,
                        "ABI model '" + abi->canonical_name +
                            "' is not function-selectable");
                }
            } else {
                diagnostics_.error(
                    representative->location,
                    "unknown ABI model '" + selected + "' for target '" +
                        options_.target + "'");
            }

            std::size_t variadic_attributes{};
            const Attribute* variadic_attribute{};
            for (const auto* declaration : function.declarations) {
                for (const auto& attribute : declaration->attributes) {
                    if (attribute.name != "variadic") continue;
                    ++variadic_attributes;
                    if (!variadic_attribute) variadic_attribute = &attribute;
                    if (!declaration->definition()) {
                        diagnostics_.error(
                            attribute.location,
                            "variadic state bindings are allowed only on a definition");
                    }
                }
            }
            if (function.variadic) {
                if (function.parameters.empty()) {
                    diagnostics_.error(
                        representative->location,
                        "a variadic function requires at least one named parameter");
                }
                for (const auto& parameter : function.parameters) {
                    if (parameter.mode != ParameterMode::In) {
                        diagnostics_.error(
                            parameter.location,
                            "every fixed parameter of a variadic function must use 'in'");
                    }
                    if (parameter.physical_location &&
                        *parameter.physical_location != "auto") {
                        diagnostics_.error(
                            parameter.location,
                            "bootstrap variadic lowering requires automatic fixed-parameter locations");
                    }
                }
                if (!selected_abi || !selected_abi->variadic_supported) {
                    diagnostics_.error(
                        representative->location,
                        "ABI model '" + selected +
                            "' does not define variadic lowering");
                }
                if (variadic_attributes > 1) {
                    diagnostics_.error(
                        representative->location,
                        "a variadic definition accepts at most one variadic attribute");
                }
                if (variadic_attributes != 0 && selected_abi &&
                    variadic_attribute) {
                    const auto* attribute = variadic_attribute;
                    std::unordered_set<std::string> names;
                    std::unordered_set<std::string> states;
                    for (const auto& argument : attribute->arguments) {
                        const auto quote = argument.find('"');
                        const auto decoded = quote == std::string::npos
                            ? std::optional<std::string>{}
                            : decode_string_literal(
                                  std::string_view(argument).substr(quote));
                        if (!decoded) {
                            diagnostics_.error(
                                attribute->location,
                                "variadic binding must end in a state-name string");
                            continue;
                        }
                        const auto state = std::find_if(
                            selected_abi->variadic_states.begin(),
                            selected_abi->variadic_states.end(),
                            [&](const AbiVariadicState& candidate) {
                                return candidate.canonical_name == *decoded;
                            });
                        if (state == selected_abi->variadic_states.end()) {
                            diagnostics_.error(
                                attribute->location,
                                "unknown variadic ABI state '" + *decoded + "'");
                            continue;
                        }
                        const auto prefix = std::string_view(argument).substr(0, quote);
                        if (!prefix.starts_with(state->type)) {
                            diagnostics_.error(
                                attribute->location,
                                "variadic state '" + *decoded +
                                    "' requires binding type '" + state->type + "'");
                            continue;
                        }
                        const auto name = prefix.substr(state->type.size());
                        const auto type = variadic_state_type(state->type);
                        if (!type || !source_identifier(name)) {
                            diagnostics_.error(
                                attribute->location,
                                "invalid variadic state binding declaration");
                            continue;
                        }
                        if (!names.insert(std::string(name)).second ||
                            !states.insert(*decoded).second) {
                            diagnostics_.error(
                                attribute->location,
                                "duplicate variadic state binding");
                            continue;
                        }
                        function.variadic_bindings.push_back(
                            {attribute->location, std::string(name),
                             intern_type(type), state->id});
                    }
                }
            } else if (variadic_attributes != 0) {
                diagnostics_.error(
                    representative->location,
                    "variadic attribute requires a variadic function definition");
            }
            if (const auto section = decode_attribute_string(
                    representative->attribute("section")); !section.empty()) {
                function.section = section;
            }
            if (function.definition) {
                function.minimum_alignment = parse_alignment(
                    function.definition->attributes, "function definition",
                    function.definition->source_namespace);
            }
            function.naked = representative->attribute("naked") != nullptr;
            for (const auto& attribute : representative->attributes) {
                if (attribute.name == "naked" && !attribute.arguments.empty()) {
                    diagnostics_.error(attribute.location, "naked does not take arguments");
                }
                if (attribute.name != "clobber") continue;
                for (const auto& argument : attribute.arguments) {
                    const auto value = decode_string_literal(argument);
                    if (!value) {
                        diagnostics_.error(attribute.location,
                                           "clobber arguments must be string literals");
                    } else {
                        function.clobbers.push_back(*value);
                        if (*value != "memory" && *value != "flags") {
                            const auto* entry = find_register(target_, *value);
                            const auto instruction_resource = std::any_of(
                                target_.instructions.begin(),
                                target_.instructions.end(),
                                [&](const InstructionEntry& instruction) {
                                    const auto contains = [&](const auto& resources) {
                                        return std::find(resources.begin(),
                                                         resources.end(), *value) !=
                                               resources.end();
                                    };
                                    return contains(instruction.implicit_reads) ||
                                           contains(instruction.implicit_writes);
                                });
                            if (!entry && !instruction_resource) {
                                diagnostics_.error(
                                    attribute.location,
                                    "unknown target resource in clobber: '" +
                                        *value + "'");
                            } else if (entry && entry->compiler_owned) {
                                diagnostics_.error(
                                    attribute.location,
                                    "stack/frame pointer cannot be a raw-inline "
                                    "scratch clobber");
                            }
                        }
                    }
                }
            }
            if (!function.definition) {
                function.ownership = BodyOwnership::None;
            } else if (function.naked) {
                function.ownership = BodyOwnership::RawMir;
                validate_naked_interface(function);
            } else {
                function.ownership = BodyOwnership::ManagedAst;
            }
            if (function.definition && function.definition->body) {
                collect_labels(function, *function.definition->body);
            }
        }
    }

    void collect_labels(Function& function, const Statement& statement) {
        if (statement.kind == Statement::Kind::Label) {
            if (module_.label(function.id, statement.label_name)) {
                diagnostics_.error(statement.location,
                                   "duplicate label '" + statement.label_name + "'");
            } else {
                const LabelId id{static_cast<std::uint32_t>(module_.labels.size())};
                const auto qualified =
                    function.source_name + "::" + statement.label_name;
                std::string symbol;
                if (statement.global_label) {
                    symbol = resolved_label_link_name(
                        qualified, statement.attributes, statement.location);
                    if (function.linkage != Linkage::Global ||
                        function.abi_contract != AbiContract::Registered) {
                        diagnostics_.error(
                            statement.location,
                            "a global label definition requires a global stable-ABI function");
                    }
                    if (function.definition &&
                        function.definition->attribute("always_inline")) {
                        diagnostics_.error(
                            statement.location,
                            "a function containing a global label cannot be always_inline");
                    }
                } else if (!statement.attributes.empty()) {
                    diagnostics_.error(statement.location,
                                       "attributes are not valid on a local label");
                }
                module_.labels.push_back(
                    {id, function.id, statement.location,
                     statement.label_name, qualified, std::move(symbol), {},
                     &statement, statement.global_label});
                function.labels.push_back(id);
            }
        }
        for (const auto& child : statement.statements) collect_labels(function, *child);
        if (statement.first) collect_labels(function, *statement.first);
        if (statement.second) collect_labels(function, *statement.second);
    }

    std::string resolved_label_link_name(
        std::string_view qualified_name,
        const std::vector<Attribute>& attributes,
        SourceLocation location) {
        const Attribute* link_name{};
        for (const auto& attribute : attributes) {
            if (attribute.name != "link_name") {
                diagnostics_.error(
                    attribute.location,
                    "attribute '" + attribute.name +
                        "' is not valid on a global label");
                continue;
            }
            if (link_name) {
                diagnostics_.error(attribute.location,
                                   "global label has more than one link_name attribute");
                continue;
            }
            link_name = &attribute;
        }
        std::string result;
        if (link_name) {
            result = decode_attribute_string(link_name);
            if (link_name->arguments.size() != 1 || result.empty()) {
                diagnostics_.error(
                    link_name->location,
                    "global-label link_name requires one nonempty string literal");
            }
        } else {
            result = encode_model_link_name(qualified_name, true,
                                            options_.mangling);
        }
        if (result.empty()) {
            diagnostics_.error(
                location,
                "selected mangling model produced an empty global-label link name");
        }
        return result;
    }

    Function* find_global_label_function(std::string_view name,
                                         SourceLocation location) {
        Function* result{};
        for (auto& function : module_.functions) {
            if (function.source_name != name) continue;
            if (result) {
                diagnostics_.error(
                    location,
                    "global label owner '" + std::string(name) +
                        "' is ambiguous");
                return nullptr;
            }
            result = &function;
        }
        if (!result) {
            diagnostics_.error(
                location,
                "global label refers to unknown function '" +
                    std::string(name) + "'");
        }
        return result;
    }

    void finish_global_labels() {
        for (const auto& declaration : program_.global_labels) {
            const auto split = declaration.qualified_name.rfind("::");
            if (split == std::string::npos) {
                diagnostics_.error(
                    declaration.location,
                    "a global label declaration requires a qualified label name");
                continue;
            }
            const auto function_name =
                std::string_view(declaration.qualified_name).substr(0, split);
            auto* function =
                find_global_label_function(function_name, declaration.location);
            if (!function) continue;
            if (function->linkage != Linkage::Global ||
                function->abi_contract != AbiContract::Registered) {
                diagnostics_.error(
                    declaration.location,
                    "a global label declaration requires a global stable-ABI function");
            }
            const auto symbol = resolved_label_link_name(
                declaration.qualified_name, declaration.attributes,
                declaration.location);
            auto found = std::find_if(
                module_.labels.begin(), module_.labels.end(),
                [&](const Label& label) {
                    return label.owner == function->id &&
                           label.qualified_name == declaration.qualified_name;
                });
            if (found != module_.labels.end()) {
                if (!found->is_global) {
                    diagnostics_.error(
                        declaration.location,
                        "global label declaration disagrees with a local label definition");
                } else if (found->link_symbol != symbol) {
                    diagnostics_.error(
                        declaration.location,
                        "global label declarations use different link names for '" +
                            declaration.qualified_name + "'");
                }
                found->declarations.push_back(&declaration);
                continue;
            }
            if (function->definition) {
                diagnostics_.error(
                    declaration.location,
                    "global label declaration has no matching definition in function '" +
                        function->source_name + "'");
                continue;
            }
            const LabelId id{
                static_cast<std::uint32_t>(module_.labels.size())};
            module_.labels.push_back(
                {id, function->id, declaration.location,
                 declaration.qualified_name.substr(split + 2),
                 declaration.qualified_name, symbol, {&declaration}, nullptr,
                 true});
        }
    }

    void validate_naked_interface(const Function& function) {
        if (function.variadic) {
            diagnostics_.error(function.location,
                               "naked function cannot have a variadic interface");
        }
        for (const auto& parameter : function.parameters) {
            if (automatic(parameter.physical_location)) {
                diagnostics_.error(parameter.location,
                                   "naked requires a complete manual ABI; parameter '" +
                                       parameter.name + "' has an automatic endpoint");
            }
        }
        const auto& result = module_.type(function.result_type);
        const bool is_void = result.kind == Type::Kind::Builtin &&
                             result.builtin == BuiltinType::Void;
        if (!is_void && automatic(function.result_location)) {
            diagnostics_.error(function.location,
                               "naked function with a non-void result requires an explicit result location");
        }
    }

    Object make_object(const ObjectDecl& declaration) {
        Object object;
        object.id = {static_cast<std::uint32_t>(module_.objects.size())};
        object.location = declaration.location;
        object.source_name = declaration.name;
        object.source_unit = declaration.source_unit;
        object.linkage = declaration.linkage;
        object.type = intern_type(declaration.type);
        validate_atomic_type(object.type, declaration.location);
        (void)complete_object_type(object.type, declaration.location);
        return object;
    }

    void collect_objects() {
        for (const auto& source : program_.objects) {
            const auto key = entity_key(source->linkage, source->source_unit, source->name);
            const auto found = object_keys_.find(key);
            ObjectId id;
            if (found == object_keys_.end()) {
                id = {static_cast<std::uint32_t>(module_.objects.size())};
                object_keys_.emplace(key, id);
                module_.objects.push_back(make_object(*source));
            } else {
                id = found->second;
                if (module_.objects[id.value].type != intern_type(source->type)) {
                    diagnostics_.error(source->location,
                                       "incompatible redeclaration of object '" + source->name + "'");
                }
            }
            auto& canonical = module_.objects[id.value];
            canonical.declarations.push_back(source.get());
            module_.object_ids.emplace(source.get(), id);
            if (source->initializer || source->linkage != Linkage::Group) {
                if (canonical.definition) {
                    diagnostics_.error(source->location,
                                       "duplicate definition of object '" + source->name + "'");
                } else {
                    canonical.definition = source.get();
                }
            }
        }
    }

    void finish_objects() {
        for (auto& object : module_.objects) {
            const auto* representative = object.definition
                                             ? object.definition
                                             : object.declarations.back();
            object.location = representative->location;
            object.source_unit = representative->source_unit;
            object.linkage = representative->linkage;
            object.link_symbol = resolved_link_name(*representative, options_);
            if (object.link_symbol.empty()) {
                diagnostics_.error(
                    representative->location,
                    "selected mangling model produced an empty object link "
                    "name");
            }
            if (const auto section = decode_attribute_string(
                    object_attribute(*representative, "section"));
                !section.empty()) {
                object.section = section;
            }
            object.minimum_alignment = parse_alignment(
                representative->attributes, "an object definition",
                source_namespace(object.source_name));
            object.is_thread_local =
                object_attribute(*representative, "thread_local") != nullptr;
            object.tls_model = decode_attribute_string(
                object_attribute(*representative, "tls_model"));
        }
    }

    void diagnose_symbol_collisions() {
        std::unordered_map<std::string, SourceLocation> symbols;
        const auto add = [&](std::string_view symbol, SourceLocation location,
                             std::string_view source_name) {
            const auto [found, inserted] = symbols.emplace(std::string(symbol), location);
            if (!inserted) {
                diagnostics_.error(location, "link symbol '" + std::string(symbol) +
                                                 "' is shared by distinct entities including '" +
                                                 std::string(source_name) + "'");
            }
        };
        for (const auto& function : module_.functions) {
            add(function.link_symbol, function.location, function.source_name);
        }
        for (const auto& object : module_.objects) {
            add(object.link_symbol, object.location, object.source_name);
        }
        for (const auto& label : module_.labels) {
            if (label.is_global) {
                add(label.link_symbol, label.location, label.qualified_name);
            }
        }
    }

    Program& program_;
    const CompilerOptions& options_;
    const TargetInfo& target_;
    Diagnostics& diagnostics_;
    Module module_;
    unsigned address_bytes_{8};
    std::vector<unsigned char> layout_state_;
    std::unordered_map<std::string, FunctionId> function_keys_;
    std::unordered_map<std::string, ObjectId> object_keys_;
};

} // namespace

const Function* Module::function(const FunctionDecl& declaration) const {
    const auto found = function_ids.find(&declaration);
    return found == function_ids.end() ? nullptr : &functions[found->second.value];
}

std::optional<TypeId> Module::builtin(BuiltinType kind) const {
    for (std::uint32_t index = 0; index < types.size(); ++index) {
        const auto& candidate = types[index];
        if (candidate.kind == Type::Kind::Builtin && candidate.builtin == kind &&
            !candidate.is_const && !candidate.is_volatile &&
            !candidate.is_atomic && !candidate.is_restrict) {
            return TypeId{index};
        }
    }
    return std::nullopt;
}

TypeId Module::intern_type(const TypePtr& source) {
    Type candidate;
    if (source) {
        candidate.kind =
            source->kind == cross::Type::Kind::Pointer    ? Type::Kind::Pointer
            : source->kind == cross::Type::Kind::Vector   ? Type::Kind::Vector
            : source->kind == cross::Type::Kind::Array    ? Type::Kind::Array
            : source->kind == cross::Type::Kind::Record   ? Type::Kind::Record
            : source->kind == cross::Type::Kind::Function ? Type::Kind::Function
                                                          : Type::Kind::Builtin;
        candidate.builtin = source->builtin;
        candidate.nominal_name = source->nominal_name;
        candidate.is_const = source->is_const;
        candidate.is_volatile = source->is_volatile;
        candidate.is_atomic = source->is_atomic;
        candidate.is_restrict = source->is_restrict;
        if (source->kind == cross::Type::Kind::Pointer) {
            candidate.pointee = intern_type(source->pointee);
        } else if (source->kind == cross::Type::Kind::Function &&
                   source->function) {
            FunctionSignature signature;
            signature.result_type = intern_type(source->function->result);
            signature.variadic = source->function->variadic;
            const auto found = abi_names.find(source->function->abi);
            signature.abi = source->function->abi.empty() ? default_abi
                            : found == abi_names.end()    ? AbiId{}
                                                          : found->second;
            for (const auto& parameter : source->function->parameters) {
                signature.parameters.push_back(
                    {parameter.location, parameter.name,
                     intern_type(parameter.type), parameter.mode,
                     parameter.location_name});
            }
            candidate.function = std::move(signature);
        } else if (source->kind == cross::Type::Kind::Vector ||
                   source->kind == cross::Type::Kind::Array) {
            candidate.element = intern_type(source->element);
            candidate.lanes = source->lanes;
            candidate.scalable = source->scalable;
        } else if (source->kind == cross::Type::Kind::Record) {
            auto found = record_ids.find(source->nominal_name);
            if (found == record_ids.end()) {
                const RecordId id{static_cast<std::uint32_t>(records.size())};
                record_ids.emplace(source->nominal_name, id);
                Record record;
                record.id = id;
                record.source_name = source->nominal_name;
                record.is_union = source->is_union;
                records.push_back(std::move(record));
                candidate.record = id;
            } else {
                candidate.record = found->second;
            }
        }
    }
    for (std::uint32_t index = 0; index < types.size(); ++index) {
        const auto& type = types[index];
        if (type.kind == candidate.kind && type.builtin == candidate.builtin &&
            type.pointee == candidate.pointee &&
            type.record == candidate.record &&
            type.function == candidate.function &&
            type.element == candidate.element &&
            type.lanes == candidate.lanes &&
            type.scalable == candidate.scalable &&
            type.nominal_name == candidate.nominal_name &&
            type.is_const == candidate.is_const &&
            type.is_volatile == candidate.is_volatile &&
            type.is_restrict == candidate.is_restrict &&
            type.is_atomic == candidate.is_atomic) {
            return {index};
        }
    }
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return id;
}

TypeId Module::function_type(FunctionSignature signature) {
    for (std::uint32_t index = 0; index < types.size(); ++index) {
        if (types[index].kind == Type::Kind::Function &&
            types[index].function == signature)
            return {index};
    }
    Type type;
    type.kind = Type::Kind::Function;
    type.function = std::move(signature);
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(type));
    return id;
}

TypeId Module::pointer_to(TypeId pointee) {
    for (std::uint32_t index = 0; index < types.size(); ++index) {
        const auto& candidate = types[index];
        if (candidate.kind == Type::Kind::Pointer &&
            candidate.pointee == pointee && !candidate.is_const &&
            !candidate.is_volatile && !candidate.is_atomic &&
            !candidate.is_restrict &&
            candidate.nominal_name.empty()) {
            return {index};
        }
    }
    Type type;
    type.kind = Type::Kind::Pointer;
    type.pointee = pointee;
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(type));
    return id;
}

TypeId Module::unqualified(TypeId id) {
    const auto& source = type(id);
    if (!source.is_const && !source.is_volatile && !source.is_atomic &&
        !source.is_restrict) {
        return id;
    }
    Type candidate = source;
    candidate.is_const = false;
    candidate.is_volatile = false;
    candidate.is_atomic = false;
    candidate.is_restrict = false;
    for (std::uint32_t index = 0; index < types.size(); ++index) {
        const auto& existing = types[index];
        if (existing.kind == candidate.kind &&
            existing.builtin == candidate.builtin &&
            existing.pointee == candidate.pointee &&
            existing.record == candidate.record &&
            existing.element == candidate.element &&
            existing.lanes == candidate.lanes &&
            existing.scalable == candidate.scalable &&
            existing.nominal_name == candidate.nominal_name &&
            !existing.is_const && !existing.is_volatile &&
            !existing.is_atomic && !existing.is_restrict) {
            return {index};
        }
    }
    const TypeId result{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return result;
}

TypeId Module::add_qualifiers(TypeId id, bool is_const,
                              bool is_volatile) {
    const auto& source = type(id);
    if ((!is_const || source.is_const) &&
        (!is_volatile || source.is_volatile)) {
        return id;
    }
    Type candidate = source;
    candidate.is_const = candidate.is_const || is_const;
    candidate.is_volatile = candidate.is_volatile || is_volatile;
    for (std::uint32_t index = 0; index < types.size(); ++index) {
        const auto& existing = types[index];
        if (existing.kind == candidate.kind &&
            existing.builtin == candidate.builtin &&
            existing.pointee == candidate.pointee &&
            existing.record == candidate.record &&
            existing.element == candidate.element &&
            existing.lanes == candidate.lanes &&
            existing.scalable == candidate.scalable &&
            existing.nominal_name == candidate.nominal_name &&
            existing.is_const == candidate.is_const &&
            existing.is_volatile == candidate.is_volatile &&
            existing.is_restrict == candidate.is_restrict &&
            existing.is_atomic == candidate.is_atomic) {
            return {index};
        }
    }
    const TypeId result{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return result;
}

TypeId Module::vector_of(TypeId element, std::uint32_t lanes,
                         bool scalable) {
    for (std::uint32_t index = 0; index < types.size(); ++index) {
        const auto& candidate = types[index];
        if (candidate.kind == Type::Kind::Vector &&
            candidate.element == element && candidate.lanes == lanes &&
            candidate.scalable == scalable && !candidate.is_const &&
            !candidate.is_volatile && !candidate.is_atomic &&
            !candidate.is_restrict) {
            return {index};
        }
    }
    Type type;
    type.kind = Type::Kind::Vector;
    type.element = element;
    type.lanes = lanes;
    type.scalable = scalable;
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(type));
    return id;
}

TypeId Module::array_of(TypeId element, std::uint32_t elements) {
    for (std::uint32_t index = 0; index < types.size(); ++index) {
        const auto& candidate = types[index];
        if (candidate.kind == Type::Kind::Array &&
            candidate.element == element && candidate.lanes == elements &&
            !candidate.is_const && !candidate.is_volatile &&
            !candidate.is_atomic && !candidate.is_restrict) {
            return {index};
        }
    }
    Type type;
    type.kind = Type::Kind::Array;
    type.element = element;
    type.lanes = elements;
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(type));
    return id;
}

const Record* Module::record(std::string_view name) const {
    const auto found = record_ids.find(std::string(name));
    return found == record_ids.end() ? nullptr
                                    : &records.at(found->second.value);
}

const RecordMember* Module::member(RecordId id,
                                   std::string_view name) const {
    const auto& source = record(id);
    const auto found = std::find_if(
        source.members.begin(), source.members.end(),
        [&](const RecordMember& candidate) { return candidate.name == name; });
    return found == source.members.end() ? nullptr : &*found;
}

const Object* Module::object(const ObjectDecl& declaration) const {
    const auto found = object_ids.find(&declaration);
    return found == object_ids.end() ? nullptr : &objects[found->second.value];
}

const Label* Module::label(FunctionId function, std::string_view name) const {
    for (const auto id : functions.at(function.value).labels) {
        const auto& candidate = labels.at(id.value);
        if (candidate.source_name == name || candidate.qualified_name == name) {
            return &candidate;
        }
    }
    for (const auto& candidate : labels) {
        if (candidate.owner == function && candidate.is_global &&
            (candidate.source_name == name ||
             candidate.qualified_name == name)) {
            return &candidate;
        }
    }
    return nullptr;
}

const Label* Module::global_label(std::string_view qualified_name) const {
    for (const auto& candidate : labels) {
        if (candidate.is_global &&
            candidate.qualified_name == qualified_name) {
            return &candidate;
        }
    }
    return nullptr;
}

bool Module::raw_owned(const FunctionDecl& declaration) const {
    const auto* entity = function(declaration);
    return entity && entity->definition == &declaration &&
           entity->ownership == BodyOwnership::RawMir;
}

Module build(Program& program, const CompilerOptions& options,
             const TargetInfo& target, Diagnostics& diagnostics) {
    return Builder(program, options, target, diagnostics).run();
}

bool stabilize_function_address(Module& module, FunctionId id,
                                SourceLocation location,
                                Diagnostics& diagnostics) {
    auto& function = module.function(id);
    if ((function.result_location && *function.result_location != "auto") ||
        !function.clobbers.empty() ||
        std::any_of(function.parameters.begin(), function.parameters.end(),
                    [](const Parameter& parameter) {
                        return parameter.physical_location &&
                               *parameter.physical_location != "auto";
                    })) {
        diagnostics.error(
            location, "function-pointer adapters for manual endpoints or extra "
                      "clobbers are not implemented yet");
        return false;
    }
    function.abi_contract = AbiContract::Registered;
    return true;
}

std::optional<FunctionSignature>
call_signature(const Module& module, std::optional<FunctionId> direct,
               std::optional<TypeId> indirect) {
    if (direct.has_value() == indirect.has_value()) return std::nullopt;
    if (direct) {
        if (direct->value >= module.functions.size()) return std::nullopt;
        const auto& function = module.function(*direct);
        return FunctionSignature{function.result_type, function.parameters,
                                 function.abi, function.variadic};
    }
    if (indirect->value >= module.types.size()) return std::nullopt;
    const auto& type = module.type(*indirect);
    return type.kind == Type::Kind::Function ? type.function : std::nullopt;
}

std::string type_name(const Module& module, TypeId id) {
    const auto& type = module.type(id);
    std::string prefix;
    if (type.is_const) prefix += "const ";
    if (type.is_volatile) prefix += "volatile ";
    if (type.is_restrict) prefix += "restrict ";
    if (type.is_atomic) prefix += "[[atomic]] ";
    if (type.kind == Type::Kind::Pointer) {
        return prefix + type_name(module, *type.pointee) + " *";
    }
    if (type.kind == Type::Kind::Function && type.function) {
        std::string result =
            type_name(module, type.function->result_type) + " (";
        for (std::size_t index = 0; index < type.function->parameters.size();
             ++index) {
            if (index) result += ", ";
            const auto& parameter = type.function->parameters[index];
            result += std::string(parameter_mode_name(parameter.mode)) + " " +
                      type_name(module, parameter.type);
        }
        if (type.function->variadic)
            result += type.function->parameters.empty() ? "..." : ", ...";
        return prefix + result + ")";
    }
    if (type.kind == Type::Kind::Vector) {
        return prefix + (type.scalable ? "scalable_vector<" : "vector<") +
               type_name(module, *type.element) + "," +
               std::to_string(type.lanes) + ">";
    }
    if (type.kind == Type::Kind::Array) {
        return prefix + type_name(module, *type.element) + "[" +
               (type.lanes == 0 ? std::string("*")
                                : std::to_string(type.lanes)) +
               "]";
    }
    if (type.kind == Type::Kind::Record && type.record) {
        const auto& record = module.record(*type.record);
        return prefix + (record.is_union ? "union " : "struct ") +
               record.source_name;
    }
    if (!type.nominal_name.empty()) {
        return prefix + "enum " + type.nominal_name;
    }
    static constexpr std::string_view names[] = {
        "void", "bool", "i8", "u8", "i16", "u16", "i32", "u32",
        "i64", "u64", "i128", "u128", "iptr", "uptr", "f32",
        "f64", "f80", "f128", "fptr", "label",
    };
    return prefix + std::string(names[static_cast<unsigned>(type.builtin)]);
}

std::optional<std::uint64_t> layout_size(const Module& module, TypeId id,
                                         const TargetInfo& target) {
    const auto& type = module.type(id);
    if (type.kind == Type::Kind::Pointer) {
        return (module.address_bits + 7U) / 8U;
    }
    if (type.kind == Type::Kind::Record) {
        if (!type.record || !module.record(*type.record).complete ||
            module.record(*type.record).size == 0) {
            return std::nullopt;
        }
        return module.record(*type.record).size;
    }
    if (type.kind == Type::Kind::Array) {
        if (!type.element || type.lanes == 0) return std::nullopt;
        const auto element = layout_size(module, *type.element, target);
        if (!element || *element == 0 ||
            type.lanes > std::numeric_limits<std::uint64_t>::max() /
                             *element) {
            return std::nullopt;
        }
        return *element * type.lanes;
    }
    if (type.kind == Type::Kind::Vector) {
        if (type.scalable || !type.element || type.lanes == 0) {
            return std::nullopt;
        }
        const auto element = layout_size(module, *type.element, target);
        if (!element ||
            type.lanes > std::numeric_limits<std::uint64_t>::max() /
                             *element) {
            return std::nullopt;
        }
        return *element * type.lanes;
    }
    if (type.kind != Type::Kind::Builtin ||
        type.builtin == BuiltinType::Void) {
        return std::nullopt;
    }
    if (type.builtin == BuiltinType::F80) {
        return target.data_layout.f80_storage_bytes;
    }
    const auto bits = type.builtin == BuiltinType::Bool ||
                              type.builtin == BuiltinType::I8 ||
                              type.builtin == BuiltinType::U8
                          ? 8U
                      : type.builtin == BuiltinType::I16 ||
                                type.builtin == BuiltinType::U16
                          ? 16U
                      : type.builtin == BuiltinType::I32 ||
                                type.builtin == BuiltinType::U32 ||
                                type.builtin == BuiltinType::F32
                          ? 32U
                      : type.builtin == BuiltinType::I64 ||
                                type.builtin == BuiltinType::U64 ||
                                type.builtin == BuiltinType::F64
                          ? 64U
                      : type.builtin == BuiltinType::Iptr ||
                                type.builtin == BuiltinType::Uptr ||
                                type.builtin == BuiltinType::Fptr ||
                                type.builtin == BuiltinType::Label
                          ? module.address_bits
                          : 128U;
    return (bits + 7U) / 8U;
}

std::optional<std::uint64_t> layout_alignment(
    const Module& module, TypeId id, const TargetInfo& target) {
    const auto& type = module.type(id);
    if (type.kind == Type::Kind::Record) {
        if (!type.record || !module.record(*type.record).complete) {
            return std::nullopt;
        }
        return module.record(*type.record).alignment;
    }
    if (type.kind == Type::Kind::Array && type.element) {
        return layout_alignment(module, *type.element, target);
    }
    if (type.kind == Type::Kind::Builtin &&
        type.builtin == BuiltinType::F80) {
        return target.data_layout.f80_alignment;
    }
    const auto size = layout_size(module, id, target);
    if (!size) return std::nullopt;
    return std::max<std::uint64_t>(
        1, std::min<std::uint64_t>(
               *size, target.data_layout.natural_alignment_limit));
}

} // namespace cross::hir

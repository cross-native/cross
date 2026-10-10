// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/native/debug_info.hpp"

#include "target/assembly_format.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace cross::native {
namespace {

// DWARF 5 encodings used below.
namespace dw {
constexpr unsigned tag_array_type = 0x01;
constexpr unsigned tag_enumeration_type = 0x04;
constexpr unsigned tag_formal_parameter = 0x05;
constexpr unsigned tag_member = 0x0d;
constexpr unsigned tag_pointer_type = 0x0f;
constexpr unsigned tag_compile_unit = 0x11;
constexpr unsigned tag_structure_type = 0x13;
constexpr unsigned tag_subroutine_type = 0x15;
constexpr unsigned tag_union_type = 0x17;
constexpr unsigned tag_unspecified_parameters = 0x18;
constexpr unsigned tag_subrange_type = 0x21;
constexpr unsigned tag_base_type = 0x24;
constexpr unsigned tag_const_type = 0x26;
constexpr unsigned tag_enumerator = 0x28;
constexpr unsigned tag_subprogram = 0x2e;
constexpr unsigned tag_variable = 0x34;
constexpr unsigned tag_volatile_type = 0x35;
constexpr unsigned tag_restrict_type = 0x37;
constexpr unsigned tag_namespace = 0x39;
constexpr unsigned tag_atomic_type = 0x47;

constexpr unsigned at_location = 0x02;
constexpr unsigned at_name = 0x03;
constexpr unsigned at_byte_size = 0x0b;
constexpr unsigned at_bit_size = 0x0d;
constexpr unsigned at_stmt_list = 0x10;
constexpr unsigned at_low_pc = 0x11;
constexpr unsigned at_high_pc = 0x12;
constexpr unsigned at_language = 0x13;
constexpr unsigned at_comp_dir = 0x1b;
constexpr unsigned at_const_value = 0x1c;
constexpr unsigned at_producer = 0x25;
constexpr unsigned at_count = 0x37;
constexpr unsigned at_data_member_location = 0x38;
constexpr unsigned at_decl_file = 0x3a;
constexpr unsigned at_decl_line = 0x3b;
constexpr unsigned at_declaration = 0x3c;
constexpr unsigned at_encoding = 0x3e;
constexpr unsigned at_external = 0x3f;
constexpr unsigned at_frame_base = 0x40;
constexpr unsigned at_type = 0x49;
constexpr unsigned at_ranges = 0x55;
constexpr unsigned at_data_bit_offset = 0x6b;
constexpr unsigned at_linkage_name = 0x6e;
constexpr unsigned at_gnu_vector = 0x2107;

constexpr unsigned form_addr = 0x01;
constexpr unsigned form_data2 = 0x05;
constexpr unsigned form_data4 = 0x06;
constexpr unsigned form_data1 = 0x0b;
constexpr unsigned form_sdata = 0x0d;
constexpr unsigned form_strp = 0x0e;
constexpr unsigned form_udata = 0x0f;
constexpr unsigned form_ref4 = 0x13;
constexpr unsigned form_sec_offset = 0x17;
constexpr unsigned form_exprloc = 0x18;
constexpr unsigned form_flag_present = 0x19;

constexpr unsigned ate_address = 0x01;
constexpr unsigned ate_boolean = 0x02;
constexpr unsigned ate_float = 0x04;
constexpr unsigned ate_signed = 0x05;
constexpr unsigned ate_signed_char = 0x06;
constexpr unsigned ate_unsigned = 0x07;
constexpr unsigned ate_unsigned_char = 0x08;

constexpr unsigned ut_compile = 0x01;
constexpr unsigned lang_c_plus_plus_14 = 0x21;
constexpr unsigned op_addr = 0x03;
constexpr unsigned op_reg0 = 0x50;
constexpr unsigned op_breg0 = 0x70;
constexpr unsigned op_call_frame_cfa = 0x9c;
constexpr unsigned rle_end_of_list = 0x00;
constexpr unsigned rle_start_length = 0x07;
} // namespace dw

struct Attribute {
    unsigned name{};
    unsigned form{};
    // Directive lines that encode the value; empty for a present flag.
    std::string value;
};

struct Die {
    unsigned tag{};
    std::vector<Attribute> attributes;
    std::vector<Die> children;
    // Defined at the entry when another entry refers to it.
    std::string label{};
};

// A generated token is described at the outermost invocation that produced
// it, and preprocessed text at the file and line its line marker names.
SourceLocation written(SourceLocation location) {
    for (unsigned depth = 0; location.valid() && depth < 64; ++depth) {
        const auto* file = location.file;
        if (const auto* expansion = file->expansion_at(location.offset);
            expansion && expansion->invocation.valid()) {
            location = expansion->invocation;
            continue;
        }
        if (location.line != 0 && location.line <= file->line_origins.size()) {
            auto origin = file->line_origins[location.line - 1];
            if (origin.valid()) {
                origin.column += std::max(1U, location.column) - 1;
                return origin;
            }
        }
        if (const auto* token = file->token_origin_at(location.offset);
            token && token->span.valid() && token->span.file != file) {
            location = token->span;
            continue;
        }
        return location;
    }
    return location;
}

std::string assembly_string(std::string_view text) {
    static constexpr char digits[] = "01234567";
    std::string result = "\"";
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            result += '\\';
            result += character;
        } else if (byte < 0x20 || byte >= 0x7f) {
            result += '\\';
            result += digits[byte >> 6];
            result += digits[(byte >> 3) & 7];
            result += digits[byte & 7];
        } else {
            result += character;
        }
    }
    return result + '"';
}

// Splits a qualified name at the `::` separators outside generic arguments.
std::vector<std::string> name_components(std::string_view name) {
    std::vector<std::string> result;
    int depth = 0;
    std::size_t begin = 0;
    for (std::size_t index = 0; index < name.size(); ++index) {
        const char character = name[index];
        if (character == '<' || character == '(' || character == '[') ++depth;
        else if (character == '>' || character == ')' || character == ']') --depth;
        else if (depth == 0 && character == ':' && index + 1 < name.size() &&
                 name[index + 1] == ':') {
            result.emplace_back(name.substr(begin, index - begin));
            begin = index + 2;
            ++index;
        }
    }
    result.emplace_back(name.substr(begin));
    return result;
}

// The source name of a function: a generic instance carries the generic's
// name, and a specialized clone the name of the function it copies.
std::string source_name(const hir::Function& function) {
    std::string name = function.definition ? function.definition->name
                                           : function.source_name;
    if (function.definition && function.definition->generic_instance) {
        if (const auto suffix = name.rfind("$G"); suffix != std::string::npos)
            name.resize(suffix);
    }
    return name;
}

std::string udata(std::uint64_t value) {
    return "\t.uleb128 " + std::to_string(value) + '\n';
}

void append_sleb(std::vector<std::uint8_t>& bytes, std::int64_t value) {
    for (;;) {
        const auto byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        const bool done = (value == 0 && (byte & 0x40) == 0) ||
                          (value == -1 && (byte & 0x40) != 0);
        bytes.push_back(done ? byte : static_cast<std::uint8_t>(byte | 0x80));
        if (done) return;
    }
}

// An exprloc value: the length, then the bytes.
std::string expression(const std::vector<std::uint8_t>& bytes) {
    std::string result = udata(bytes.size()) + "\t.byte ";
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) result += ", ";
        result += std::to_string(bytes[index]);
    }
    return result + '\n';
}

class Writer {
public:
    Writer(ObjectFormat format, unsigned address_bytes)
        : format_(format), address_bytes_(address_bytes),
          prefix_(format == ObjectFormat::MachO ? "L" : ".L") {}

    [[nodiscard]] std::string label(std::string_view kind) const {
        return prefix_ + "cross.debug." + std::string(kind);
    }

    [[nodiscard]] std::string section(std::string_view name) const {
        switch (format_) {
        case ObjectFormat::Coff:
            return ".section ." + std::string(name) + ",\"dr\"\n";
        case ObjectFormat::MachO:
            return ".section __DWARF,__" + std::string(name) + ",regular,debug\n";
        default:
            return ".section ." + std::string(name) +
                   (name == "debug_str" ? ",\"MS\",@progbits,1\n"
                                        : ",\"\",@progbits\n");
        }
    }

    // A 4-byte offset of `target` within its debugging section, whose first
    // byte is labeled `start`.
    [[nodiscard]] std::string offset(std::string_view target,
                                     std::string_view start) const {
        switch (format_) {
        case ObjectFormat::Coff:
            return "\t.secrel32 " + std::string(target) + '\n';
        case ObjectFormat::MachO:
            return "\t.long " + std::string(target) + '-' + std::string(start) + '\n';
        default:
            return "\t.long " + std::string(target) + '\n';
        }
    }

    [[nodiscard]] std::string address(std::string_view value) const {
        return std::string(address_bytes_ == 8 ? "\t.quad " : "\t.long ") +
               std::string(value) + '\n';
    }

    [[nodiscard]] unsigned address_bytes() const { return address_bytes_; }

    // The .debug_str offset of `text`.
    [[nodiscard]] std::string string(const std::string& text) {
        auto [found, inserted] = strings_.try_emplace(text, strings_.size());
        if (inserted) string_order_.push_back(&found->first);
        return offset(label("str." + std::to_string(found->second)),
                      label("str"));
    }

    [[nodiscard]] std::string strings() const {
        std::string result = section("debug_str") + label("str") + ":\n";
        for (std::size_t index = 0; index < string_order_.size(); ++index) {
            result += label("str." + std::to_string(index)) + ":\n\t.asciz " +
                      assembly_string(*string_order_[index]) + '\n';
        }
        return result;
    }

    // A reference to the entry labeled `target` in the unit.
    [[nodiscard]] std::string reference(std::string_view target) const {
        return "\t.long " + std::string(target) + '-' + label("info") + '\n';
    }

private:
    ObjectFormat format_;
    unsigned address_bytes_;
    std::string prefix_;
    std::map<std::string, std::size_t> strings_;
    std::vector<const std::string*> string_order_;
};

// Entries by the namespace path that contains them, in creation order.
class Scopes {
public:
    void add(const std::vector<std::string>& path, Die die) {
        auto [found, inserted] = entries_.try_emplace(path);
        if (inserted) order_.push_back(path);
        found->second.push_back(std::move(die));
    }

    // Moves the entries into `parent`, with a namespace entry for each path
    // below `path`.
    void build(Writer& writer, const std::vector<std::string>& path, Die& parent) {
        if (const auto found = entries_.find(path); found != entries_.end()) {
            for (auto& die : found->second) parent.children.push_back(std::move(die));
        }
        std::vector<std::string> nested;
        for (const auto& scope : order_) {
            if (scope.size() <= path.size() ||
                !std::equal(path.begin(), path.end(), scope.begin()))
                continue;
            const auto& name = scope[path.size()];
            if (std::find(nested.begin(), nested.end(), name) == nested.end())
                nested.push_back(name);
        }
        for (const auto& name : nested) {
            Die die{dw::tag_namespace, {}, {}};
            die.attributes.push_back({dw::at_name, dw::form_strp, writer.string(name)});
            auto inner = path;
            inner.push_back(name);
            build(writer, inner, die);
            parent.children.push_back(std::move(die));
        }
    }

private:
    std::map<std::vector<std::string>, std::vector<Die>> entries_;
    std::vector<std::vector<std::string>> order_;
};

// Type entries, created on first reference.
class Types {
public:
    Types(Writer& writer, Scopes& scopes, const hir::Module& module,
          const TargetInfo& target, std::span<const EnumDecl> enumerations)
        : writer_(writer), scopes_(scopes), module_(module), target_(target),
          enumerations_(enumerations) {}

    // The DW_AT_type attribute for `id`, or nothing for void.
    std::optional<Attribute> attribute(hir::TypeId id) {
        const auto label = qualified(id);
        if (label.empty()) return std::nullopt;
        return Attribute{dw::at_type, dw::form_ref4, writer_.reference(label)};
    }

private:
    std::string next_label() {
        return writer_.label("type." + std::to_string(next_++));
    }

    void add_type(Die& die, hir::TypeId id) {
        if (auto type = attribute(id)) die.attributes.push_back(std::move(*type));
    }

    std::uint64_t size(hir::TypeId id) const {
        return hir::natural_size(module_, id, target_).value_or(0);
    }

    // The label of the entry for `id` with its qualifiers, empty for void.
    std::string qualified(hir::TypeId id) {
        if (const auto found = qualified_.find(id.value); found != qualified_.end())
            return found->second;
        const auto& type = module_.type(id);
        auto label = core(id);
        const std::pair<bool, unsigned> qualifiers[] = {
            {type.is_atomic, dw::tag_atomic_type},
            {type.is_restrict, dw::tag_restrict_type},
            {type.is_volatile, dw::tag_volatile_type},
            {type.is_const, dw::tag_const_type},
        };
        for (const auto& [present, tag] : qualifiers) {
            if (!present) continue;
            Die die{tag, {}, {}, next_label()};
            if (!label.empty())
                die.attributes.push_back({dw::at_type, dw::form_ref4, writer_.reference(label)});
            label = die.label;
            scopes_.add({}, std::move(die));
        }
        qualified_.emplace(id.value, label);
        return label;
    }

    // The label of the entry of the type without qualifiers.
    std::string core(hir::TypeId id) {
        const auto& type = module_.type(id);
        std::string key;
        switch (type.kind) {
        case hir::Type::Kind::Builtin:
            if (type.builtin == BuiltinType::Void && type.nominal_key().empty())
                return {};
            key = "b" + std::to_string(static_cast<unsigned>(type.builtin)) + ':' +
                  type.nominal_name + ':' +
                  std::to_string(reinterpret_cast<std::uintptr_t>(type.nominal_identity.get()));
            break;
        case hir::Type::Kind::Pointer:
            key = "p" + std::to_string(type.pointee->value);
            break;
        case hir::Type::Kind::Array:
        case hir::Type::Kind::Vector:
            key = (type.kind == hir::Type::Kind::Array ? "a" : "v") +
                  std::to_string(type.element->value) + 'x' + std::to_string(type.lanes);
            break;
        case hir::Type::Kind::Record:
            key = "r" + std::to_string(type.record->value);
            break;
        case hir::Type::Kind::Function:
            key = "f" + std::to_string(id.value);
            break;
        }
        if (const auto found = core_.find(key); found != core_.end()) return found->second;
        const auto label = next_label();
        core_.emplace(key, label);
        std::vector<std::string> path;
        Die die{dw::tag_base_type, {}, {}, label};
        switch (type.kind) {
        case hir::Type::Kind::Builtin:
            if (type.nominal_key().empty()) base(die, type.builtin, id);
            else path = enumeration(die, type, id);
            break;
        case hir::Type::Kind::Pointer:
            die.tag = dw::tag_pointer_type;
            die.attributes.push_back({dw::at_byte_size, dw::form_data1,
                                      "\t.byte " + std::to_string(writer_.address_bytes()) + '\n'});
            add_type(die, *type.pointee);
            break;
        case hir::Type::Kind::Array:
        case hir::Type::Kind::Vector: {
            die.tag = dw::tag_array_type;
            if (type.kind == hir::Type::Kind::Vector)
                die.attributes.push_back({dw::at_gnu_vector, dw::form_flag_present, {}});
            add_type(die, *type.element);
            Die range{dw::tag_subrange_type, {}, {}};
            if (type.lanes != 0 && !type.scalable)
                range.attributes.push_back({dw::at_count, dw::form_udata, udata(type.lanes)});
            die.children.push_back(std::move(range));
            break;
        }
        case hir::Type::Kind::Record:
            path = record(die, *type.record);
            break;
        case hir::Type::Kind::Function: {
            die.tag = dw::tag_subroutine_type;
            const auto& signature = *type.function;
            add_type(die, signature.result_type);
            for (const auto& parameter : signature.parameters) {
                Die item{dw::tag_formal_parameter, {}, {}};
                add_type(item, parameter.type);
                die.children.push_back(std::move(item));
            }
            if (signature.variadic)
                die.children.push_back({dw::tag_unspecified_parameters, {}, {}});
            break;
        }
        }
        scopes_.add(path, std::move(die));
        return label;
    }

    void base(Die& die, BuiltinType builtin, hir::TypeId id) {
        static constexpr std::string_view names[] = {
            "void", "bool", "i8", "u8", "i16", "u16", "i32", "u32",
            "i64", "u64", "i128", "u128", "iptr", "uptr", "f32",
            "f64", "f80", "f128", "fptr", "label",
        };
        unsigned encoding = dw::ate_signed;
        switch (builtin) {
        case BuiltinType::Bool: encoding = dw::ate_boolean; break;
        case BuiltinType::I8: encoding = dw::ate_signed_char; break;
        case BuiltinType::U8: encoding = dw::ate_unsigned_char; break;
        case BuiltinType::U16: case BuiltinType::U32: case BuiltinType::U64:
        case BuiltinType::U128: case BuiltinType::Uptr:
            encoding = dw::ate_unsigned;
            break;
        case BuiltinType::F32: case BuiltinType::F64: case BuiltinType::F80:
        case BuiltinType::F128: case BuiltinType::Fptr:
            encoding = dw::ate_float;
            break;
        case BuiltinType::Label: encoding = dw::ate_address; break;
        default: break;
        }
        die.attributes.push_back({dw::at_name, dw::form_strp,
                                  writer_.string(std::string(names[static_cast<unsigned>(builtin)]))});
        die.attributes.push_back({dw::at_encoding, dw::form_data1,
                                  "\t.byte " + std::to_string(encoding) + '\n'});
        die.attributes.push_back({dw::at_byte_size, dw::form_udata, udata(size(id))});
    }

    std::vector<std::string> enumeration(Die& die, const hir::Type& type,
                                         hir::TypeId id) {
        auto path = name_components(type.nominal_name);
        const auto name = path.back();
        path.pop_back();
        die.tag = dw::tag_enumeration_type;
        die.attributes.push_back({dw::at_name, dw::form_strp, writer_.string(name)});
        die.attributes.push_back({dw::at_byte_size, dw::form_udata, udata(size(id))});
        if (const auto underlying = module_.builtin(type.builtin))
            add_type(die, *underlying);
        const auto found = std::find_if(
            enumerations_.begin(), enumerations_.end(),
            [&](const EnumDecl& candidate) {
                return candidate.nominal_key() == type.nominal_key();
            });
        if (found == enumerations_.end()) return path;
        const bool is_signed = type.builtin == BuiltinType::I8 ||
            type.builtin == BuiltinType::I16 || type.builtin == BuiltinType::I32 ||
            type.builtin == BuiltinType::I64 || type.builtin == BuiltinType::I128 ||
            type.builtin == BuiltinType::Iptr;
        for (const auto& enumerator : found->enumerators) {
            if (!enumerator.value) continue;
            Die item{dw::tag_enumerator, {}, {}};
            item.attributes.push_back({dw::at_name, dw::form_strp,
                                       writer_.string(enumerator.name)});
            const auto low = enumerator.value->value.low;
            if (is_signed) {
                item.attributes.push_back({dw::at_const_value, dw::form_sdata,
                                           "\t.sleb128 " +
                                               std::to_string(static_cast<std::int64_t>(low)) + '\n'});
            } else {
                item.attributes.push_back({dw::at_const_value, dw::form_udata, udata(low)});
            }
            die.children.push_back(std::move(item));
        }
        return path;
    }

    std::vector<std::string> record(Die& die, hir::RecordId id) {
        const auto& source = module_.record(id);
        auto path = name_components(source.source_name);
        const auto name = path.back();
        path.pop_back();
        die.tag = source.is_union ? dw::tag_union_type : dw::tag_structure_type;
        if (!name.empty())
            die.attributes.push_back({dw::at_name, dw::form_strp, writer_.string(name)});
        if (!source.complete) {
            die.attributes.push_back({dw::at_declaration, dw::form_flag_present, {}});
            return path;
        }
        die.attributes.push_back({dw::at_byte_size, dw::form_udata, udata(source.size)});
        const bool big = target_.data_layout.byte_order == ByteOrder::Big;
        for (const auto& member : source.members) {
            // Big-endian bit-field numbering is not described.
            if (member.bit_width && (*member.bit_width == 0 || big)) continue;
            Die item{dw::tag_member, {}, {}};
            if (!member.name.empty())
                item.attributes.push_back({dw::at_name, dw::form_strp,
                                           writer_.string(member.name)});
            add_type(item, member.type);
            if (member.bit_width) {
                item.attributes.push_back({dw::at_bit_size, dw::form_udata,
                                           udata(*member.bit_width)});
                item.attributes.push_back({dw::at_data_bit_offset, dw::form_udata,
                                           udata(member.offset * 8U + member.bit_offset)});
            } else {
                item.attributes.push_back({dw::at_data_member_location, dw::form_udata,
                                           udata(member.offset)});
            }
            die.children.push_back(std::move(item));
        }
        return path;
    }

    Writer& writer_;
    Scopes& scopes_;
    const hir::Module& module_;
    const TargetInfo& target_;
    std::span<const EnumDecl> enumerations_;
    std::unordered_map<std::uint32_t, std::string> qualified_;
    std::map<std::string, std::string> core_;
    std::size_t next_{};
};

class Abbreviations {
public:
    unsigned code(const Die& die) {
        std::vector<unsigned> key{die.tag, die.children.empty() ? 0U : 1U};
        for (const auto& attribute : die.attributes) {
            key.push_back(attribute.name);
            key.push_back(attribute.form);
        }
        auto [found, inserted] = codes_.try_emplace(
            key, static_cast<unsigned>(order_.size() + 1));
        if (inserted) order_.push_back(key);
        return found->second;
    }

    [[nodiscard]] std::string table() const {
        std::ostringstream output;
        for (std::size_t index = 0; index < order_.size(); ++index) {
            const auto& key = order_[index];
            output << "\t.uleb128 " << index + 1 << "\n\t.uleb128 " << key[0]
                   << "\n\t.byte " << key[1] << '\n';
            for (std::size_t item = 2; item < key.size(); item += 2) {
                output << "\t.uleb128 " << key[item] << "\n\t.uleb128 "
                       << key[item + 1] << '\n';
            }
            output << "\t.byte 0\n\t.byte 0\n";
        }
        output << "\t.byte 0\n";
        return output.str();
    }

private:
    std::map<std::vector<unsigned>, unsigned> codes_;
    std::vector<std::vector<unsigned>> order_;
};

void write_die(std::ostringstream& output, const Die& die,
               Abbreviations& abbreviations) {
    if (!die.label.empty()) output << die.label << ":\n";
    output << "\t.uleb128 " << abbreviations.code(die) << '\n';
    for (const auto& attribute : die.attributes) output << attribute.value;
    if (die.children.empty()) return;
    for (const auto& child : die.children) write_die(output, child, abbreviations);
    output << "\t.byte 0\n";
}

// The slots that MIR still accesses, so that their homes hold the variables.
std::vector<bool> used_slots(const mir::ManagedFunction& function) {
    std::vector<bool> result(function.slots.size());
    for (const auto& value : function.values) {
        if (value.slot && value.slot->value < result.size() &&
            value.kind != mir::ValueKind::LifetimeStart &&
            value.kind != mir::ValueKind::LifetimeEnd)
            result[value.slot->value] = true;
    }
    return result;
}

} // namespace

DebugInfo::DebugInfo(const CompilerOptions& options, const hir::Module& module,
                     const mir::ManagedModule& managed,
                     std::span<const EnumDecl> enumerations,
                     const Subtarget& subtarget)
    : options_(options), module_(module), managed_(managed),
      enumerations_(enumerations), subtarget_(subtarget),
      format_(subtarget.object_format()),
      unit_(options.inputs.empty()
                ? std::string()
                : mapped_source_path(options, options.inputs.front())) {}

void DebugInfo::begin_function() {
    last_row_.reset();
    prologue_end_ = false;
    homes_.clear();
    parameter_homes_.clear();
}

std::optional<DebugInfo::Position> DebugInfo::position(SourceLocation location) {
    const auto place = written(location);
    if (!place.valid() || place.line == 0) return std::nullopt;
    return Position{file_number(mapped_source_path(options_, place.file->path)),
                    place.line, place.column};
}

unsigned DebugInfo::file_number(const std::string& path) {
    if (path == unit_) return 0;
    auto [found, inserted] = file_numbers_.try_emplace(
        path, static_cast<unsigned>(files_.size() + 1));
    if (inserted) files_.push_back(path);
    return found->second;
}

void DebugInfo::row(std::ostream& output, SourceLocation location) {
    if (!lines()) return;
    const auto current = position(location);
    if (!current || current == last_row_) return;
    output << ".loc " << current->file << ' ' << current->line << ' '
           << current->column << (prologue_end_ ? " prologue_end\n" : "\n");
    last_row_ = current;
    prologue_end_ = false;
    rows_ = true;
}

void DebugInfo::slot_home(std::uint32_t slot, std::string name, unsigned reg,
                          std::int64_t offset, bool in_register) {
    homes_[slot] = {std::move(name), reg, offset, in_register};
}

void DebugInfo::parameter_home(std::uint64_t index, unsigned reg,
                               std::int64_t offset) {
    parameter_homes_[index] = {{}, reg, offset, false};
}

std::string DebugInfo::end_function(hir::FunctionId function, std::string symbol,
                                    bool frame) {
    auto end = (format_ == ObjectFormat::MachO ? "L" : ".L") +
               std::string("cross.debug.end.") + std::to_string(function.value);
    functions_.push_back({function, std::move(symbol), end, frame,
                          std::move(homes_), std::move(parameter_homes_)});
    homes_.clear();
    parameter_homes_.clear();
    return end;
}

std::string DebugInfo::finish(std::string assembly) {
    if (!active()) return assembly;
    const auto& entry = *options_.debug_info;
    Writer writer(format_, (module_.address_bits + 7U) / 8U);
    Scopes scopes;
    Types types(writer, scopes, module_, subtarget_.target(), enumerations_);
    const bool line_table = rows_;

    Die unit{dw::tag_compile_unit, {}, {}};
    unit.attributes.push_back({dw::at_producer, dw::form_strp,
                               writer.string(std::string(toolchain_version()))});
    unit.attributes.push_back({dw::at_language, dw::form_data2,
                               "\t.short " + std::to_string(dw::lang_c_plus_plus_14) + '\n'});
    unit.attributes.push_back({dw::at_name, dw::form_strp, writer.string(unit_)});
    unit.attributes.push_back({dw::at_comp_dir, dw::form_strp, writer.string(".")});
    if (line_table) {
        unit.attributes.push_back({dw::at_stmt_list, dw::form_sec_offset,
                                   writer.offset(writer.label("line"), writer.label("line"))});
    }
    if (!functions_.empty()) {
        unit.attributes.push_back({dw::at_low_pc, dw::form_addr, writer.address("0")});
        unit.attributes.push_back({dw::at_ranges, dw::form_sec_offset,
                                   writer.offset(writer.label("ranges"),
                                                 writer.label("rnglists"))});
    }

    const auto add_type = [&](Die& die, hir::TypeId type) {
        if (!entry.types) return;
        if (auto attribute = types.attribute(type)) die.attributes.push_back(std::move(*attribute));
    };
    const auto add_declaration = [&](Die& die, SourceLocation location) {
        if (!line_table) return;
        if (const auto place = position(location)) {
            die.attributes.push_back({dw::at_decl_file, dw::form_udata, udata(place->file)});
            die.attributes.push_back({dw::at_decl_line, dw::form_udata, udata(place->line)});
        }
    };
    const auto add_location = [&](Die& die, const Home& home) {
        if (home.reg >= 32) return;
        std::vector<std::uint8_t> bytes;
        if (home.in_register) {
            bytes.push_back(static_cast<std::uint8_t>(dw::op_reg0 + home.reg));
        } else {
            bytes.push_back(static_cast<std::uint8_t>(dw::op_breg0 + home.reg));
            append_sleb(bytes, home.offset);
        }
        die.attributes.push_back({dw::at_location, dw::form_exprloc, expression(bytes)});
    };
    const auto add_home = [&](Die& die, const Function& function,
                              const mir::ManagedSlot& slot) {
        const auto found = function.homes.find(slot.id.value);
        if (found != function.homes.end() && found->second.name == slot.name)
            add_location(die, found->second);
    };

    for (const auto& function : functions_) {
        const auto& entity = module_.function(function.source);
        auto path = name_components(source_name(entity));
        const auto name = path.back();
        path.pop_back();
        Die die{dw::tag_subprogram, {}, {}};
        die.attributes.push_back({dw::at_low_pc, dw::form_addr,
                                  writer.address(function.symbol)});
        die.attributes.push_back({dw::at_high_pc, dw::form_data4,
                                  "\t.long " + function.end_label + '-' +
                                      function.symbol + '\n'});
        if (function.frame) {
            die.attributes.push_back({dw::at_frame_base, dw::form_exprloc,
                                      expression({static_cast<std::uint8_t>(dw::op_call_frame_cfa)})});
        }
        if (entity.link_symbol != name) {
            die.attributes.push_back({dw::at_linkage_name, dw::form_strp,
                                      writer.string(entity.link_symbol)});
        }
        die.attributes.push_back({dw::at_name, dw::form_strp, writer.string(name)});
        add_declaration(die, entity.location);
        add_type(die, entity.result_type);
        if (entity.linkage == Linkage::Global) {
            die.attributes.push_back({dw::at_external, dw::form_flag_present, {}});
        }
        if (entry.variables) {
            // Parameters, then the locals with source names. A variable whose
            // slot MIR no longer accesses, or that has no home, has no
            // location.
            const auto* body = managed_.find(function.source);
            const auto used = body ? used_slots(*body) : std::vector<bool>{};
            for (std::size_t index = 0; index < entity.parameters.size(); ++index) {
                const auto& parameter = entity.parameters[index];
                Die item{dw::tag_formal_parameter, {}, {}};
                item.attributes.push_back({dw::at_name, dw::form_strp,
                                           writer.string(parameter.name)});
                add_declaration(item, parameter.location);
                add_type(item, parameter.type);
                // A parameter cell holds the current value; an `in` value
                // without one keeps the value's own home.
                const mir::ManagedSlot* cell{};
                for (std::size_t slot = 0; body && !cell && slot < body->slots.size(); ++slot) {
                    if (body->slots[slot].source_parameter == index && used[slot])
                        cell = &body->slots[slot];
                }
                if (cell) {
                    add_home(item, function, *cell);
                } else if (const auto found = function.parameter_homes.find(index);
                           parameter.mode == ParameterMode::In &&
                           found != function.parameter_homes.end()) {
                    add_location(item, found->second);
                }
                die.children.push_back(std::move(item));
            }
            for (std::uint32_t slot = 0; body && slot < body->slots.size(); ++slot) {
                const auto& local = body->slots[slot];
                if (local.source_parameter || local.name.empty() ||
                    local.name.front() == '$')
                    continue;
                Die item{dw::tag_variable, {}, {}};
                item.attributes.push_back({dw::at_name, dw::form_strp,
                                           writer.string(local.name)});
                add_declaration(item, local.location);
                add_type(item, local.type);
                if (used[slot]) add_home(item, function, local);
                die.children.push_back(std::move(item));
            }
        }
        scopes.add(path, std::move(die));
    }

    if (entry.variables) {
        for (const auto& object : module_.objects) {
            if (!object.definition || object.is_thread_local ||
                object.alias_target || object.weakref_target)
                continue;
            auto path = name_components(object.source_name);
            const auto name = path.back();
            path.pop_back();
            Die die{dw::tag_variable, {}, {}};
            die.attributes.push_back({dw::at_name, dw::form_strp, writer.string(name)});
            add_declaration(die, object.location);
            add_type(die, object.type);
            if (object.linkage == Linkage::Global)
                die.attributes.push_back({dw::at_external, dw::form_flag_present, {}});
            const auto address = object.fixed_address
                ? std::to_string(*object.fixed_address)
                : assembly_symbol(format_, object.link_symbol);
            die.attributes.push_back({dw::at_location, dw::form_exprloc,
                                      udata(1 + writer.address_bytes()) + "\t.byte " +
                                          std::to_string(dw::op_addr) + '\n' +
                                          writer.address(address)});
            scopes.add(path, std::move(die));
        }
    }
    scopes.build(writer, {}, unit);

    Abbreviations abbreviations;
    std::ostringstream info;
    write_die(info, unit, abbreviations);

    std::ostringstream sections;
    const auto abbrev = writer.label("abbrev");
    sections << writer.section("debug_abbrev") << abbrev << ":\n"
             << abbreviations.table();
    const auto info_start = writer.label("info");
    const auto info_begin = writer.label("info.begin");
    const auto info_end = writer.label("info.end");
    sections << writer.section("debug_info") << info_start << ":\n"
             << "\t.long " << info_end << '-' << info_begin << '\n'
             << info_begin << ":\n\t.short 5\n\t.byte " << dw::ut_compile
             << "\n\t.byte " << writer.address_bytes() << '\n'
             << writer.offset(abbrev, abbrev) << info.str() << info_end << ":\n";
    if (!functions_.empty()) {
        const auto start = writer.label("rnglists");
        const auto begin = writer.label("rnglists.begin");
        const auto end = writer.label("rnglists.end");
        sections << writer.section("debug_rnglists") << start << ":\n"
                 << "\t.long " << end << '-' << begin << '\n' << begin
                 << ":\n\t.short 5\n\t.byte " << writer.address_bytes()
                 << "\n\t.byte 0\n\t.long 0\n" << writer.label("ranges") << ":\n";
        for (const auto& function : functions_) {
            sections << "\t.byte " << dw::rle_start_length << '\n'
                     << writer.address(function.symbol) << "\t.uleb128 "
                     << function.end_label << '-' << function.symbol << '\n';
        }
        sections << "\t.byte " << dw::rle_end_of_list << '\n' << end << ":\n";
    }
    sections << writer.strings();
    if (line_table) {
        sections << writer.section("debug_line") << writer.label("line") << ":\n";
    }

    std::string header;
    if (line_table) {
        header += ".file 0 \".\" " + assembly_string(unit_) + '\n';
        for (std::size_t index = 0; index < files_.size(); ++index) {
            header += ".file " + std::to_string(index + 1) + ' ' +
                      assembly_string(files_[index]) + '\n';
        }
    }
    if (frames() && !entry.eh_frame) {
        const bool unwind_tables =
            (options_.unwind_tables || options_.asynchronous_unwind_tables) &&
            assembly_uses_dwarf_cfi(format_);
        header += unwind_tables ? ".cfi_sections .eh_frame, .debug_frame\n"
                                : ".cfi_sections .debug_frame\n";
    }
    return header + assembly + sections.str();
}

} // namespace cross::native

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Prints how ABI models place example signatures and compares the listing
// with an expected one.
//
//   abi_layouts aarch64 EXPECTED          the shipped AArch64 ABIs
//   abi_layouts custom MODEL EXPECTED     the ABIs of a custom model
//
// A further argument --write replaces EXPECTED with the listing.
#include "common/diagnostic.hpp"
#include "model/model.hpp"
#include "target/abi_lowering.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace cross;

// A C-like type with the layout that the compiler gives it on a 64-bit
// target whose largest natural alignment is 16 bytes.
struct Type {
    AbiValue value;
    std::uint32_t size{};
    std::uint32_t align{};
    std::string text;
};

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1U) / alignment * alignment;
}

Type scalar(ScalarMode mode, std::string text) {
    Type result;
    result.value.mode = mode;
    result.size = std::max<std::uint32_t>(1, mode.bits / 8U);
    result.align = std::min<std::uint32_t>(result.size, 16);
    result.value.alignment_bits = static_cast<std::uint16_t>(result.align * 8U);
    result.text = std::move(text);
    return result;
}

Type integer(std::uint16_t bits) {
    return scalar(ScalarMode::integer(bits), "i" + std::to_string(bits));
}

Type floating(std::uint16_t bits) {
    return scalar(ScalarMode::floating(bits), "f" + std::to_string(bits));
}

Type pointer() { return scalar(ScalarMode::pointer(64), "ptr"); }

Type vector(const Type& element, std::uint16_t lanes) {
    return scalar(ScalarMode::vector(static_cast<std::uint16_t>(element.size * 8U * lanes)),
                  element.text + "x" + std::to_string(lanes));
}

Type record(std::initializer_list<Type> fields, std::uint32_t request = 0,
            bool is_union = false) {
    Type result;
    std::uint32_t end{};
    result.align = std::max<std::uint32_t>(1, request);
    result.text = is_union ? "union {" : "{";
    for (const auto& field : fields) {
        const auto offset = is_union ? 0U : align_up(end, field.align);
        result.value.elements.push_back(field.value);
        result.value.element_offsets_bits.push_back(offset * 8U);
        end = std::max(end, offset + field.size);
        result.align = std::max(result.align, field.align);
        if (result.text.size() > (is_union ? 7U : 1U)) result.text += ", ";
        result.text += field.text;
    }
    result.text += "}";
    if (request) result.text += " aligned " + std::to_string(request);
    result.size = align_up(end, result.align);
    result.value.mode = ScalarMode::aggregate(static_cast<std::uint16_t>(result.size * 8U));
    result.value.alignment_bits = static_cast<std::uint16_t>(result.align * 8U);
    result.value.union_value = is_union;
    return result;
}

Type union_of(std::initializer_list<Type> fields) {
    return record(fields, 0, true);
}

Type array(const Type& element, std::uint32_t count) {
    Type result;
    result.size = element.size * count;
    result.align = element.align;
    result.value.mode = ScalarMode::array(static_cast<std::uint16_t>(result.size * 8U));
    result.value.alignment_bits = static_cast<std::uint16_t>(result.align * 8U);
    result.value.element_count = count;
    result.value.elements = {element.value};
    result.text = element.text + "[" + std::to_string(count) + "]";
    return result;
}

std::vector<Type> repeat(const Type& type, std::size_t count) {
    return std::vector<Type>(count, type);
}

std::vector<Type> join(std::initializer_list<std::vector<Type>> parts) {
    std::vector<Type> result;
    for (const auto& part : parts) result.insert(result.end(), part.begin(), part.end());
    return result;
}

std::string place(const ValuePiece& piece, unsigned value_bits) {
    std::string text = piece.location.kind == LocationKind::Register
                           ? piece.location.reg
                           : "sp+" + std::to_string(piece.location.stack_offset);
    if (piece.indirect_value_bits != 0) return "&" + text;
    if (piece.value_bit_offset != 0 || piece.value_bits != value_bits) {
        text += "[" + std::to_string(piece.value_bit_offset) + ":" +
                std::to_string(piece.value_bit_offset + piece.value_bits) + "]";
    }
    return text;
}

std::string places(const std::vector<ValuePiece>& pieces, unsigned value_bits) {
    if (pieces.empty()) return "nothing";
    std::string text;
    for (const auto& piece : pieces) {
        if (!text.empty()) text += " ";
        text += place(piece, value_bits);
    }
    return text;
}

class Listing {
public:
    Listing(const ModelRegistry& registry, std::string_view architecture,
            std::string triple)
        : registry_(registry), architecture_(architecture), triple_(std::move(triple)) {}

    void features(std::vector<std::string> names) { features_ = std::move(names); }

    // A signature; `fixed` is the number of named arguments of a variadic
    // one.
    void signature(std::string_view abi_name, const std::vector<Type>& arguments,
                   const std::optional<Type>& result = std::nullopt,
                   std::optional<std::size_t> fixed = std::nullopt) {
        const auto* abi = registry_.find_abi(architecture_, abi_name, triple_);
        out_ << abi_name << ": (";
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (index != 0) out_ << ", ";
            if (fixed && index == *fixed) out_ << "... ";
            out_ << arguments[index].text;
        }
        if (fixed && *fixed == arguments.size()) out_ << (arguments.empty() ? "..." : ", ...");
        out_ << ") -> " << (result ? result->text : "void") << '\n';
        if (!abi) {
            out_ << "  no such ABI\n";
            return;
        }
        std::vector<AbiValue> values;
        for (const auto& argument : arguments) values.push_back(argument.value);
        std::vector<AbiValue> results;
        if (result) results.push_back(result->value);
        const auto classified =
            fixed ? classify_variadic_signature(*abi, values, results, *fixed, features_)
                  : classify_signature(*abi, values, results, features_);
        if (!classified) {
            out_ << "  rejected: " << (classified.error_in_results ? "result" : "argument")
                 << ' ' << classified.error_value << '\n';
            return;
        }
        const auto& layout = classified.layout;
        for (std::size_t index = 0; index < layout.call.arguments.size(); ++index) {
            const auto& argument = layout.call.arguments[index];
            out_ << "  " << index << ": "
                 << places(argument.pieces, argument.value.mode.bits) << '\n';
        }
        for (const auto& assignment : layout.results) {
            out_ << "  result: " << places(assignment.pieces, assignment.value.mode.bits);
            if (!assignment.indirect_result_reg.empty()) {
                out_ << ", address returned in " << assignment.indirect_result_reg;
            }
            out_ << '\n';
        }
        if (layout.call.outgoing_area_size != 0) {
            out_ << "  stack: " << layout.call.outgoing_area_size << " bytes\n";
        }
        if (!fixed) return;
        for (const auto& state : abi->variadic_states) {
            if (state.kind == AbiVariadicStateKind::RecordAddress) continue;
            const auto value = variadic_state_value(state, layout.call.named_cursors,
                                                    layout.call.variadic_stack_offset);
            out_ << "  " << state.canonical_name << ": ";
            switch (value.base) {
            case VariadicStateBase::None: out_ << value.offset; break;
            case VariadicStateBase::IncomingArguments: out_ << "arguments" << signed_offset(value.offset); break;
            case VariadicStateBase::SaveArea: out_ << "saved" << signed_offset(value.offset); break;
            }
            out_ << '\n';
        }
    }

    // The registers that calls preserve, whole or in part.
    void preserved(std::string_view abi_name) {
        const auto* abi = registry_.find_abi(architecture_, abi_name, triple_);
        const auto* target = target_for_triple(triple_);
        out_ << abi_name << " preserves:";
        const auto clobbered = [&](const RegisterEntry& reg) {
            return std::any_of(abi->call_clobbers.begin(), abi->call_clobbers.end(),
                               [&](const std::string& name) {
                                   const auto* entry = find_register(*target, name);
                                   return entry && entry->storage == reg.storage;
                               });
        };
        std::vector<std::string_view> seen;
        for (const auto& reg : target->registers) {
            // The zero register keeps nothing.
            if (reg.name != reg.storage || clobbered(reg) ||
                (!reg.instruction_writable && !reg.compiler_owned) ||
                std::find(seen.begin(), seen.end(), reg.storage) != seen.end()) {
                continue;
            }
            seen.push_back(reg.storage);
            std::string_view shown = reg.name;
            for (const auto& view : abi->call_preserved_views) {
                const auto* entry = find_register(*target, view);
                if (entry && entry->storage == reg.storage) shown = entry->name;
            }
            out_ << ' ' << shown;
        }
        out_ << '\n';
    }

    void note(std::string_view text) { out_ << '\n' << "# " << text << '\n'; }

    void va_list(std::string_view abi_name) {
        const auto* abi = registry_.find_abi(architecture_, abi_name, triple_);
        for (const auto& state : abi->variadic_states) {
            if (state.kind != AbiVariadicStateKind::RecordAddress) continue;
            out_ << abi_name << ' ' << state.canonical_name << ": " << state.record_bytes
                 << " bytes, aligned " << state.record_alignment << ":";
            for (const auto& field : state.fields) {
                out_ << ' ' << abi->variadic_states[field.state.value].canonical_name
                     << '@' << field.offset;
            }
            out_ << '\n';
        }
    }

    [[nodiscard]] std::string text() const { return out_.str(); }

private:
    static std::string signed_offset(std::int64_t value) {
        return value < 0 ? std::to_string(value) : "+" + std::to_string(value);
    }

    const ModelRegistry& registry_;
    std::string_view architecture_;
    std::string triple_;
    std::vector<std::string> features_;
    std::ostringstream out_;
};

std::string aarch64_listing(const ModelRegistry& registry) {
    Listing list(registry, "aarch64", "aarch64-unknown-linux-gnu");
    list.features({"armv8-a", "fp", "simd"});
    const auto i8 = integer(8), i16 = integer(16), i32 = integer(32),
               i64 = integer(64), i128 = integer(128);
    const auto f32 = floating(32), f64 = floating(64), f128 = floating(128);
    const auto ptr = pointer();
    const auto f32x2 = vector(f32, 2), f32x4 = vector(f32, 4),
               i32x4 = vector(i32, 4), i16x2 = vector(i16, 2),
               f32x8 = vector(f32, 8);

    list.preserved("aapcs64");
    list.preserved("x");
    list.va_list("aapcs64");

    list.note("AAPCS64 scalars: x0-x7 and v0-v7, then 8-byte stack slots.");
    list.signature("aapcs64", {i8, i16, i32, i64, ptr}, i32);
    list.signature("aapcs64", repeat(i64, 9), i64);
    list.signature("aapcs64", {f32, f64, f128}, f64);
    list.signature("aapcs64", repeat(f64, 9));
    list.signature("aapcs64", {i32, f64, i32, f32});
    list.signature("aapcs64", join({repeat(i64, 8), {i32, i128}}));
    list.signature("aapcs64", join({repeat(f64, 8), {f32, f128}}), f128);

    list.note("128-bit integers: an even register pair, or the stack once x7 "
              "would hold the first half.");
    list.signature("aapcs64", {i32, i128}, i128);
    list.signature("aapcs64", join({repeat(i64, 7), {i128, i64}}));

    list.note("Homogeneous floating-point and short-vector aggregates.");
    const auto hfa2 = record({f32, f32});
    const auto hfa3 = record({f32, f32, f32});
    const auto hfa4d = record({f64, f64, f64, f64});
    list.signature("aapcs64", {hfa2, hfa3}, hfa3);
    list.signature("aapcs64", {hfa4d}, hfa4d);
    list.signature("aapcs64", {record({array(f32, 3)})}, record({array(f32, 3)}));
    list.signature("aapcs64", {record({f32, record({f32, f32})})});
    list.signature("aapcs64", {union_of({f32, array(f32, 2)})});
    list.signature("aapcs64", {record({f128, f128})}, record({f128, f128}));
    list.signature("aapcs64", {record({f32x4, f32x4})}, record({f32x4, i32x4}));
    list.signature("aapcs64", {record({f32x2, f32x2, f32x2, f32x2})});
    list.signature("aapcs64", {record({array(record({f32, f32}), 2)})});
    list.note("Not homogeneous: five members, mixed types, mixed vector sizes, "
              "padding.");
    list.signature("aapcs64", {record({f32, f32, f32, f32, f32})},
                   record({f32, f32, f32, f32, f32}));
    list.signature("aapcs64", {record({f32, f64})}, record({f32, f64}));
    list.signature("aapcs64", {record({f64, i64})});
    list.signature("aapcs64", {record({f64, f32x2})});
    list.signature("aapcs64", {record({f32x2, f32x4})});
    list.signature("aapcs64", {record({f32, f32}, 16)});
    list.signature("aapcs64", {union_of({f32, f64})});
    list.note("Running out of SIMD and floating-point registers: the aggregate "
              "and every later floating value go on the stack.");
    list.signature("aapcs64", join({repeat(f64, 6), {record({f64, f64, f64}), f64}}));
    list.signature("aapcs64", join({repeat(f32, 8), {hfa3, f32}}));
    list.note("On the stack, an HVA aligned to 32 bytes starts at a 16-byte "
              "boundary.");
    list.signature("aapcs64", join({repeat(f32x4, 8), {f64, record({f32x4, f32x4}, 32)}}));

    list.note("Other records: up to 16 bytes in general registers, an even "
              "pair when 16-byte aligned; larger by reference.");
    list.signature("aapcs64", {record({i8})}, record({i8}));
    list.signature("aapcs64", {record({i32, i32, i32})}, record({i32, i32, i32}));
    list.signature("aapcs64", {i32, record({i128})}, record({i128}));
    list.signature("aapcs64", {record({i64, i64, i64})}, record({i64, i64, i64}));
    list.signature("aapcs64", {i64}, record({i64, i64, i64}));
    list.signature("aapcs64", join({repeat(i64, 7), {record({i64, i64}), i64}}));
    list.signature("aapcs64", join({repeat(i64, 8), {record({i64, i64, i64})}}));
    list.signature("aapcs64", {array(i32, 3)});

    list.note("Vectors: 64 and 128 bits in SIMD registers, others like records.");
    list.signature("aapcs64", {f32x2, f32x4}, f32x4);
    list.signature("aapcs64", {i16x2}, i16x2);
    list.signature("aapcs64", {f32x8}, f32x8);

    list.note("Variadic calls: unnamed arguments travel as named ones do.");
    list.signature("aapcs64", {ptr, i32, f64}, i32, 1);
    list.signature("aapcs64", join({repeat(i64, 8), {f64, hfa2, i64}}), std::nullopt, 9);
    list.signature("aapcs64", join({repeat(f64, 8), {i32, f64}}), std::nullopt, 9);
    list.signature("aapcs64", join({{record({f32, f32, f32})}, repeat(i64, 8), {i64}}),
                   std::nullopt, 10);

    list.note("Without floating-point registers, aapcs64 has no location for "
              "a floating value, and x passes it in general registers.");
    list.features({"armv8-a"});
    list.signature("aapcs64", {f64});
    list.signature("x", {f64, f32}, f64);
    list.features({"armv8-a", "fp", "simd"});

    list.note("The Cross ABI x.");
    list.signature("x", repeat(i64, 20), i64);
    list.signature("x", repeat(f64, 10), f64);
    list.signature("x", {i128, f128}, i128);
    list.signature("x", {hfa3, record({f64, i64})}, hfa3);
    list.signature("x", {record({i64, i64, i64, i64, i64})},
                   record({i64, i64, i64, i64, i64}));
    list.signature("x", {f32x8}, f32x8);
    list.signature("x", {ptr, i32, f64}, std::nullopt, 1);
    return list.text();
}

std::string custom_listing(const ModelRegistry& registry) {
    Listing list(registry, "x86-64", "x86_64-unknown-linux-gnu");
    list.features({"base"});
    const auto i64 = integer(64), f32 = floating(32), f64 = floating(64);
    list.preserved("homogeneous_test");
    list.note("uniform_members and max_members.");
    list.signature("homogeneous_test", {record({f32, f32, f32})}, record({f32, f32, f32}));
    list.signature("homogeneous_test", {union_of({f32, array(f32, 2)})});
    list.signature("homogeneous_test", {record({f32, f32, f32, f32, f32})});
    list.signature("homogeneous_test", {record({f32, f64})});
    list.note("exhaust: no later value takes a register of the cursor.");
    list.signature("homogeneous_test", join({repeat(f64, 3), {record({f64, f64}), f64}}));
    list.signature("homogeneous_test", join({{i64}, {record({i64, i64}), i64}}));
    list.note("The same signature with stack instead of exhaust.");
    list.signature("homogeneous_stack", join({repeat(f64, 3), {record({f64, f64}), f64}}));
    return list.text();
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const bool write = !args.empty() && args.back() == "--write";
    const auto count = args.size() - (write ? 1U : 0U);
    std::string listing;
    std::filesystem::path expected;
    if (count == 2 && args[0] == "aarch64") {
        listing = aarch64_listing(model_registry());
        expected = args[1];
    } else if (count == 3 && args[0] == "custom") {
        Diagnostics diagnostics(std::cerr);
        ModelRegistry registry;
        if (!registry.load_file(args[1], diagnostics)) return 1;
        listing = custom_listing(registry);
        expected = args[2];
    } else {
        std::cerr << "usage: abi_layouts aarch64 EXPECTED | custom MODEL EXPECTED [--write]\n";
        return 2;
    }
    if (write) {
        std::ofstream(expected, std::ios::binary) << listing;
        return 0;
    }
    std::ifstream input(expected, std::ios::binary);
    std::ostringstream want;
    want << input.rdbuf();
    if (want.str() == listing) return 0;
    std::istringstream got_lines(listing), want_lines(want.str());
    std::string got_line, want_line;
    for (unsigned line = 1;; ++line) {
        const bool more_got = static_cast<bool>(std::getline(got_lines, got_line));
        const bool more_want = static_cast<bool>(std::getline(want_lines, want_line));
        if (!more_got && !more_want) break;
        if (more_got && more_want && got_line == want_line) continue;
        std::cerr << expected.string() << ':' << line << ": expected '"
                  << (more_want ? want_line : "<end>") << "', got '"
                  << (more_got ? got_line : "<end>") << "'\n";
        break;
    }
    std::cerr << "full listing:\n" << listing;
    return 1;
}

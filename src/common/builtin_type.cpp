// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/builtin_type.hpp"

#include <algorithm>
#include <utility>

namespace cross {

std::optional<BuiltinType> builtin_kind(std::string_view spelling) {
    static constexpr std::pair<std::string_view, BuiltinType> types[] = {
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
    for (const auto& [name, kind] : types) if (spelling == name) return kind;
    return std::nullopt;
}

std::optional<std::uint64_t> builtin_storage_size(BuiltinType type, unsigned address_bits,
                                                  unsigned f80_storage_bytes) {
    switch (type) {
    case BuiltinType::Void: return std::nullopt;
    case BuiltinType::Bool: case BuiltinType::I8: case BuiltinType::U8: return 1;
    case BuiltinType::I16: case BuiltinType::U16: return 2;
    case BuiltinType::I32: case BuiltinType::U32: case BuiltinType::F32: return 4;
    case BuiltinType::I64: case BuiltinType::U64: case BuiltinType::F64: return 8;
    case BuiltinType::I128: case BuiltinType::U128: case BuiltinType::F128: return 16;
    case BuiltinType::F80: return f80_storage_bytes;
    case BuiltinType::Iptr: case BuiltinType::Uptr: case BuiltinType::Fptr:
    case BuiltinType::Label: return (address_bits + 7U) / 8U;
    }
    return std::nullopt;
}

std::uint64_t natural_storage_alignment(std::uint64_t size, bool f80,
                                        unsigned alignment_limit, unsigned f80_alignment) {
    if (f80) return std::max(1U, f80_alignment);
    return std::max<std::uint64_t>(1, std::min<std::uint64_t>(size, alignment_limit));
}

} // namespace cross

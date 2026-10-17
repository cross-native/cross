// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace cross {

enum class BuiltinType {
    Void, Bool, I8, U8, I16, U16, I32, U32, I64, U64, I128, U128, Iptr, Uptr,
    F32, F64, F80, F128, Fptr, Label,
};

std::optional<BuiltinType> builtin_kind(std::string_view spelling);

// Natural storage of builtin scalars and fixed vectors, shared by target layout,
// translation-time evaluation, and model checks. Void has no storage.
std::optional<std::uint64_t> builtin_storage_size(BuiltinType type, unsigned address_bits,
                                                  unsigned f80_storage_bytes);
std::uint64_t natural_storage_alignment(std::uint64_t size, bool f80,
                                        unsigned alignment_limit, unsigned f80_alignment);

} // namespace cross

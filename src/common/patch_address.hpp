// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cross {

// A materializer/field must opt into a sink representation. FlatUptr promises
// that every cell address is losslessly representable by the selected model's
// uptr and an ordinary absolute object relocation in target byte order. It is
// not a fallback for targets with separate spaces, tags, or capabilities.
enum class PatchAddressRepresentation { Unavailable, FlatUptr };

// Encodings currently implemented by the native object emitter. Returning zero
// makes an unsupported width unavailable rather than truncating a relocation.
constexpr unsigned patch_address_storage_bytes(PatchAddressRepresentation representation,
                                                unsigned address_bits) {
    return representation == PatchAddressRepresentation::FlatUptr &&
        (address_bits == 32 || address_bits == 64) ? address_bits / 8 : 0;
}

} // namespace cross

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cross {

// Targets opt into observable numeric code addresses. Opaque still permits
// symbolic label identity, but does not invent a null encoding or address bits.
// Flat uses the resolved ABI address width, target byte order, an all-zero null,
// and bit-preserving explicit conversions between label and uptr.
enum class CodeAddressRepresentation { Opaque, Flat };

} // namespace cross

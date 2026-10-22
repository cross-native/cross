// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace cross::mips {

// MIPS ELF facts shared by the assembler and the linker: relocation types,
// section types and flags, header flags, and the instruction fields that
// relocations fill.

inline constexpr std::uint16_t em_mips = 8;

inline constexpr std::uint32_t r_mips_none = 0;
inline constexpr std::uint32_t r_mips_16 = 1;
inline constexpr std::uint32_t r_mips_32 = 2;
inline constexpr std::uint32_t r_mips_26 = 4;
inline constexpr std::uint32_t r_mips_hi16 = 5;
inline constexpr std::uint32_t r_mips_lo16 = 6;
inline constexpr std::uint32_t r_mips_gprel16 = 7;
inline constexpr std::uint32_t r_mips_literal = 8;
inline constexpr std::uint32_t r_mips_got16 = 9;
inline constexpr std::uint32_t r_mips_pc16 = 10;
inline constexpr std::uint32_t r_mips_call16 = 11;
inline constexpr std::uint32_t r_mips_gprel32 = 12;
inline constexpr std::uint32_t r_mips_64 = 18;
inline constexpr std::uint32_t r_mips_got_disp = 19;
inline constexpr std::uint32_t r_mips_got_page = 20;
inline constexpr std::uint32_t r_mips_got_ofst = 21;
inline constexpr std::uint32_t r_mips_got_hi16 = 22;
inline constexpr std::uint32_t r_mips_got_lo16 = 23;
inline constexpr std::uint32_t r_mips_sub = 24;
inline constexpr std::uint32_t r_mips_higher = 28;
inline constexpr std::uint32_t r_mips_highest = 29;
inline constexpr std::uint32_t r_mips_call_hi16 = 30;
inline constexpr std::uint32_t r_mips_call_lo16 = 31;
inline constexpr std::uint32_t r_mips_rel16 = 33;
inline constexpr std::uint32_t r_mips_jalr = 37;
inline constexpr std::uint32_t r_mips_tls_dtpmod32 = 38;
inline constexpr std::uint32_t r_mips_tls_dtprel32 = 39;
inline constexpr std::uint32_t r_mips_tls_dtpmod64 = 40;
inline constexpr std::uint32_t r_mips_tls_dtprel64 = 41;
inline constexpr std::uint32_t r_mips_tls_gd = 42;
inline constexpr std::uint32_t r_mips_tls_ldm = 43;
inline constexpr std::uint32_t r_mips_tls_dtprel_hi16 = 44;
inline constexpr std::uint32_t r_mips_tls_dtprel_lo16 = 45;
inline constexpr std::uint32_t r_mips_tls_gottprel = 46;
inline constexpr std::uint32_t r_mips_tls_tprel32 = 47;
inline constexpr std::uint32_t r_mips_tls_tprel64 = 48;
inline constexpr std::uint32_t r_mips_tls_tprel_hi16 = 49;
inline constexpr std::uint32_t r_mips_tls_tprel_lo16 = 50;
// Release 6 PC-relative fields.
inline constexpr std::uint32_t r_mips_pc21_s2 = 60;
inline constexpr std::uint32_t r_mips_pc26_s2 = 61;
inline constexpr std::uint32_t r_mips_pc18_s3 = 62;
inline constexpr std::uint32_t r_mips_pc19_s2 = 63;
inline constexpr std::uint32_t r_mips_pc32 = 248;

// The thread pointer (UserLocal, which `rdhwr $3, $29` reads) points 0x7000
// past the start of the static TLS block, and DTP-relative offsets count
// from 0x8000 past the start of a module's block, so that signed 16-bit
// offsets reach 64 KiB of TLS.
inline constexpr std::uint64_t tls_tp_offset = 0x7000;
inline constexpr std::uint64_t tls_dtp_offset = 0x8000;
// The gp of a GOT: 0x7ff0 past its start, so that signed 16-bit offsets
// reach 64 KiB of entries.
inline constexpr std::uint64_t got_gp_offset = 0x7ff0;

inline constexpr std::uint32_t sht_mips_reginfo = 0x70000006;
inline constexpr std::uint32_t sht_mips_options = 0x7000000d;
inline constexpr std::uint32_t sht_mips_debug = 0x70000005;
inline constexpr std::uint32_t sht_mips_dwarf = 0x7000001e;
inline constexpr std::uint32_t sht_mips_abiflags = 0x7000002a;
inline constexpr std::uint64_t shf_mips_gprel = 0x10000000;
inline constexpr std::uint64_t shf_mips_nostrip = 0x08000000;
inline constexpr std::uint16_t shn_mips_acommon = 0xff00;
inline constexpr std::uint16_t shn_mips_scommon = 0xff03;
inline constexpr std::uint16_t shn_mips_sundefined = 0xff04;
inline constexpr std::uint32_t pt_mips_reginfo = 0x70000000;
inline constexpr std::uint32_t pt_mips_options = 0x70000002;
inline constexpr std::uint32_t pt_mips_abiflags = 0x70000003;
// The kind of a .MIPS.options descriptor that holds register usage.
inline constexpr std::uint8_t odk_reginfo = 1;

inline constexpr std::uint32_t ef_mips_noreorder = 0x1;
inline constexpr std::uint32_t ef_mips_pic = 0x2;
inline constexpr std::uint32_t ef_mips_cpic = 0x4;
inline constexpr std::uint32_t ef_mips_32bitmode = 0x100;
inline constexpr std::uint32_t ef_mips_fp64 = 0x200;
inline constexpr std::uint32_t ef_mips_nan2008 = 0x400;
inline constexpr std::uint32_t ef_mips_abi2 = 0x20;
inline constexpr std::uint32_t ef_mips_abi = 0x0000f000;
inline constexpr std::uint32_t ef_mips_abi_o32 = 0x1000;
inline constexpr std::uint32_t ef_mips_abi_eabi32 = 0x3000;
inline constexpr std::uint32_t ef_mips_mach = 0x00ff0000;
inline constexpr std::uint32_t ef_mips_ase = 0x0f000000;
inline constexpr std::uint32_t ef_mips_arch = 0xf0000000;

// Instruction fields: %hi rounds for the sign of the low half, %higher and
// %highest for the signs of the halves below them.
constexpr std::uint32_t high16(std::uint64_t value) {
    return static_cast<std::uint32_t>(((value + 0x8000U) >> 16U) & 0xffffU);
}
constexpr std::uint32_t low16(std::uint64_t value) {
    return static_cast<std::uint32_t>(value & 0xffffU);
}
constexpr std::uint32_t higher16(std::uint64_t value) {
    return static_cast<std::uint32_t>(((value + 0x80008000ULL) >> 32U) & 0xffffU);
}
constexpr std::uint32_t highest16(std::uint64_t value) {
    return static_cast<std::uint32_t>(((value + 0x800080008000ULL) >> 48U) & 0xffffU);
}
// The word index of a jump target within its 256 MiB region.
constexpr std::uint32_t jump26(std::uint64_t target) {
    return static_cast<std::uint32_t>((target >> 2U) & 0x3ffffffU);
}
inline constexpr std::uint32_t jump26_mask = 0x3ffffff;
constexpr std::uint32_t insert_field(std::uint32_t word, std::uint32_t field,
                                     std::uint32_t mask = 0xffffU) {
    return (word & ~mask) | (field & mask);
}

} // namespace cross::mips

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/subtarget.hpp"
#include "target/x86_64/features.hpp"
#include "target/x86_64/instructions.hpp"

#include <vector>

namespace cross::x86_64 {

// Whether x86-64 has a packed integer form of an operation at one width:
// SSE2, SSE4.1 PMULLD, SSE4.2/AVX2 64-bit comparisons, AVX2 variable shifts
// and 256-bit forms, and AVX-512F/BW/DQ 512-bit forms. A uniform shift
// takes an immediate count.
inline bool packed_integer_operation(const Subtarget& subtarget,
                                     CostedVectorOperation operation,
                                     unsigned element_bits, unsigned width,
                                     bool uniform_shift) {
    using O = CostedVectorOperation;
    const bool add_sub = operation == O::Add || operation == O::Subtract;
    const bool bitwise = operation == O::BitAnd || operation == O::BitOr ||
                         operation == O::BitXor;
    const bool multiply = operation == O::Multiply;
    const bool shift = operation == O::ShiftLeft ||
                       operation == O::ShiftRightArithmetic ||
                       operation == O::ShiftRightLogical;
    const bool arithmetic_shift = operation == O::ShiftRightArithmetic;
    const bool comparison = operation == O::Compare;
    if (!add_sub && !bitwise && !multiply && !shift && !comparison) {
        return false;
    }
    if (width == 512) {
        if (!subtarget.has_feature(Feature::Avx512f)) return false;
        if (comparison) {
            return element_bits >= 32 ||
                   subtarget.has_feature(Feature::Avx512bw);
        }
        if (shift) {
            return element_bits >= 32 ||
                   (element_bits == 16 &&
                    subtarget.has_feature(Feature::Avx512bw));
        }
        if (bitwise) return true;
        if (element_bits == 8 || element_bits == 16) {
            return subtarget.has_feature(Feature::Avx512bw) &&
                   (add_sub || (multiply && element_bits == 16));
        }
        return add_sub ||
               (multiply && element_bits == 32) ||
               (multiply && element_bits == 64 &&
                subtarget.has_feature(Feature::Avx512dq));
    }
    if (width == 256) {
        const bool packed_shift = shift &&
            ((uniform_shift && element_bits == 16) ||
             element_bits == 32 ||
              (element_bits == 64 && !arithmetic_shift));
        return subtarget.has_feature(Feature::Avx2) &&
               (add_sub || bitwise || comparison || packed_shift ||
               (multiply && (element_bits == 16 ||
                              element_bits == 32 ||
                              element_bits == 64)));
    }
    if (comparison) {
        return element_bits <= 32 ||
               subtarget.has_feature(Feature::Avx2);
    }
    if (shift) {
        if (uniform_shift) {
            return element_bits == 16 || element_bits == 32 ||
                   (element_bits == 64 && !arithmetic_shift);
        }
        return subtarget.has_feature(Feature::Avx2) &&
               (element_bits == 32 ||
                (element_bits == 64 && !arithmetic_shift));
    }
    return add_sub || bitwise ||
           (multiply && element_bits == 16) ||
           (multiply && element_bits == 32) ||
           (multiply && element_bits == 64 &&
            subtarget.has_feature(Feature::Avx2));
}

// The instructions of a packed operation, or none where selection computes
// the lanes one at a time: an integer operation without a packed form at
// any width up to the vector's (wider vectors are issued in pieces), and
// every integer division. A 64-bit multiply without AVX-512DQ is a sequence
// of PMULUDQ partial products.
inline std::vector<InstructionCostId> vector_operation_instructions(
    const Subtarget& subtarget, const VectorOperationCostQuery& query) {
    using I = Instruction;
    using O = CostedVectorOperation;
    if (query.operation == O::Select) {
        // VBLENDV (VPBLENDVB for narrow lanes, which needs AVX2), else the
        // SSE mask logic.
        if (subtarget.has_feature(Feature::Avx) &&
            (query.element_bits >= 32 || subtarget.has_feature(Feature::Avx2))) {
            return {I::Vblendvps};
        }
        return {I::Andps, I::Andnps, I::Orps};
    }
    if (query.floating) {
        switch (query.operation) {
        case O::Add:
        case O::Subtract: return {I::Addps};
        case O::Multiply: return {I::Mulps};
        case O::Divide: return {I::Divps};
        case O::Compare: return {I::Cmpps};
        case O::BitAnd:
        case O::BitOr:
        case O::BitXor: return {I::Andps};
        default: return {};
        }
    }
    bool packed = false;
    for (const unsigned width : {512U, 256U, 128U}) {
        if (width <= query.vector_bits &&
            packed_integer_operation(subtarget, query.operation,
                                     query.element_bits, width,
                                     query.uniform_shift)) {
            packed = true;
            break;
        }
    }
    if (!packed) return {};
    const bool wide = query.element_bits == 64;
    switch (query.operation) {
    case O::Add:
    case O::Subtract: return {wide ? I::Paddq : I::Paddd};
    case O::BitAnd:
    case O::BitOr:
    case O::BitXor: return {I::Pand};
    case O::Compare: return {I::Pcmpgtd};
    case O::ShiftLeft:
    case O::ShiftRightArithmetic:
    case O::ShiftRightLogical:
        if (!query.uniform_shift) return {I::Psllvd};
        return {wide ? I::Psllq : I::Pslld};
    case O::Multiply:
        if (query.element_bits == 16) return {I::Pmullw};
        if (query.element_bits == 32) {
            if (subtarget.has_feature(Feature::Sse41)) return {I::Pmulld};
            return {I::Movdqa, I::Pmuludq, I::Pshufd, I::Pshufd,
                    I::Pmuludq, I::Pshufd, I::Pshufd, I::Punpckldq};
        }
        if (subtarget.has_feature(Feature::Avx512dq)) return {I::Vpmullq};
        return {I::Pmuludq, I::Pmuludq, I::Pmuludq, I::Psrlq, I::Psrlq,
                I::Paddq, I::Paddq, I::Psllq};
    default: return {};
    }
}

} // namespace cross::x86_64

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] static u32 symbol_identity(in u32 value) { return value; }
global u32 symbol_payload = 61u32;
global void symbol_label_owner() { global label point: ; }

// All three symbolic-address forms share a stack result. RAX is not an ABI
// result endpoint and the selected model promises to preserve its storage.
[[abi("stack_result_abi"), noinline]]
global u32 (*symbol_function_factory())(in u32 argument) { return &symbol_identity; }
[[abi("stack_result_abi"), noinline]]
global u32 *symbol_object_factory() { return &symbol_payload; }
[[abi("stack_result_abi"), noinline]]
global label symbol_label_factory() { return symbol_label_owner::point; }

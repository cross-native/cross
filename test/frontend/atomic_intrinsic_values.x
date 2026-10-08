// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static u32 [[atomic]] cell;
static volatile u16 [[atomic]] narrow;
static u32 effects;
[[noinline, runtime_only]] static u32 next() { ++effects; return 13u32; }
[[eval_only]] static u32 poison() { return 1u32 / 0u32; }
[[noinline, runtime_only]] static u32 [[atomic]] *address() { ++effects; return &cell; }
static uptr width<T>(in T value) { return sizeof(T); }

$::static_assert(sizeof($::atomic_load(&narrow, $::memory::relaxed)) == sizeof(u16), "narrow result");
$::static_assert($::alignof($::atomic_load(&narrow, $::memory::relaxed)) == $::alignof(u16), "unqualified result");

[[macro]] static $::meta::tokens typed(in $::meta::tokens input) {
    if (sizeof($::atomic_load(address(), $::memory::relaxed)) != sizeof(u32)) return $::quote { 0u32 };
    if (!$::atomic_is_lock_free(next())) return $::quote { 0u32 };
    return input;
}

#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (sizeof($::atomic_exchange(address(), next(), $::memory::relaxed)) != sizeof(u32)) return 1u32;
    if (sizeof($::atomic_is_lock_free(next())) != sizeof(bool) || effects != 0u32) return 2u32;
    if (!$::atomic_is_lock_free(next()) || !$::eval($::atomic_is_lock_free(next())) || effects != 0u32) return 10u32;
    if (!$::atomic_is_lock_free(poison()) || !$::atomic_is_lock_free(effects + poison())) return 11u32;
    u32 local = 0u32;
    if (!$::atomic_is_lock_free(local + poison()) || sizeof(local + poison()) != sizeof(u32)) return 12u32;
    { u64 local = 0u64;
      if ($::atomic_is_lock_free(local + poison()) != $::atomic_is_lock_free(u64) ||
          sizeof(local + poison()) != sizeof(u64)) return 13u32; }
    $::atomic_store(address(), next(), $::memory::release);
    if (effects != 2u32 || cell != 13u32) return 3u32;
    if ($::atomic_exchange(address(), next(), $::memory::seq_cst) != 13u32 || effects != 4u32) return 4u32;
    u32 expected = 13u32;
    if (!$::atomic_compare_exchange(address(), &expected, 17u32, $::memory::acq_rel, $::memory::acquire)) return 5u32;
    expected = 0u32;
    if ($::atomic_compare_exchange(address(), &expected, next(), $::memory::acquire, $::memory::relaxed)) return 6u32;
    if (expected != 17u32 || effects != 7u32) return 7u32;
    if ($::atomic_fetch_add(address(), 2u32, $::memory::relaxed) != 17u32 || effects != 8u32) return 8u32;
    if (width($::atomic_load(&cell, $::memory::acquire)) != sizeof(u32) || cell != 19u32) return 9u32;
    return typed!(61u32);
}

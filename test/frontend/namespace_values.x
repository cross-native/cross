// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 namespace_pick = 1u64;
static u64 namespace_object() { return 99u64; }
global u64 namespace_enum = 98u64;
static u64 namespace_fallback() { return 3u64; }
static u64 imported() { return 3u64; }
namespace value_scopes {
    [[noinline]] static u64 namespace_pick() { return 7u64; }
    global u64 namespace_object = 11u64;
    enum values { namespace_enum = 13 };
    global u64 initialized = namespace_pick();
    $::static_assert($::eval(namespace_pick()) == 7u64, "namespace call in required context");
    $::static_assert(namespace_enum == 13, "near enum before far object");
    namespace nested {
        namespace branch { static u64 get() { return 19u64; } }
        static u64 relative() { return branch::get(); }
        $::static_assert($::eval(branch::get()) == 19u64, "relative qualified call");
    }
    [[noinline]] static u64 local_callback() {
        u64 (*namespace_pick)() = namespace_fallback;
        return namespace_pick();
    }
    static u64 store_cell;
    [[noinline]] static u64 store_while_live() {
        u64 result = namespace_pick();
        store_cell = 17u64;
        return result;
    }
    [[noinline]] static i32 check() {
        if (namespace_pick() != 7u64 || initialized != 7u64) return 1;
        if (namespace_object != 11u64 || namespace_enum != 13) return 2;
        if (nested::relative() != 19u64 || local_callback() != 3u64) return 3;
        if (store_while_live() != 7u64 || store_cell != 17u64) return 4;
        return 0;
    }
}

namespace value_first {
    [[noinline]] static u64 imported() { return 23u64; }
    [[macro]] static $::meta::tokens imported_macro(in $::meta::tokens input) { return $::quote { 23u64 }; }
}
namespace value_second {
    global u64 imported = 29u64;
    [[macro]] static $::meta::tokens imported_macro(in $::meta::tokens input) { return $::quote { 29u64 }; }
}
namespace value_imports {
    using value_first;
    using value_second;
    $::static_assert($::eval(imported()) == 23u64, "first same-scope import wins");
    $::static_assert(imported_macro!{} == 23u64, "macro import ordering agrees");
    [[noinline]] static u64 check() {
        u64 total = imported();
        {
            using value_second;
            total += imported;
            if (imported_macro!{} != 29u64) return 999u64;
        }
        return total + imported();
    }
    namespace inner {
        using value_second;
        [[noinline]] static u64 check() { return imported; }
    }
}
namespace value_imports {
    // Reopening does not revive imports from the previous block.
    [[noinline]] static u64 closed() { return imported(); }
}

namespace value_definition {
    global u64 object = 31u64;
    global u64 cells[1] = { 37u64 };
    enum constants { marker = 41 };
    [[noinline]] static u64 helper() { return 43u64; }
    static T generic_pick<T>(in T value) { return value + 5u64; }
    [[macro]] static $::meta::tokens quoted_generic(in $::meta::tokens input) {
        return $::quote { generic_pick<u64>(37u64) };
    }
    [[macro]] static $::meta::tokens parsed_generic(in $::meta::tokens input) {
        return $::meta::parse("generic_pick<u64>(38u64)");
    }
    [[macro]] static $::meta::tokens quoted(in $::meta::tokens input) {
        return $::quote { helper() + object + marker };
    }
    [[macro]] static $::meta::tokens parsed(in $::meta::tokens input) {
        return $::meta::parse("helper() + object + marker");
    }
    [[macro]] static $::meta::tokens literal(in $::meta::tokens input) {
        return $::quote { "xy" };
    }
    [[macro]] static $::meta::tokens required(in $::meta::tokens input) {
        return $::quote { helper() + marker };
    }
    [[macro]] static $::meta::tokens pointer(in $::meta::tokens input) {
        return $::quote { cells };
    }
    [[macro]] static $::meta::tokens callback(in $::meta::tokens input) {
        return $::quote { helper };
    }
    [[macro]] static $::meta::tokens copied(in $::meta::tokens input) {
        return $::quote { $::unquote(input) };
    }
    [[macro]] static $::meta::tokens relocated(in $::meta::tokens input) {
        return $::quote { namespace generated { global u64 result = $::unquote(input); } };
    }
    [[macro]] static $::meta::tokens generic_definition(in $::meta::tokens input) {
        return $::quote {
            [[generic(u64 count), noinline]] static u64 macro_generic() {
                return helper() + count;
            }
        };
    }
}
namespace value_caller {
    global u64 object = 47u64;
    global u64 cells[1] = { 53u64 };
    global u64 generic_pick = 2u64;
    enum constants { marker = 59 };
    [[noinline]] static u64 helper() { return 61u64; }
    global u64 *alias = value_definition::pointer!{};
    global u64 (*callback)() = value_definition::callback!{};
    value_definition::relocated! { helper() }
    value_definition::generic_definition!{}
    $::static_assert(value_definition::required!{} == 84u64, "quote definition values");
    [[generic(u64 *address), noinline]] static u64 *identity() { return address; }
    [[noinline]] static i32 check() {
        u64 generic_pick = 3u64;
        if (value_definition::quoted!{} != 115u64) return 1;
        if (value_definition::parsed!{} != 115u64) return 2;
        if (value_definition::quoted_generic!{} != 42u64 ||
            value_definition::parsed_generic!{} != 43u64 ||
            generic_pick != 3u64) return 7;
        if (value_definition::copied!{helper() + object + marker} != 167u64) return 3;
        if (alias[0] != 37u64 || callback() != 43u64) return 4;
        if (generated::result != 61u64 || macro_generic::<5u64>() != 48u64) return 5;
        u64 *pointer = identity::<value_definition::pointer!{}>();
        if (*pointer != 37u64) return 6;
        return 0;
    }
}

namespace value_import_definition {
    using value_first;
    [[macro]] static $::meta::tokens with_import(in $::meta::tokens input) {
        return $::quote { imported() };
    }
}
#if defined(TEST_MACRO_RAW)
namespace value_floating {
    global f64 cell = 7.0f64;
    // An explicit ABI keeps this test independent of the compilation profile,
    // including profiles without a floating result rule.
    [[abi("sysv_abi"), noinline]] static f64 seed() { return 5.0f64; }
    [[abi("sysv_abi"), noinline]] static f64 load() {
        f64 value = seed();
        return value + cell;
    }
    [[abi("sysv_abi"), noinline]] static f64 store() {
        f64 value = seed();
        cell = 3.0f64;
        return value;
    }
}
#endif
[[noinline]] static i32 namespace_values_check() {
    i32 scopes_check = value_scopes::check();
    if (scopes_check != 0) return 10 + scopes_check;
    if (value_imports::check() != 75u64 || value_imports::inner::check() != 29u64) return 2;
    if (value_imports::closed() != 3u64) return 3;
    i32 caller_check = value_caller::check();
    if (caller_check != 0) return 40 + caller_check;
    if (value_import_definition::with_import!{} != 23u64) return 5;
#if defined(TEST_MACRO_RAW)
    if (value_floating::load() != 12.0f64) return 6;
    if (value_floating::store() != 5.0f64 || value_floating::cell != 3.0f64) return 7;
#endif
    return 0;
}

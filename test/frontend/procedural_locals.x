// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace hygienic_locals {
    [[macro]] static $::meta::tokens scope(in $::meta::tokens input) {
        return $::quote {
            u64 value = 3u64;
            $::unquote(input)
            if (value != 3u64) return 101u64;
        };
    }
    [[macro]] static $::meta::tokens forward(in $::meta::tokens input) {
        return $::quote { scope! { $::unquote(input) } };
    }
    [[noinline]] static u64 calculate(in u64 seed) {
        u64 value = seed;
        scope! { value += 5u64; }
        forward! { value += 7u64; }
        return value;
    }
    $::static_assert($::eval(hygienic_locals::calculate(7u64)) == 19u64, "evaluated macro local capture");

    [[macro]] static $::meta::tokens readonly_shadow(in $::meta::tokens input) {
        return $::quote {
            u64 value = 3u64;
            value += 4u64;
            if (value != 7u64 || $::unquote(input) != 23u64) return 102u64;
        };
    }
    [[noinline]] static u64 readonly_parameter(in u64 value) {
        readonly_shadow! { value }
        return value;
    }
    $::static_assert($::eval(hygienic_locals::readonly_parameter(23u64)) == 23u64, "evaluated parameter capture");

    [[macro]] static $::meta::tokens parameter(in $::meta::tokens input) {
        return $::quote {
            [[noinline]] static u64 parameter_generated(in u64 $::unquote(input)) {
                u64 value = 4u64;
                return $::unquote(input) + value;
            }
        };
    }
    parameter! { value }

    [[macro]] static $::meta::tokens static_scope(in $::meta::tokens input) {
        return $::quote {
            static u64 value = 17u64;
            ++value;
            $::unquote(input)
            if (value != 18u64) return 103u64;
        };
    }
    [[noinline]] static u64 static_cells() {
        u64 value = 11u64;
        static_scope! { value += 2u64; }
        static_scope! { value += 3u64; }
        return value;
    }

    [[generic(u64 value), noinline]] static u64 generic_value() {
        scope! { if (value != 29u64) return 104u64; }
        return value;
    }

    [[macro]] static $::meta::tokens objects(in $::meta::tokens input) {
        return $::quote {
            u64 data[2] = { 3u64, 4u64 };
            u64 *pointer = data;
            $::unquote(input)
            if (*pointer != 3u64 || sizeof(data) != 16uptr) return 105u64;
        };
    }
    [[noinline]] static u64 local_objects() {
        u64 data[2] = { 11u64, 12u64 };
        u64 *pointer = data;
        objects! { pointer[0] += 10u64; }
        return data[0] + data[1];
    }

    [[macro]] static $::meta::tokens sized(in $::meta::tokens input) {
        return $::quote {
            u64 value = 1u64;
            u8 bytes[$::unquote(input)];
            if (sizeof(bytes) != $::unquote(input) || value != 1u64) return 106u64;
        };
    }
    [[noinline]] static u64 variable_array(in uptr value) {
        sized! { value }
        return value;
    }

    [[noinline]] static u64 add_one(in u64 n) { return n + 1u64; }
    [[noinline]] static u64 add_two(in u64 n) { return n + 2u64; }
    [[macro]] static $::meta::tokens pointer_scope(in $::meta::tokens input) {
        return $::quote {
            u64 (*operation)(in u64 n) = hygienic_locals::add_two;
            $::unquote(input)
            if (operation(4u64) != 6u64) return 107u64;
        };
    }
    [[noinline]] static u64 local_function_pointers() {
        u64 (*operation)(in u64 n) = add_one;
        u64 answer = 0u64;
        pointer_scope! { answer = operation(4u64); }
        return answer;
    }

    [[macro]] static $::meta::tokens generic_definition(in $::meta::tokens input) {
        return $::quote {
            [[generic(u64 count), noinline]] static u64 generated_generic() {
                return count + $::unquote(input);
            }
        };
    }
    generic_definition! { 5u64 }

    global u64 pointer_target[1] = { 41u64 };
    [[generic(u64 *P), noinline]] static u64 *address() { return P; }
    [[macro]] static $::meta::tokens address_scope(in $::meta::tokens input) {
        return $::quote {
            u64 pointer_target = 3u64;
            $::unquote(input)
            if (pointer_target != 3u64) return 108u64;
        };
    }
    [[noinline]] static u64 generic_address() {
        u64 answer = 0u64;
        address_scope! { u64 *pointer = address::<pointer_target>(); answer = *pointer; }
        return answer;
    }

    [[macro]] static $::meta::tokens nested_scope(in $::meta::tokens input) {
        return $::quote {
            u64 value = 5u64;
            scope! { value += 2u64; }
            if (value != 7u64) return 109u64;
            $::unquote(input)
        };
    }
    [[macro]] static $::meta::tokens parsed_scope(in $::meta::tokens input) {
        return $::meta::concat($::meta::parse("u64 value = 3u64;"), $::quote {
            $::unquote(input)
            if (value != 3u64) return 110u64;
        });
    }
    [[macro]] static $::meta::tokens named_local(in $::meta::tokens input) {
        return $::quote { u64 $::unquote(input) = 9u64; };
    }
    [[noinline]] static u64 constructed_locals(in u64 seed) {
        u64 value = seed;
        nested_scope! { value += 2u64; }
        parsed_scope! { value += 3u64; }
        named_local! { named }
        return value + named;
    }
    $::static_assert($::eval(hygienic_locals::constructed_locals(1u64)) == 15u64,
                     "constructed and copied local identity");

    [[noinline]] static i32 check() {
        if (calculate(7u64) != 19u64) return 1;
        if (readonly_parameter(23u64) != 23u64) return 2;
        if (parameter_generated(5u64) != 9u64) return 3;
        if (static_cells() != 16u64) return 4;
        if (generic_value::<29u64>() != 29u64) return 5;
        if (local_objects() != 33u64) return 6;
        if (variable_array(5uptr) != 5u64) return 7;
        if (local_function_pointers() != 5u64) return 8;
        if (generated_generic::<7u64>() != 12u64) return 9;
        if (generic_address() != 41u64) return 10;
        if (constructed_locals(1u64) != 15u64) return 11;
        return 0;
    }
}

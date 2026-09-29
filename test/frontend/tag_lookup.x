// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace TagLookup {
    enum E [[underlying(u8)]] { OuterE };
    struct R { u8 value; };
    namespace First {
        enum Imported [[underlying(u16)]] { FirstE };
        union ImportedRecord { u16 value; };
    }
    namespace Second {
        enum Imported [[underlying(u32)]] { SecondE };
        union ImportedRecord { u32 value; };
    }
    namespace Definition {
        using TagLookup::First;
        enum E [[underlying(u16)]] { DefinitionE };
        struct R { u16 value; };
        [[macro]] static $::meta::tokens record(in $::meta::tokens input) {
            return $::quote { struct R };
        }
        [[macro]] static $::meta::tokens enumeration(in $::meta::tokens input) {
            return $::quote { enum E };
        }
        [[macro]] static $::meta::tokens copied_record(in $::meta::tokens input) {
            return $::quote { struct $::unquote(input) };
        }
        [[macro]] static $::meta::tokens copied_enum(in $::meta::tokens input) {
            return $::quote { enum $::unquote(input) };
        }
        [[macro]] static $::meta::tokens parsed(in $::meta::tokens input) {
            return $::meta::parse("struct R");
        }
        [[macro]] static $::meta::tokens call_site(in $::meta::tokens input) {
            return $::quote { struct $::unquote($::meta::call_site($::meta::parse("R"))) };
        }
        [[macro]] static $::meta::tokens imported(in $::meta::tokens input) {
            return $::quote { sizeof(enum Imported) + sizeof(union ImportedRecord) };
        }
        [[macro]] static $::meta::tokens incomplete(in $::meta::tokens input) {
            return $::quote { struct Forward * };
        }
        [[noinline]] static u32 read(in struct R item) { return (u32)item.value; }
        namespace Inner {
            $::static_assert(sizeof(enum E) == 2uptr, "enclosing enum precedes outer enum");
            $::static_assert(sizeof(struct R) == 2uptr, "enclosing record precedes outer record");
            $::static_assert(sizeof(enum TagLookup::E) == 1uptr, "qualified enum is exact");
            $::static_assert(sizeof(struct TagLookup::R) == 1uptr, "qualified record is exact");
        }
    }
    namespace Invocation {
        using TagLookup::Second;
        enum E [[underlying(u32)]] { InvocationE };
        struct R { u32 value; };
        global void forward_pointer(in Definition::incomplete!() value);
        global void forward_pointer(in struct TagLookup::Definition::Forward *value) {}
        [[noinline]] static u32 run(in u32 amount) {
            Definition::record!() item = {(u16)amount};
            if (sizeof(item) != 2uptr || sizeof(Definition::enumeration!()) != 2uptr ||
                sizeof(Definition::copied_record!(R)) != 4uptr ||
                sizeof(Definition::copied_enum!(E)) != 4uptr ||
                sizeof(Definition::parsed!()) != 2uptr ||
                sizeof(Definition::call_site!()) != 4uptr ||
                Definition::imported!() != 4uptr) return 0u32;
            return Definition::read(item);
        }
    }
    $::static_assert(Invocation::run(9u32) == 9u32, "tag token lookup context");
    namespace ImportOrder {
        using TagLookup::First;
        using TagLookup::Second;
        $::static_assert(sizeof(enum Imported) == 2uptr, "first enum import wins");
        $::static_assert(sizeof(union ImportedRecord) == 2uptr, "first record import wins");
        namespace Inner {
            using TagLookup::Second;
            $::static_assert(sizeof(enum Imported) == 4uptr, "inner enum import wins");
            $::static_assert(sizeof(union ImportedRecord) == 4uptr, "inner record import wins");
        }
    }
    namespace ImportOrder {
        using TagLookup::Second;
        $::static_assert(sizeof(enum Imported) == 4uptr, "reopened enum import scope");
        $::static_assert(sizeof(union ImportedRecord) == 4uptr, "reopened record import scope");
    }
}

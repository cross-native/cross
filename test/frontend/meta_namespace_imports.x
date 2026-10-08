// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace ImportOuter {
    [[noinline]] static u32 marker() { return 17u32; }
    typedef u16 Choice;
    struct Record { u16 value; };
    [[macro]] static $::meta::tokens extra(in $::meta::tokens input) { return $::quote { 3u32 }; }
}
namespace ImportInner {
    [[noinline]] static u32 marker() { return 31u32; }
    typedef u32 Choice;
    struct Record { u32 value; };
    [[macro]] static $::meta::tokens extra(in $::meta::tokens input) { return $::quote { 7u32 }; }
}
using ImportOuter;
namespace ImportBuilder {
    using ImportOuter;
    [[macro]] static $::meta::tokens define(in $::meta::tokens name) {
        return $::quote {
            namespace $::unquote(name) {
                using ImportInner;
                using ImportOuter;
                [[noinline]] static u32 first() {
                    return marker() + (u32)sizeof(Choice) + (u32)sizeof(struct Record) + extra!();
                }
                namespace Nested {
                    using ImportOuter;
                    [[noinline]] static u32 inner() { return marker(); }
                }
                [[noinline]] static u32 after_nested() { return marker(); }
                static $::meta::tokens helper() { return $::quote { marker() }; }
            }
            namespace $::unquote(name) {
                [[noinline]] static u32 reopened() { return marker(); }
            }
        };
    }
    [[macro]] static $::meta::tokens copy(in $::meta::tokens input) { return input; }
    [[macro]] static $::meta::tokens wrap(in $::meta::tokens input) {
        return $::quote { namespace Wrapped { using ImportInner; $::unquote(input) } };
    }
    [[macro]] static $::meta::tokens file(in $::meta::tokens input) {
        return $::quote {
            using ImportInner;
            [[noinline]] static u32 generated_file() { return marker(); }
            namespace FileChild {
                using ImportOuter;
                [[noinline]] static u32 inner() { return marker(); }
            }
            [[noinline]] static u32 after_file_child() { return marker(); }
        };
    }
}
namespace ImportCopied {
    using ImportInner;
    ImportBuilder::copy! {
        [[noinline]] static u32 copied() { return marker(); }
    }
}
namespace ImportRelocated {
    using ImportOuter;
    ImportBuilder::wrap! { [[noinline]] static u32 retained() { return marker(); } }
}
namespace ImportAmbient {
    using ImportInner;
    ImportBuilder::define!(Child)
}
ImportBuilder::define!(ImportGenerated)
[[syntax_expander]] static $::meta::tokens unused(in $::meta::syntax_match input) { return $::quote { 0u32 }; }
syntax Region : expression { prefix "unused_region"; match "(" ")"; expand unused; }
syntax (Region) {
    using ImportInner;
    ImportBuilder::copy! { [[noinline]] static u32 transparent() { return marker(); } }
}
ImportBuilder::copy! { [[noinline]] static u32 after_transparent() { return marker(); } }
namespace HelperCaller {
    using ImportOuter;
    [[macro]] static $::meta::tokens invoke(in $::meta::tokens input) { return ImportGenerated::helper(); }
    [[noinline]] static u32 run() { return invoke!(); }
}
ImportBuilder::file!()
namespace MixedNamespace { ImportBuilder::file!() }

#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return ImportGenerated::first() == 46u32 &&
        ImportGenerated::Nested::inner() == 17u32 &&
        ImportGenerated::after_nested() == 31u32 &&
        ImportGenerated::reopened() == 17u32 &&
        ImportCopied::copied() == 31u32 && HelperCaller::run() == 31u32 &&
        ImportRelocated::Wrapped::retained() == 17u32 && ImportAmbient::Child::reopened() == 17u32 &&
        transparent() == 31u32 && after_transparent() == 17u32 &&
        generated_file() == 31u32 && FileChild::inner() == 17u32 &&
        MixedNamespace::generated_file() == 31u32 && MixedNamespace::FileChild::inner() == 17u32 &&
        MixedNamespace::after_file_child() == 31u32 &&
        after_file_child() == 31u32 ? 61u32 : 0u32;
}

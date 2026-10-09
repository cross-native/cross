// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] static label generic_label_identity<label Address>() { return Address; }
[[noinline]] static label generic_label_forward<label Address>() {
    return generic_label_identity::<Address>();
}
namespace GenericLabelHelper {
    global void owner() { global label point: ; }
    static $::meta::tokens address() { return $::quote { generic_label_identity::<owner::point>() }; }
}
namespace GenericLabelDefinition {
    global void owner() { global label point: ; }
    // A canonical result must not undergo another relative lookup here.
    namespace GenericLabelDefinition { global void owner() { global label point: ; } }
    [[macro]] static $::meta::tokens quoted(in $::meta::tokens input) {
        return $::quote { generic_label_identity::<owner::point>() };
    }
    [[macro]] static $::meta::tokens parsed(in $::meta::tokens input) {
        return $::meta::parse("generic_label_identity::<owner::point>()");
    }
    [[macro]] static $::meta::tokens copied(in $::meta::tokens input) {
        return $::quote { generic_label_identity::<$::unquote(input)>() };
    }
    [[macro]] static $::meta::tokens retargeted(in $::meta::tokens input) {
        $::meta::tokens owner = $::meta::call_site($::meta::parse("owner"));
        return $::quote { generic_label_identity::<$::unquote(owner)::point>() };
    }
    [[macro]] static $::meta::tokens helper(in $::meta::tokens input) {
        return GenericLabelHelper::address();
    }
}
namespace GenericLabelImported {
    using GenericLabelHelper;
    [[macro]] static $::meta::tokens address(in $::meta::tokens input) {
        return $::quote { generic_label_identity::<owner::point>() };
    }
}
namespace GenericLabelInvocation {
    global void owner() { global label point: ; }
    [[noinline]] static label quoted() { return GenericLabelDefinition::quoted!(); }
    [[noinline]] static label parsed() { return GenericLabelDefinition::parsed!(); }
    [[noinline]] static label copied() { return GenericLabelDefinition::copied!(owner::point); }
    [[noinline]] static label retargeted() { return GenericLabelDefinition::retargeted!(); }
    [[noinline]] static label helper() { return GenericLabelDefinition::helper!(); }
    [[noinline]] static label imported() { return GenericLabelImported::address!(); }
}
[[macro]] static $::meta::tokens make_generic_fresh_labels(in $::meta::tokens input) {
    $::meta::tokens owner = $::meta::gensym("owner");
    $::meta::tokens first = $::meta::gensym("point");
    $::meta::tokens second = $::meta::gensym("point");
    return $::quote {
        global u32 $::unquote(owner)() {
            goto $::unquote(second);
            global label $::unquote(first): return 3u32;
            global label $::unquote(second): return 5u32;
        }
        [[noinline]] static u32 generic_fresh_labels() {
            label first = generic_label_identity::<$::unquote(owner)::$::unquote(first)>();
            label second = generic_label_identity::<$::unquote(owner)::$::unquote(second)>();
            return first == $::unquote(owner)::$::unquote(first) &&
                   second == $::unquote(owner)::$::unquote(second) && first != second;
        }
    };
}
make_generic_fresh_labels!()

// The spelling of both labels is identical, but their declaring expansions
// and source statements differ. Generic instance keys must retain that fact.
[[macro]] static $::meta::tokens capture_private_label(in $::meta::tokens output) {
    return $::quote {
        point: ;
        $::unquote(output) = generic_label_forward::<generic_private_labels::point>();
    };
}
[[noinline]] static u32 generic_private_labels() {
    label first, second;
    capture_private_label!(first)
    capture_private_label!(second)
    return first != second;
}

[[macro]] static $::meta::tokens make_private_labels(in $::meta::tokens input) {
    $::meta::tokens owner = $::meta::gensym("owner");
    $::meta::tokens first = $::meta::gensym("point");
    $::meta::tokens second = $::meta::gensym("point");
    return $::quote {
        global u32 $::unquote(owner)() {
            goto $::unquote(second);
            global label $::unquote(first): return 0u32;
            global label $::unquote(second): return 61u32;
        }
        global label $::unquote(owner)::$::unquote(first);
        global label $::unquote(owner)::$::unquote(second);
#ifdef CUSTOM_SYNTAX_ABI
        [[abi(HOST_ABI)]]
#endif
        global u32 syntax_raw_entry() {
            if (GenericLabelInvocation::quoted() != GenericLabelDefinition::owner::point ||
                GenericLabelInvocation::parsed() != GenericLabelDefinition::owner::point ||
                GenericLabelInvocation::copied() != GenericLabelInvocation::owner::point ||
                GenericLabelInvocation::retargeted() != GenericLabelInvocation::owner::point ||
                GenericLabelInvocation::helper() != GenericLabelHelper::owner::point ||
                GenericLabelInvocation::imported() != GenericLabelHelper::owner::point ||
                generic_fresh_labels() != 1u32 || generic_private_labels() != 1u32 ||
                generic_label_forward::<GenericLabelDefinition::owner::point>() !=
                    GenericLabelDefinition::owner::point) return 0u32;
            return $::unquote(owner)();
        }
    };
}

make_private_labels! {}

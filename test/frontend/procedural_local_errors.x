// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(QUOTED_CAPTURE)
[[macro]] static $::meta::tokens body(in $::meta::tokens input) {
    return $::quote { return value; };
}
static u64 subject(in u64 value) { body! {} }
$::static_assert($::eval(subject(7u64)) == 7u64, "quote must not capture parameter");
#elif defined(COPIED_READONLY)
[[macro]] static $::meta::tokens body(in $::meta::tokens input) {
    return $::quote { u64 value = 3u64; $::unquote(input) };
}
static u64 subject(in const u64 value) {
    body! { value += 2u64; }
    return value;
}
#elif defined(DUPLICATE_QUOTE)
[[macro]] static $::meta::tokens body(in $::meta::tokens input) {
    $::meta::tokens item = $::quote { u64 value = 3u64; };
    return $::meta::concat(item, item);
}
static u64 subject() { body! {} return 7u64; }
$::static_assert($::eval(subject()) == 7u64, "duplicate binding in one context");
#elif defined(QUOTED_NAMESPACE_CAPTURE)
namespace definitions {
    [[macro]] static $::meta::tokens body(in $::meta::tokens input) {
        return $::quote { only_at_call_site() };
    }
}
namespace invocations {
    static u64 only_at_call_site() { return 7u64; }
    $::static_assert(definitions::body!{} == 7u64, "definition context must not capture caller function");
}
#elif defined(OBJECT_SHADOWS_FUNCTION)
static u64 callable() { return 7u64; }
namespace inner {
    global u64 callable = 3u64;
    global u64 subject() { return callable(); }
}
#endif

// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]]
static $::meta::tokens inner(in $::meta::tokens input) {
    return $::quote {
        return +;
    };
}

[[macro]]
static $::meta::tokens outer(in $::meta::tokens input) {
    return $::quote {
        inner! { $::unquote(input) }
    };
}

global u64 generated_diagnostic() {
    outer! { ignored }
}

[[macro]]
static $::meta::tokens copied_inner(in $::meta::tokens input) {
    return input;
}

[[macro]]
static $::meta::tokens copied_outer(in $::meta::tokens input) {
    return $::quote { copied_inner! { $::unquote(input) } };
}

global u64 copied_diagnostic() {
    copied_outer! {
        return +;
    }
}

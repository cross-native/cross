// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace tools {
    [[macro]]
    static $::meta::tokens identity(in $::meta::tokens input) {
        return input;
    }
}

tools::identity! {
    global u64 qualified_generated = 66u64;
}

namespace app {
    using tools;

    identity! {
        global u64 imported_generated = 77u64;
    }

    global u64 generated_entry() {
        return imported_generated;
    }
}

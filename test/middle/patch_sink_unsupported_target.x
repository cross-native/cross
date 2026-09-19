// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global uptr unsupported_patch_sink;

global u32 unsupported_patch_sink_value() {
    return $::patch(7u32, unsupported_patch_sink);
}

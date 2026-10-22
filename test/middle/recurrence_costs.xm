# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The multiplication is cheap but the shifts are slow.
cpu "slowshift" {
    architecture = "x86-64";
    inherits = "generic";
    instruction "shll" { latency = 7; }
    instruction "shlq" { latency = 7; }
}

# Conversely, a slow multiplication makes a longer expansion worthwhile.
cpu "slowmul" {
    architecture = "x86-64";
    inherits = "generic";
    instruction "imull" { latency = 12; }
    instruction "imulq" { latency = 12; }
}

# Add/subtract latencies, as well as shifts, participate in the choice.
cpu "slowadd" {
    architecture = "x86-64";
    inherits = "generic";
    instruction "addl" { latency = 8; }
    instruction "addq" { latency = 8; }
}

cpu "slowsub" {
    architecture = "x86-64";
    inherits = "generic";
    instruction "subl" { latency = 8; }
    instruction "subq" { latency = 8; }
}

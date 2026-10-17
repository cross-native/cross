# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
section_words("${OBJECT}" .data big data)
expect_lines(".data words" "${data}" "00000004;00000014;00000010;00001000;6f6b0c")

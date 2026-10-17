/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* TEXT_START is 0x80000400 for the symbol file and 0x80300000 for the image
 * that imports its symbols. */
SECTIONS
{
    . = TEXT_START;
    .text : { *(.text) }
    /DISCARD/ : { *(*) }
}

# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Selects the linker of MIPS test images as LLD: the LD variable when given,
# else the CROSS_TEST_LD environment variable (which CROSS_TEST_CROSS_LD=ON
# sets to the Cross ld for every test), else ld.lld from PATH. Each accepts
# the GNU ld command line the tests use (-m EMULATION -T SCRIPT).

if((NOT DEFINED LD OR "${LD}" STREQUAL "") AND NOT "$ENV{CROSS_TEST_LD}" STREQUAL "")
    set(LD "$ENV{CROSS_TEST_LD}")
endif()
if(DEFINED LD AND NOT "${LD}" STREQUAL "")
    set(LLD "${LD}")
else()
    find_program(LLD NAMES ld.lld)
endif()

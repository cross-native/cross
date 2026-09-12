// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#define PRESENT 17
#define EMPTY
#define FUNCTION(x) ((x) + 1)

#if defined(MISSING) || 0 && PRESENT
#error a false condition was selected
#elif !defined PRESENT || !defined(EMPTY) || !defined(FUNCTION)
#error defined must not expand its operand
#elif FUNCTION(3) != 4 || (2 + 3 * 4) != 14
#error macro expansion or precedence failed
#else
global i32 correct_branch = 42;
#endif

#if 0
#define HIDDEN 1
#if ( invalid syntax ignored in inactive group
#error inactive nested group
#else
#error inactive nested else
#endif
#elif 1
global i32 selected_elif = 1;
#elif 1 / 0
#error a prior branch already won
#else
#error a prior branch already won
#endif

#if defined(HIDDEN) || defined(COMMAND_DISABLED)
#error inactive definition or command-line undef leaked
#endif
#if !defined(COMMAND_PRESENT) || COMMAND_PRESENT != 23
#error command-line macro was not visible
#endif

# if /* comment */ (0 || 1) && \
    (1 || (1 / 0)) && !(0 && (1 << 64)) // short circuit
global i32 continued_condition = 1;
# else
#error continued condition failed
# endif

#if !defined($::target::pointer_bytes) || $::target::pointer_bytes < 4
#error built-in macro lookup failed
#endif
#if $::has_feature($::feature::not_a_real_feature) && 1
#error false feature query selected
#endif

#undef PRESENT
#ifdef PRESENT
#error undef failed
#endif
#ifndef PRESENT
global i32 after_undef = 1;
#endif

/* Directives in comments are not active:
#if 1 / 0
#error comment parsing failed
*/
global i32 empty_expansion EMPTY = 0;

/\
* #error splicing must precede comment recognition */
#if 1 /\
* comment */ && 1
global i32 spliced_comment = 1;
#endif

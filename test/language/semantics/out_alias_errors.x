// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void unknown_call(in i32 **p);
global void unknown_pointer(out i32 *p);
global void redirect(in i32 **p, in i32 *q);
global i32 *return_pointer(in i32 *p);
struct pair { i32 a; i32 b; };

#if defined(PARTIAL_MEMBER)
global void bad(out struct pair x) { struct pair *p = &x; p->a = 1; }
#elif defined(MEMBER_READ)
global i32 bad(out struct pair x) {
    struct pair *p = &x;
    p->a = 1;
    i32 y = p->b;
    p->b = 2;
    return y;
}
#else
global i32 bad(out i32 x, in bool choose, in i32 *external) {
    i32 other = 0;
    i32 *p = &x;
#if defined(READ)
    i32 y = *p;
    x = 7;
    return y;
#elif defined(COMPOUND)
    *p += 1;
#elif defined(REASSIGN)
    p = &other;
    *p = 7;
#elif defined(BRANCH_GAP)
    if (choose) p = &other;
    *p = 7;
#elif defined(SELECT_GAP)
    p = choose ? &x : &other;
    *p = 7;
#elif defined(MAY_READ)
    p = choose ? &x : &other;
    i32 y = *p;
    x = 7;
    return y;
#elif defined(DYNAMIC_READ)
    p += *external;
    i32 y = *p;
    x = 7;
    return y;
#elif defined(DYNAMIC_WRITE)
    p += *external;
    *p = 7;
#elif defined(LOOP_GAP)
    bool again = choose;
    while (again) { *p = 7; again = 0; }
#elif defined(LOOP_REASSIGN)
    while (*external != 0) p = &other;
    *p = 7;
#elif defined(CALL_KILL)
    unknown_call(&p);
    *p = 7;
#elif defined(COPYOUT_KILL)
    unknown_pointer(p);
    *p = 7;
#elif defined(INDIRECT_KILL)
    i32 **pp = &p;
    *pp = &other;
    *p = 7;
#elif defined(UNKNOWN_WRITE)
    i32 **pp = &p;
    i32 **unknown = (i32 **)(void *)external;
    *unknown = &other;
    *p = 7;
#elif defined(PARTIAL_WRITE)
    u8 *bytes = (u8 *)(void *)&p;
    *bytes = 0u8;
    *p = 7;
#elif defined(CALL_READ)
    unknown_call(&p);
    i32 y = *p;
    x = 7;
    return y;
#elif defined(CALL_REDIRECT_READ)
    p = &other;
    redirect(&p, &x);
    i32 y = *p;
    x = 7;
    return y;
#elif defined(CALL_RESULT_READ)
    p = return_pointer(&x);
    i32 y = *p;
    x = 7;
    return y;
#endif
    return 0;
}
#endif

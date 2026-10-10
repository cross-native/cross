// The exception flags that x86-64 operations raise, read from MXCSR with every
// exception masked. This unit is compiled once under the trapping-fp profile
// with TRAPPING, defining the trapping:: operations, and once under the
// default profile, defining the masked:: operations and the test. For the
// trapping operations MXCSR also sets FTZ, as the profile's flushing declares.

#if defined(TRAPPING)
namespace trapping {
#else
namespace masked {
#endif
[[noinline]] global bool less(in f32 a, in f32 b) { return a < b; }
[[noinline]] global bool equal(in f32 a, in f32 b) { return a == b; }
[[noinline]] global bool zero(in f32 a) { return !a; }
[[noinline]] global f32 minimum(in f32 a, in f32 b) { return $::fmin(a, b); }
[[noinline]] global f64 maximum(in f64 a, in f64 b) { return $::fmax(a, b); }
[[noinline]] global f32 product(in f32 a, in f32 b) { return a * b; }
[[noinline]] global f32 negated(in f32 a) { return -a; }
[[noinline]] global f32 magnitude(in f32 a) { return $::fabs(a); }
[[noinline]] global f32 root(in f32 a) { return $::sqrt(a); }
// The product is unused: it still runs where the environment traps it.
[[noinline]] global void discard(in f32 a, in f32 b) { a * b; }
}

#if !defined(TRAPPING)
namespace trapping {
global bool less(in f32 a, in f32 b);
global bool equal(in f32 a, in f32 b);
global bool zero(in f32 a);
global f32 minimum(in f32 a, in f32 b);
global f64 maximum(in f64 a, in f64 b);
global f32 product(in f32 a, in f32 b);
global f32 negated(in f32 a);
global f32 magnitude(in f32 a);
global f32 root(in f32 a);
global void discard(in f32 a, in f32 b);
}

[[naked, clobber("memory")]]
global void store_mxcsr(in volatile u32 *cell "rdi") {
    $::_stmxcsr(*cell);
    $::_ret();
}

[[naked, clobber("memory")]]
global void load_mxcsr(in volatile u32 *cell "rdi") {
    $::_ldmxcsr(*cell);
    $::_ret();
}

union bits32 { f32 value; u32 bits; };
union bits64 { f64 value; u64 bits; };
static f32 f32_of(in u32 bits) { union bits32 cell; cell.bits = bits; return cell.value; }
static f64 f64_of(in u64 bits) { union bits64 cell; cell.bits = bits; return cell.value; }
static u32 bits_of(in f32 value) { union bits32 cell; cell.value = value; return cell.bits; }
static u64 bits_of64(in f64 value) { union bits64 cell; cell.value = value; return cell.bits; }

// MXCSR flags: invalid, denormal operand, underflow, and inexact.
global volatile u32 invalid = 0x01u32;
global volatile u32 denormal_operand = 0x02u32;
global volatile u32 underflow_inexact = 0x30u32;

global volatile u32 control;
global volatile u32 state;
global volatile u32 quiet_nan = 0x7fc00000u32;
global volatile u32 signaling_nan = 0x7fa00000u32;
global volatile u32 one = 0x3f800000u32;
global volatile u32 minus_one = 0xbf800000u32;
global volatile u32 denormal = 0x00000001u32;
global volatile u32 tiny = 0x0da24260u32;
global volatile u32 small = 0x2edbe6ffu32;
global volatile u64 quiet_nan64 = 0x7ff8000000000000u64;

// MXCSR with every exception masked and the flags clear, with and without
// FTZ.
global volatile u32 flushing = 0x9f80u32;
global volatile u32 gradual = 0x1f80u32;

static void clear(in u32 value) {
    control = value;
    load_mxcsr(&control);
}

static u32 raised() {
    store_mxcsr(&state);
    return state & 0x3fu32;
}

global u32 test_entry() {
    const f32 qnan = f32_of(quiet_nan);
    const f32 snan = f32_of(signaling_nan);
    const f32 x = f32_of(one);
    const f32 d = f32_of(denormal);
    u32 flags;

    // The relational operators signal on a quiet NaN; equality and the
    // truth value only on a signaling one.
    clear(flushing);
    bool result = $::runtime(trapping::less(qnan, x));
    if (raised() != invalid || result) return 1u32;
    clear(flushing);
    result = $::runtime(trapping::equal(qnan, x));
    if (raised() != 0u32 || result) return 2u32;
    clear(flushing);
    result = $::runtime(trapping::equal(snan, x));
    if (raised() != invalid || result) return 3u32;
    clear(flushing);
    result = $::runtime(trapping::zero(qnan));
    if (raised() != 0u32 || result) return 4u32;
    clear(flushing);
    result = $::runtime(trapping::zero(snan));
    if (raised() != invalid || result) return 5u32;
    clear(flushing);
    result = $::runtime(trapping::zero(d));
    if (raised() != denormal_operand || result) return 6u32;
    // minNum and maxNum raise invalid only on a signaling NaN.
    clear(flushing);
    f32 value = $::runtime(trapping::minimum(qnan, x));
    if (raised() != 0u32 || bits_of(value) != one) return 7u32;
    clear(flushing);
    value = $::runtime(trapping::minimum(qnan, snan));
    if (raised() != invalid || bits_of(value) != signaling_nan) return 8u32;
    clear(flushing);
    const f64 larger = $::runtime(trapping::maximum(f64_of(quiet_nan64), 2.0));
    if (raised() != 0u32 || bits_of64(larger) != 0x4000000000000000u64) return 9u32;
    // A denormal operand, and a tiny result that flushes with underflow and
    // inexact.
    clear(flushing);
    value = $::runtime(trapping::product(d, x));
    if (raised() != (denormal_operand | underflow_inexact) || bits_of(value) != 0u32)
        return 10u32;
    clear(flushing);
    $::runtime(trapping::discard(f32_of(tiny), f32_of(small)));
    if (raised() != underflow_inexact) return 11u32;
    // Sign operations raise nothing.
    clear(flushing);
    value = $::runtime(trapping::negated(snan));
    if (raised() != 0u32 || bits_of(value) != 0xffa00000u32) return 12u32;
    clear(flushing);
    value = $::runtime(trapping::magnitude(f32_of(0xffa00000u32)));
    if (raised() != 0u32 || bits_of(value) != 0x7fa00000u32) return 13u32;
    clear(flushing);
    value = $::runtime(trapping::root(f32_of(minus_one)));
    if (raised() != invalid) return 14u32;
    clear(flushing);
    value = $::runtime(trapping::root(qnan));
    if (raised() != 0u32) return 15u32;

    // The default profile: the same comparisons, and gradual underflow.
    clear(gradual);
    result = $::runtime(masked::less(qnan, x));
    if (raised() != invalid || result) return 21u32;
    clear(gradual);
    result = $::runtime(masked::equal(qnan, x));
    if (raised() != 0u32 || result) return 22u32;
    clear(gradual);
    result = $::runtime(masked::zero(snan));
    if (raised() != invalid || result) return 23u32;
    clear(gradual);
    value = $::runtime(masked::product(d, x));
    if (raised() != denormal_operand || bits_of(value) != denormal) return 24u32;
    clear(gradual);
    value = $::runtime(masked::product(f32_of(tiny), f32_of(small)));
    flags = raised();
    if (flags != underflow_inexact || bits_of(value) == 0u32) return 25u32;
    clear(gradual);
    value = $::runtime(masked::negated(snan));
    if (raised() != 0u32 || bits_of(value) != 0xffa00000u32) return 26u32;
    if (bits_of($::runtime(masked::minimum(qnan, x))) != one ||
        bits_of64($::runtime(masked::maximum(f64_of(quiet_nan64), 2.0))) !=
            0x4000000000000000u64) return 27u32;
    clear(gradual);
    return 0u32;
}
#endif

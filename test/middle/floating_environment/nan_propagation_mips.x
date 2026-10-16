// MIPS FPUs with the IEEE 754-2008 NaN encoding produce at run time the NaN
// that translation-time evaluation produces: the first signaling NaN
// operand, otherwise the first NaN operand, quieted, with its sign and
// payload.

union bits32 { f32 value; u32 bits; };
union bits64 { f64 value; u64 bits; };
static u32 b32(in f32 value) { union bits32 cell; cell.value = value; return cell.bits; }
static u64 b64(in f64 value) { union bits64 cell; cell.value = value; return cell.bits; }
static f32 f32_of(in u32 bits) { union bits32 cell; cell.bits = bits; return cell.value; }
static f64 f64_of(in u64 bits) { union bits64 cell; cell.bits = bits; return cell.value; }

global volatile u32 q1 = 0x7fc00001u32;
global volatile u32 q2 = 0xffc00002u32;
global volatile u32 s1 = 0x7f800003u32;
global volatile u32 s2 = 0xff800004u32;
global volatile u64 q64 = 0x7ff8000000000005u64;
global volatile f32 one = 1.0f32;
global volatile f32 zero = 0.0f32;

global f32 quiet_signaling = f32_of(0x7fc00001u32) / f32_of(0xff800004u32);
global f32 signaling_quiet = f32_of(0xff800004u32) / f32_of(0x7fc00001u32);
global f32 quiet_quiet = f32_of(0x7fc00001u32) + f32_of(0xffc00002u32);
global f32 signaling_signaling = f32_of(0x7f800003u32) * f32_of(0xff800004u32);
global f32 difference = 1.0f32 - f32_of(0xffc00002u32);
global f32 root = $::sqrt(f32_of(0xff800004u32));
global f64 widened = (f64)f32_of(0x7f800003u32);
global f32 narrowed = (f32)f64_of(0x7ff8000000000005u64);

global u32 test_entry() {
    if (b32(quiet_signaling) != b32(f32_of(q1) / f32_of(s2))) return 1u32;
    if (b32(signaling_quiet) != b32(f32_of(s2) / f32_of(q1))) return 2u32;
    if (b32(quiet_quiet) != b32(f32_of(q1) + f32_of(q2))) return 3u32;
    if (b32(signaling_signaling) != b32(f32_of(s1) * f32_of(s2))) return 4u32;
    if (b32(difference) != b32(one - f32_of(q2))) return 5u32;
    if (b32(root) != b32($::sqrt(f32_of(s2)))) return 6u32;
    if (b64(widened) != b64((f64)f32_of(s1))) return 7u32;
    if (b32(narrowed) != b32((f32)f64_of(q64))) return 8u32;
    // The optimizer folds an invalid operation and an operation on its
    // negated NaN.
    if (b32(-(0.0f32 / 0.0f32) + 1.0f32) != b32(-(zero / zero) + one)) return 9u32;
    if (b32(quiet_signaling) != 0xffc00004u32 || b32(difference) != 0xffc00002u32)
        return 10u32;
    return 0u32;
}

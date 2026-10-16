// Translation-time NaNs follow the target's NaN encoding: the NaN that an
// invalid operation produces and which NaNs are signaling.

union bits32 { f32 value; u32 bits; };
static f32 from_bits(in u32 bits) { union bits32 cell; cell.bits = bits; return cell.value; }

#if defined(QUIET_BIT_SET)
global f32 root = $::eval($::sqrt(from_bits(0x7fc00000u32)));
#elif defined(QUIET_BIT_CLEAR)
global f32 root = $::eval($::sqrt(from_bits(0x7fa00000u32)));
#elif defined(PROPAGATION)
// x86-64 propagates the first NaN operand and IEEE 754-2008 MIPS FPUs the
// signaling one, quieted.
global f32 propagated = from_bits(0x7fc00001u32) / from_bits(0x7fa00002u32);
global f64 widened = (f64)from_bits(0x7fa00002u32);
#elif defined(LEGACY_SIGNALING)
// A signaling NaN operand alone produces the legacy default NaN.
global f32 signaling_sum = from_bits(0x7fc00001u32) + 1.0f32;
#elif defined(LEGACY_QUIET)
// A quiet NaN operand may trap on a legacy MIPS FPU.
global f32 quiet_sum = from_bits(0x7fa00002u32) + 1.0f32;
#elif defined(LEGACY_CONVERSION)
global f64 widened = (f64)from_bits(0x7fc00001u32);
#elif defined(LEGACY_RUNTIME)
// Optional evaluation leaves the operation to run time.
static f32 add_one(in f32 value) { return value + 1.0f32; }
global f32 entry() { return add_one(from_bits(0x7fa00002u32)); }
#else
global f32 quotient32 = 0.0f32 / 0.0f32;
global f64 quotient64 = 0.0 / 0.0;
#endif

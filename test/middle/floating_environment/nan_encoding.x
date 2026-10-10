// Translation-time NaNs follow the target's NaN encoding: the NaN that an
// invalid operation produces and which NaNs are signaling.

union bits32 { f32 value; u32 bits; };
static f32 from_bits(in u32 bits) { union bits32 cell; cell.bits = bits; return cell.value; }

#if defined(QUIET_BIT_SET)
global f32 root = $::eval($::sqrt(from_bits(0x7fc00000u32)));
#elif defined(QUIET_BIT_CLEAR)
global f32 root = $::eval($::sqrt(from_bits(0x7fa00000u32)));
#else
global f32 quotient32 = 0.0f32 / 0.0f32;
global f64 quotient64 = 0.0 / 0.0;
#endif

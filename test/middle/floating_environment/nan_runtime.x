// The NaN that an invalid operation produces at run time is the one that
// translation-time evaluation produces for the same operation.

union bits32 { f32 value; u32 bits; };
union bits64 { f64 value; u64 bits; };
static u32 b32(in f32 value) { union bits32 cell; cell.value = value; return cell.bits; }
static u64 b64(in f64 value) { union bits64 cell; cell.value = value; return cell.bits; }

global volatile f32 zero32 = 0.0f32;
global volatile f64 zero64 = 0.0;
global volatile f32 minus_one32 = -1.0f32;
global volatile f64 minus_one64 = -1.0;

global f32 quotient32 = 0.0f32 / 0.0f32;
global f64 quotient64 = 0.0 / 0.0;
global f32 difference32 = 1.0f32 / 0.0f32 - 1.0f32 / 0.0f32;
global f64 product64 = 0.0 * (1.0 / 0.0);
global f32 root32 = $::sqrt(-1.0f32);
global f64 root64 = $::sqrt(-1.0);

#if defined(WIDE)
union bits80 { f80 value; u64 words[2]; };
union bits128 { f128 value; u64 words[2]; };
// The low 80 bits of an f80 object.
static u64 low80(in f80 value) { union bits80 cell; cell.value = value; return cell.words[0]; }
static u64 high80(in f80 value) { union bits80 cell; cell.value = value; return cell.words[1] & 0xffffu64; }
static u64 low128(in f128 value) { union bits128 cell; cell.value = value; return cell.words[0]; }
static u64 high128(in f128 value) { union bits128 cell; cell.value = value; return cell.words[1]; }

global volatile f80 zero80 = 0.0f80;
global volatile f128 zero128 = 0.0f128;
global f80 quotient80 = 0.0f80 / 0.0f80;
global f80 root80 = $::sqrt(-1.0f80);
global f128 quotient128 = 0.0f128 / 0.0f128;
global f128 difference128 = 1.0f128 / 0.0f128 - 1.0f128 / 0.0f128;
global f128 product128 = 0.0f128 * (1.0f128 / 0.0f128);
#endif

global u32 test_entry() {
    const f32 z32 = zero32;
    const f64 z64 = zero64;
    if (b32(quotient32) != b32(z32 / z32)) return 1u32;
    if (b64(quotient64) != b64(z64 / z64)) return 2u32;
    if (b32(difference32) != b32(1.0f32 / z32 - 1.0f32 / z32)) return 3u32;
    if (b64(product64) != b64(z64 * (1.0 / z64))) return 4u32;
    if (b32(root32) != b32($::sqrt(minus_one32))) return 5u32;
    if (b64(root64) != b64($::sqrt(minus_one64))) return 6u32;
#if defined(WIDE)
    const f80 z80 = zero80;
    const f128 z128 = zero128;
    const f80 runtime80 = z80 / z80;
    if (low80(quotient80) != low80(runtime80) || high80(quotient80) != high80(runtime80))
        return 7u32;
    const f80 runtime_root80 = $::sqrt(z80 - 1.0f80);
    if (low80(root80) != low80(runtime_root80) || high80(root80) != high80(runtime_root80))
        return 8u32;
    const f128 runtime128 = z128 / z128;
    if (low128(quotient128) != low128(runtime128) || high128(quotient128) != high128(runtime128))
        return 9u32;
    const f128 infinity128 = 1.0f128 / z128;
    const f128 runtime_difference128 = infinity128 - infinity128;
    if (low128(difference128) != low128(runtime_difference128) ||
        high128(difference128) != high128(runtime_difference128))
        return 10u32;
    const f128 runtime_product128 = z128 * infinity128;
    if (low128(product128) != low128(runtime_product128) ||
        high128(product128) != high128(runtime_product128))
        return 11u32;
#endif
    return 0u32;
}

// Instruction selection that the floating environment decides.

global bool less(in f32 a, in f32 b) { return a < b; }
global bool equal(in f32 a, in f32 b) { return a == b; }
global bool zero(in f64 a) { return !a; }
global f32 minimum(in f32 a, in f32 b) { return $::fmin(a, b); }
global f64 maximum(in f64 a, in f64 b) { return $::fmax(a, b); }
global f32 negated(in f32 a) { return -a; }
global f64 negated64(in f64 a) { return -a; }
#if defined(X87)
global bool less80(in f80 a, in f80 b) { return a < b; }
#endif

// A fast-math identity would delete the multiplication.
global f64 scaled(in f64 a) { return a * 1.0; }

// Unused results: an operation that may raise a trapped exception still runs.
global void discarded(in f64 a, in f64 b) {
    $::sqrt(a);
    a * b;
    a < b;
    f32 narrowed = (f32)a;
}

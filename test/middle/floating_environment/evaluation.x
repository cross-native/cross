// Translation-time evaluation follows the profile's floating environment.

global f32 tiny = 1.0e-30f32 * 1.0e-10f32;
global f32 negative_tiny = -1.0e-30f32 * 1.0e-10f32;
// A literal is not an operation: it keeps its denormal value.
global f32 written = 1.0e-40f32;
global f32 huge = 1.0e30f32 * 1.0e30f32;

f32 quotient(in f32 a, in f32 b) {
    return a / b;
}

global f32 folded() {
    return quotient(1.0f32, 4.0f32);
}

global f32 invalid_quotient() {
    return quotient(0.0f32, 0.0f32);
}

#if defined(INVALID)
global f32 nan_value = $::eval(0.0f32 / 0.0f32);
#elif defined(DENORMAL)
global f32 doubled = $::eval(1.0e-40f32 * 2.0f32);
#endif

global f64 x86_feature_arithmetic(in f64 left, in f64 right) {
    return (left + right) * right;
}

global bool x86_feature_compare(in f64 left, in f64 right) {
    return left < right;
}

global f32 x86_feature_narrow(in f64 value) {
    f32 result = value;
    return result;
}

global f64 x86_feature_widen(in f32 value) {
    f64 result = value;
    return result;
}

// The user instruction in a naked function and in managed code.
[[naked]]
global u32 mix(in u32 first "a0", in u32 second "a1") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    $::_vmix(result, first, second);
    $::_jr(link);
}

global u32 mixed(u32 first, u32 second) {
    u32 result;
    $::_vmix(result, first, second);
    return result;
}

global u32 has_vmix() {
    return $::has_instruction($::_vmix);
}

// Every entry gives a built-in: the alias, and both rotation forms, which
// the amount selects.
[[naked]]
global u32 blended(in u32 first "a0", in u32 second "a1") -> "v0" {
    register u32 result "v0";
    register void *link "ra";
    $::_vblend(result, first, second);
    $::_jr(link);
}

global u32 rotations(in u32 value) {
    u32 low;
    u32 high;
    $::_vrot(low, value, 5);
    $::_vrot(high, value, 40);
    return low ^ high;
}

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

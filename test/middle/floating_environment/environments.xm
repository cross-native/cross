# Floating environments for the speculation, evaluation, and runtime tests.

# Threads of an N64 program: the invalid-operation trap enabled and denormal
# results flushed to zero; the VR4300 also traps on a denormal operand.
profile "trapping-fp" {
    fp_traps = ["invalid"];
    fp_denormal_operand = "trap";
    fp_denormal_result = "flush";
}

# Only divide-by-zero traps, which reassociation cannot raise.
profile "divide-trap-fp" {
    fp_traps = ["divide-by-zero"];
}

# Overflow and underflow trap, which a fused multiply-add can raise.
profile "range-trap-fp" {
    fp_traps = ["overflow", "underflow"];
}

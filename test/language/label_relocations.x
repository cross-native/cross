// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An explicit uptr conversion of a code label in a static initializer is a
// relocation against the label, like the address of a function.
global u32 label_relocation_owner(in u32 value) {
    if (value == 0u32) goto done;
    value += 1u32;
    global label resume:
    value += 2u32;
done:
    return value;
}
[[noinline]] static void local_owner() { point: ; }

global uptr resume_address = (uptr)label_relocation_owner::resume;
global uptr point_after = (uptr)local_owner::point + 8uptr;
struct label_bits { uptr address; u32 tag; };
global struct label_bits labelled[2] = {
    { (uptr)label_relocation_owner::resume, 1u32 },
    { (uptr)(local_owner::point), 2u32 },
};

[[noinline]] static uptr own_point() {
    static uptr saved = (uptr)here;
here:
    return saved;
}

global u32 label_relocation_entry() {
    return resume_address == (uptr)label_relocation_owner::resume &&
           point_after - 8uptr == (uptr)local_owner::point &&
           labelled[0].address == resume_address && labelled[0].tag == 1u32 &&
           labelled[1].address == (uptr)local_owner::point && labelled[1].tag == 2u32 &&
           own_point() == (uptr)own_point::here &&
           label_relocation_owner(1u32) == 4u32 ? 1u32 : 0u32;
}

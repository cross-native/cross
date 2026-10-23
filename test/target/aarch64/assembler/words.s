// Values that GNU as and llvm-mc do not both take: relocation operators of
// constants, which give the operators' bits; immediates and offsets that
// only layout settles; an offset known before layout that the scaled form
// cannot hold, which takes the unscaled form; and prfm with an unscaled
// offset. Each comment holds the word.
	add	x1, x1, :lo12:0x12345678	// 9119e021
	ldr	x1, [x1, :lo12:0x12345678]	// f9433c21
	strh	w1, [x1, #:lo12:0x1236]		// 79046c21
	movz	x1, #:abs_g1:0x12345678		// d2a24681
	movk	x1, #:abs_g0_nc:0x12345678	// f28acf01
	movk	x1, #:abs_g3:0x123456789abcdef0	// f2e24681
	movz	x2, #(2f - 1f), lsl #16		// d2a00102
	add	x2, x2, #(2f - 1f)		// 91002042
1:	ldr	x2, [x2, #(2f - 1b)]		// f9400442
	ldr	x2, [x2, #(. - 1b - 2)]		// f8402042
2:	ldr	x2, [x2, #(3f - 2b + 8)]	// f9400842
	nop					// d503201f
3:	prfm	plil3strm, [x4, #-1]		// f89ff08d
	// Instructions after data start on a word.
	.byte	1				// 00000001
	adr	x3, 3b				// 10ffffc3

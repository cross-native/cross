// One statement per form of the shipped AArch64 instruction table,
// each selecting that form; the comment names the entry.
	add x1, x2, #4095 // add/x-immediate
	add w1, w2, #4095 // add/w-immediate
	add x1, x2, #-4096 // add/x-negative
	add w1, w2, #-4096 // add/w-negative
	add x3, x4, x5, asr #63 // add/x-shifted
	add w3, w4, w5, asr #31 // add/w-shifted
	add x6, sp, w7, sxth #2 // add/x-extended-w
	add x8, x9, x10, sxtx #4 // add/x-extended-x
	add sp, x11, x12 // add/x-sp
	add w13, w14, w15, uxtb #1 // add/w-extended
	add wsp, w16, w17 // add/w-sp
	adds x1, x2, #4095 // adds/x-immediate
	adds w1, w2, #4095 // adds/w-immediate
	adds x1, x2, #-4096 // adds/x-negative
	adds w1, w2, #-4096 // adds/w-negative
	adds x3, x4, x5, asr #63 // adds/x-shifted
	adds w3, w4, w5, asr #31 // adds/w-shifted
	adds x6, sp, w7, sxth #2 // adds/x-extended-w
	adds x8, x9, x10, sxtx #4 // adds/x-extended-x
	adds x11, sp, x12 // adds/x-sp
	adds w13, w14, w15, uxtb #1 // adds/w-extended
	adds w16, wsp, w17 // adds/w-sp
	sub x1, x2, #4095 // sub/x-immediate
	sub w1, w2, #4095 // sub/w-immediate
	sub x1, x2, #-4096 // sub/x-negative
	sub w1, w2, #-4096 // sub/w-negative
	sub x3, x4, x5, asr #63 // sub/x-shifted
	sub w3, w4, w5, asr #31 // sub/w-shifted
	sub x6, sp, w7, sxth #2 // sub/x-extended-w
	sub x8, x9, x10, sxtx #4 // sub/x-extended-x
	sub sp, x11, x12 // sub/x-sp
	sub w13, w14, w15, uxtb #1 // sub/w-extended
	sub wsp, w16, w17 // sub/w-sp
	subs x1, x2, #4095 // subs/x-immediate
	subs w1, w2, #4095 // subs/w-immediate
	subs x1, x2, #-4096 // subs/x-negative
	subs w1, w2, #-4096 // subs/w-negative
	subs x3, x4, x5, asr #63 // subs/x-shifted
	subs w3, w4, w5, asr #31 // subs/w-shifted
	subs x6, sp, w7, sxth #2 // subs/x-extended-w
	subs x8, x9, x10, sxtx #4 // subs/x-extended-x
	subs x11, sp, x12 // subs/x-sp
	subs w13, w14, w15, uxtb #1 // subs/w-extended
	subs w16, wsp, w17 // subs/w-sp
	cmp sp, #1, lsl #12 // cmp/x-immediate
	cmp x18, #-1 // cmp/x-negative
	cmp w18, #1, lsl #12 // cmp/w-immediate
	cmp w18, #-1 // cmp/w-negative
	cmp x19, x20, lsr #3 // cmp/x-shifted
	cmp w19, w20, lsr #3 // cmp/w-shifted
	cmp sp, w21, uxtw // cmp/x-extended-w
	cmp x22, x23, sxtx // cmp/x-extended-x
	cmp sp, x24 // cmp/x-sp
	cmp w25, w26, sxtb // cmp/w-extended
	cmp wsp, w27 // cmp/w-sp
	cmn sp, #1, lsl #12 // cmn/x-immediate
	cmn x18, #-1 // cmn/x-negative
	cmn w18, #1, lsl #12 // cmn/w-immediate
	cmn w18, #-1 // cmn/w-negative
	cmn x19, x20, lsr #3 // cmn/x-shifted
	cmn w19, w20, lsr #3 // cmn/w-shifted
	cmn sp, w21, uxtw // cmn/x-extended-w
	cmn x22, x23, sxtx // cmn/x-extended-x
	cmn sp, x24 // cmn/x-sp
	cmn w25, w26, sxtb // cmn/w-extended
	cmn wsp, w27 // cmn/w-sp
	neg x28, x0, lsl #2 // neg/x
	neg w28, w0, lsl #2 // neg/w
	negs x28, x0, lsl #2 // negs/x
	negs w28, w0, lsl #2 // negs/w
	adc x1, x2, x3 // adc/x
	adc w1, w2, w3 // adc/w
	adcs x1, x2, x3 // adcs/x
	adcs w1, w2, w3 // adcs/w
	sbc x1, x2, x3 // sbc/x
	sbc w1, w2, w3 // sbc/w
	sbcs x1, x2, x3 // sbcs/x
	sbcs w1, w2, w3 // sbcs/w
	ngc x4, x5 // ngc/x
	ngc w4, w5 // ngc/w
	ngcs x4, x5 // ngcs/x
	ngcs w4, w5 // ngcs/w
	and x1, x2, #0xff00ff00ff00ff00 // and/x-immediate
	and w3, w4, #0x80000001 // and/w-immediate
	orr x1, x2, #0xff00ff00ff00ff00 // orr/x-immediate
	orr w3, w4, #0x80000001 // orr/w-immediate
	eor x1, x2, #0xff00ff00ff00ff00 // eor/x-immediate
	eor w3, w4, #0x80000001 // eor/w-immediate
	ands x1, x2, #0xff00ff00ff00ff00 // ands/x-immediate
	ands w3, w4, #0x80000001 // ands/w-immediate
	and x5, x6, x7, ror #7 // and/x-shifted
	and w5, w6, w7, ror #7 // and/w-shifted
	bic x5, x6, x7, ror #7 // bic/x-shifted
	bic w5, w6, w7, ror #7 // bic/w-shifted
	orr x5, x6, x7, ror #7 // orr/x-shifted
	orr w5, w6, w7, ror #7 // orr/w-shifted
	orn x5, x6, x7, ror #7 // orn/x-shifted
	orn w5, w6, w7, ror #7 // orn/w-shifted
	eor x5, x6, x7, ror #7 // eor/x-shifted
	eor w5, w6, w7, ror #7 // eor/w-shifted
	eon x5, x6, x7, ror #7 // eon/x-shifted
	eon w5, w6, w7, ror #7 // eon/w-shifted
	ands x5, x6, x7, ror #7 // ands/x-shifted
	ands w5, w6, w7, ror #7 // ands/w-shifted
	bics x5, x6, x7, ror #7 // bics/x-shifted
	bics w5, w6, w7, ror #7 // bics/w-shifted
	tst x8, #0x3 // tst/x-immediate
	tst w9, #0xfffffff0 // tst/w-immediate
	tst x10, x11, lsl #1 // tst/x-shifted
	tst w10, w11, lsl #1 // tst/w-shifted
	mvn x12, x13, asr #4 // mvn/x
	mvn w12, w13, asr #4 // mvn/w
	movn x1, #0x1234, lsl #48 // movn/x
	movn w2, #0xabcd, lsl #16 // movn/w
	movz x1, #0x1234, lsl #48 // movz/x
	movz w2, #0xabcd, lsl #16 // movz/w
	movk x1, #0x1234, lsl #48 // movk/x
	movk w2, #0xabcd, lsl #16 // movk/w
	mov x3, x4 // mov/x
	mov w5, w6 // mov/w
	mov x7, sp // mov/x-sp
	mov wsp, w8 // mov/w-sp
	mov x9, #0x56780000 // mov/x-wide
	mov x10, #-0x10001 // mov/x-inverted
	mov sp, #0x00ff00ff00ff00ff // mov/x-bitmask
	mov w9, #0x56780000 // mov/w-wide
	mov w10, #-0x10001 // mov/w-inverted
	mov w11, #0x00ff00ff // mov/w-bitmask
	sbfm x1, x2, #3, #60 // sbfm/x
	sbfm w3, w4, #31, #7 // sbfm/w
	bfm x1, x2, #3, #60 // bfm/x
	bfm w3, w4, #31, #7 // bfm/w
	ubfm x1, x2, #3, #60 // ubfm/x
	ubfm w3, w4, #31, #7 // ubfm/w
	asr x5, x6, #63 // asr/x-immediate
	lsr x7, x8, #1 // lsr/x-immediate
	lsl x9, x10, #61 // lsl/x-immediate
	ror x11, x12, #62 // ror/x-immediate
	extr x13, x14, x15, #63 // extr/x
	sbfx x16, x17, #59, #4 // sbfx/x
	ubfx x16, x17, #59, #4 // ubfx/x
	bfxil x16, x17, #59, #4 // bfxil/x
	sbfiz x16, x17, #59, #4 // sbfiz/x
	ubfiz x16, x17, #59, #4 // ubfiz/x
	bfi x16, x17, #59, #4 // bfi/x
	bfc x18, #5, #3 // bfc/x
	asr w5, w6, #31 // asr/w-immediate
	lsr w7, w8, #1 // lsr/w-immediate
	lsl w9, w10, #29 // lsl/w-immediate
	ror w11, w12, #30 // ror/w-immediate
	extr w13, w14, w15, #31 // extr/w
	sbfx w16, w17, #27, #4 // sbfx/w
	ubfx w16, w17, #27, #4 // ubfx/w
	bfxil w16, w17, #27, #4 // bfxil/w
	sbfiz w16, w17, #27, #4 // sbfiz/w
	ubfiz w16, w17, #27, #4 // ubfiz/w
	bfi w16, w17, #27, #4 // bfi/w
	bfc w18, #5, #3 // bfc/w
	sxtb w19, w20 // sxtb/w
	sxtb x21, w22 // sxtb/x
	sxth w19, w20 // sxth/w
	sxth x21, w22 // sxth/x
	uxtb w19, w20 // uxtb/w
	uxtb x21, w22 // uxtb/x
	uxth w19, w20 // uxth/w
	uxth x21, w22 // uxth/x
	sxtw x23, w24 // sxtw
	madd x1, x2, x3, x4 // madd/x
	madd w1, w2, w3, w4 // madd/w
	msub x1, x2, x3, x4 // msub/x
	msub w1, w2, w3, w4 // msub/w
	mul x5, x6, x7 // mul/x
	mul w5, w6, w7 // mul/w
	mneg x5, x6, x7 // mneg/x
	mneg w5, w6, w7 // mneg/w
	smaddl x8, w9, w10, x11 // smaddl
	smsubl x8, w9, w10, x11 // smsubl
	umaddl x8, w9, w10, x11 // umaddl
	umsubl x8, w9, w10, x11 // umsubl
	smull x12, w13, w14 // smull
	smnegl x12, w13, w14 // smnegl
	umull x12, w13, w14 // umull
	umnegl x12, w13, w14 // umnegl
	smulh x15, x16, x17 // smulh
	umulh x15, x16, x17 // umulh
	udiv x18, x19, x20 // udiv/x
	udiv w18, w19, w20 // udiv/w
	sdiv x18, x19, x20 // sdiv/x
	sdiv w18, w19, w20 // sdiv/w
	lslv x18, x19, x20 // lslv/x
	lslv w18, w19, w20 // lslv/w
	lsrv x18, x19, x20 // lsrv/x
	lsrv w18, w19, w20 // lsrv/w
	asrv x18, x19, x20 // asrv/x
	asrv w18, w19, w20 // asrv/w
	rorv x18, x19, x20 // rorv/x
	rorv w18, w19, w20 // rorv/w
	lsl x21, x22, x23 // lsl/x-register
	lsl w21, w22, w23 // lsl/w-register
	lsr x21, x22, x23 // lsr/x-register
	lsr w21, w22, w23 // lsr/w-register
	asr x21, x22, x23 // asr/x-register
	asr w21, w22, w23 // asr/w-register
	ror x21, x22, x23 // ror/x-register
	ror w21, w22, w23 // ror/w-register
	rbit x24, x25 // rbit/x
	rbit w24, w25 // rbit/w
	rev16 x24, x25 // rev16/x
	rev16 w24, w25 // rev16/w
	rev32 x24, x25 // rev32/x
	clz x24, x25 // clz/x
	clz w24, w25 // clz/w
	cls x24, x25 // cls/x
	cls w24, w25 // cls/w
	rev x26, x27 // rev/x
	rev w28, w29 // rev/w
	rev64 x0, x1 // rev64
	crc32b w1, w2, w3 // crc32b
	crc32h w1, w2, w3 // crc32h
	crc32w w1, w2, w3 // crc32w
	crc32x w1, w2, x3 // crc32x
	crc32cb w1, w2, w3 // crc32cb
	crc32ch w1, w2, w3 // crc32ch
	crc32cw w1, w2, w3 // crc32cw
	crc32cx w1, w2, x3 // crc32cx
	csel x1, x2, x3, hs // csel/x
	csel w1, w2, w3, hs // csel/w
	csinc x1, x2, x3, hs // csinc/x
	csinc w1, w2, w3, hs // csinc/w
	csinv x1, x2, x3, hs // csinv/x
	csinv w1, w2, w3, hs // csinv/w
	csneg x1, x2, x3, hs // csneg/x
	csneg w1, w2, w3, hs // csneg/w
	cset x4, lt // cset/x
	cset w4, lt // cset/w
	csetm x4, lt // csetm/x
	csetm w4, lt // csetm/w
	cinc x5, x6, ne // cinc/x
	cinc w5, w6, ne // cinc/w
	cinv x5, x6, ne // cinv/x
	cinv w5, w6, ne // cinv/w
	cneg x5, x6, ne // cneg/x
	cneg w5, w6, ne // cneg/w
	ccmn x7, x8, #15, gt // ccmn/x-register
	ccmn x9, #31, #2, le // ccmn/x-immediate
	ccmn w7, w8, #15, gt // ccmn/w-register
	ccmn w9, #31, #2, le // ccmn/w-immediate
	ccmp x7, x8, #15, gt // ccmp/x-register
	ccmp x9, #31, #2, le // ccmp/x-immediate
	ccmp w7, w8, #15, gt // ccmp/w-register
	ccmp w9, #31, #2, le // ccmp/w-immediate
	adr x1, .+12 // adr
	adrp x2, .+8192 // adrp
	strb w1, [x1, #4095] // strb/w-8-scaled
	strb w2, [x3, #-256] // strb/w-8-unscaled
	strb w4, [x5, #255]! // strb/w-8-pre
	strb w6, [sp], #-8 // strb/w-8-post
	strb w7, [x9, x8] // strb/w-8-register
	sturb w10, [x11, #17] // sturb/w-8
	ldrb w1, [x1, #4095] // ldrb/w-8-scaled
	ldrb w2, [x3, #-256] // ldrb/w-8-unscaled
	ldrb w4, [x5, #255]! // ldrb/w-8-pre
	ldrb w6, [sp], #-8 // ldrb/w-8-post
	ldrb w7, [x9, x8] // ldrb/w-8-register
	ldurb w10, [x11, #17] // ldurb/w-8
	ldrsb x1, [x1, #4095] // ldrsb/x-8-scaled
	ldrsb x2, [x3, #-256] // ldrsb/x-8-unscaled
	ldrsb x4, [x5, #255]! // ldrsb/x-8-pre
	ldrsb x6, [sp], #-8 // ldrsb/x-8-post
	ldrsb x7, [x9, x8] // ldrsb/x-8-register
	ldursb x10, [x11, #17] // ldursb/x-8
	ldrsb w1, [x1, #4095] // ldrsb/w-8-scaled
	ldrsb w2, [x3, #-256] // ldrsb/w-8-unscaled
	ldrsb w4, [x5, #255]! // ldrsb/w-8-pre
	ldrsb w6, [sp], #-8 // ldrsb/w-8-post
	ldrsb w7, [x9, x8] // ldrsb/w-8-register
	ldursb w10, [x11, #17] // ldursb/w-8
	strh w1, [x1, #8190] // strh/w-16-scaled
	strh w2, [x3, #-256] // strh/w-16-unscaled
	strh w4, [x5, #255]! // strh/w-16-pre
	strh w6, [sp], #-8 // strh/w-16-post
	strh w7, [x9, x8, lsl #1] // strh/w-16-register
	sturh w10, [x11, #17] // sturh/w-16
	ldrh w1, [x1, #8190] // ldrh/w-16-scaled
	ldrh w2, [x3, #-256] // ldrh/w-16-unscaled
	ldrh w4, [x5, #255]! // ldrh/w-16-pre
	ldrh w6, [sp], #-8 // ldrh/w-16-post
	ldrh w7, [x9, x8, lsl #1] // ldrh/w-16-register
	ldurh w10, [x11, #17] // ldurh/w-16
	ldrsh x1, [x1, #8190] // ldrsh/x-16-scaled
	ldrsh x2, [x3, #-256] // ldrsh/x-16-unscaled
	ldrsh x4, [x5, #255]! // ldrsh/x-16-pre
	ldrsh x6, [sp], #-8 // ldrsh/x-16-post
	ldrsh x7, [x9, x8, lsl #1] // ldrsh/x-16-register
	ldursh x10, [x11, #17] // ldursh/x-16
	ldrsh w1, [x1, #8190] // ldrsh/w-16-scaled
	ldrsh w2, [x3, #-256] // ldrsh/w-16-unscaled
	ldrsh w4, [x5, #255]! // ldrsh/w-16-pre
	ldrsh w6, [sp], #-8 // ldrsh/w-16-post
	ldrsh w7, [x9, x8, lsl #1] // ldrsh/w-16-register
	ldursh w10, [x11, #17] // ldursh/w-16
	str w1, [x1, #16380] // str/w-32-scaled
	str w2, [x3, #-256] // str/w-32-unscaled
	str w4, [x5, #255]! // str/w-32-pre
	str w6, [sp], #-8 // str/w-32-post
	str w7, [x9, x8, lsl #2] // str/w-32-register
	stur w10, [x11, #17] // stur/w-32
	ldr w1, [x1, #16380] // ldr/w-32-scaled
	ldr w2, [x3, #-256] // ldr/w-32-unscaled
	ldr w4, [x5, #255]! // ldr/w-32-pre
	ldr w6, [sp], #-8 // ldr/w-32-post
	ldr w7, [x9, x8, lsl #2] // ldr/w-32-register
	ldur w10, [x11, #17] // ldur/w-32
	ldrsw x1, [x1, #16380] // ldrsw/x-32-scaled
	ldrsw x2, [x3, #-256] // ldrsw/x-32-unscaled
	ldrsw x4, [x5, #255]! // ldrsw/x-32-pre
	ldrsw x6, [sp], #-8 // ldrsw/x-32-post
	ldrsw x7, [x9, x8, lsl #2] // ldrsw/x-32-register
	ldursw x10, [x11, #17] // ldursw/x-32
	str x1, [x1, #32760] // str/x-64-scaled
	str x2, [x3, #-256] // str/x-64-unscaled
	str x4, [x5, #255]! // str/x-64-pre
	str x6, [sp], #-8 // str/x-64-post
	str x7, [x9, x8, lsl #3] // str/x-64-register
	stur x10, [x11, #17] // stur/x-64
	ldr x1, [x1, #32760] // ldr/x-64-scaled
	ldr x2, [x3, #-256] // ldr/x-64-unscaled
	ldr x4, [x5, #255]! // ldr/x-64-pre
	ldr x6, [sp], #-8 // ldr/x-64-post
	ldr x7, [x9, x8, lsl #3] // ldr/x-64-register
	ldur x10, [x11, #17] // ldur/x-64
	str b1, [x1, #4095] // str/b-8-scaled
	str b2, [x3, #-256] // str/b-8-unscaled
	str b4, [x5, #255]! // str/b-8-pre
	str b6, [sp], #-8 // str/b-8-post
	str b7, [x9, x8] // str/b-8-register
	stur b10, [x11, #17] // stur/b-8
	ldr b1, [x1, #4095] // ldr/b-8-scaled
	ldr b2, [x3, #-256] // ldr/b-8-unscaled
	ldr b4, [x5, #255]! // ldr/b-8-pre
	ldr b6, [sp], #-8 // ldr/b-8-post
	ldr b7, [x9, x8] // ldr/b-8-register
	ldur b10, [x11, #17] // ldur/b-8
	str h1, [x1, #8190] // str/h-16-scaled
	str h2, [x3, #-256] // str/h-16-unscaled
	str h4, [x5, #255]! // str/h-16-pre
	str h6, [sp], #-8 // str/h-16-post
	str h7, [x9, x8, lsl #1] // str/h-16-register
	stur h10, [x11, #17] // stur/h-16
	ldr h1, [x1, #8190] // ldr/h-16-scaled
	ldr h2, [x3, #-256] // ldr/h-16-unscaled
	ldr h4, [x5, #255]! // ldr/h-16-pre
	ldr h6, [sp], #-8 // ldr/h-16-post
	ldr h7, [x9, x8, lsl #1] // ldr/h-16-register
	ldur h10, [x11, #17] // ldur/h-16
	str s1, [x1, #16380] // str/s-32-scaled
	str s2, [x3, #-256] // str/s-32-unscaled
	str s4, [x5, #255]! // str/s-32-pre
	str s6, [sp], #-8 // str/s-32-post
	str s7, [x9, x8, lsl #2] // str/s-32-register
	stur s10, [x11, #17] // stur/s-32
	ldr s1, [x1, #16380] // ldr/s-32-scaled
	ldr s2, [x3, #-256] // ldr/s-32-unscaled
	ldr s4, [x5, #255]! // ldr/s-32-pre
	ldr s6, [sp], #-8 // ldr/s-32-post
	ldr s7, [x9, x8, lsl #2] // ldr/s-32-register
	ldur s10, [x11, #17] // ldur/s-32
	str d1, [x1, #32760] // str/d-64-scaled
	str d2, [x3, #-256] // str/d-64-unscaled
	str d4, [x5, #255]! // str/d-64-pre
	str d6, [sp], #-8 // str/d-64-post
	str d7, [x9, x8, lsl #3] // str/d-64-register
	stur d10, [x11, #17] // stur/d-64
	ldr d1, [x1, #32760] // ldr/d-64-scaled
	ldr d2, [x3, #-256] // ldr/d-64-unscaled
	ldr d4, [x5, #255]! // ldr/d-64-pre
	ldr d6, [sp], #-8 // ldr/d-64-post
	ldr d7, [x9, x8, lsl #3] // ldr/d-64-register
	ldur d10, [x11, #17] // ldur/d-64
	str q1, [x1, #65520] // str/q-128-scaled
	str q2, [x3, #-256] // str/q-128-unscaled
	str q4, [x5, #255]! // str/q-128-pre
	str q6, [sp], #-8 // str/q-128-post
	str q7, [x9, x8, lsl #4] // str/q-128-register
	stur q10, [x11, #17] // stur/q-128
	ldr q1, [x1, #65520] // ldr/q-128-scaled
	ldr q2, [x3, #-256] // ldr/q-128-unscaled
	ldr q4, [x5, #255]! // ldr/q-128-pre
	ldr q6, [sp], #-8 // ldr/q-128-post
	ldr q7, [x9, x8, lsl #4] // ldr/q-128-register
	ldur q10, [x11, #17] // ldur/q-128
	prfm pldl1keep, [x1, #8] // prfm/scaled
	prfm pstl2strm, [x2, w3, sxtw #3] // prfm/register
	prfm #5, .+16 // prfm/literal
	prfum pldl2keep, [x5, #255] // prfum
	sttrb w12, [x13, #-9] // sttrb/w-00
	ldtrb w12, [x13, #-9] // ldtrb/w-00
	ldtrsb x12, [x13, #-9] // ldtrsb/x-00
	ldtrsb w12, [x13, #-9] // ldtrsb/w-00
	sttrh w12, [x13, #-9] // sttrh/w-01
	ldtrh w12, [x13, #-9] // ldtrh/w-01
	ldtrsh x12, [x13, #-9] // ldtrsh/x-01
	ldtrsh w12, [x13, #-9] // ldtrsh/w-01
	sttr w12, [x13, #-9] // sttr/w-10
	ldtr w12, [x13, #-9] // ldtr/w-10
	ldtrsw x12, [x13, #-9] // ldtrsw/x-10
	sttr x12, [x13, #-9] // sttr/x-11
	ldtr x12, [x13, #-9] // ldtr/x-11
	ldr w14, .-1048576 // ldr/w-literal
	ldr x14, .-1048576 // ldr/x-literal
	ldrsw x14, .-1048576 // ldrsw/x-literal
	ldr s14, .-1048576 // ldr/s-literal
	ldr d14, .-1048576 // ldr/d-literal
	ldr q14, .-1048576 // ldr/q-literal
	stp w0, w1, [x2, #-256] // stp/w-32
	stp w0, w1, [sp, #-8]! // stp/w-32-pre
	stp w0, w1, [x3], #252 // stp/w-32-post
	stnp w4, w5, [x6, #32] // stnp/w-32
	ldp w0, w1, [x2, #-256] // ldp/w-32
	ldp w0, w1, [sp, #-8]! // ldp/w-32-pre
	ldp w0, w1, [x3], #252 // ldp/w-32-post
	ldnp w4, w5, [x6, #32] // ldnp/w-32
	ldpsw x0, x1, [x2, #-256] // ldpsw/x-32
	ldpsw x0, x1, [sp, #-8]! // ldpsw/x-32-pre
	ldpsw x0, x1, [x3], #252 // ldpsw/x-32-post
	stp x0, x1, [x2, #-512] // stp/x-64
	stp x0, x1, [sp, #-16]! // stp/x-64-pre
	stp x0, x1, [x3], #504 // stp/x-64-post
	stnp x4, x5, [x6, #64] // stnp/x-64
	ldp x0, x1, [x2, #-512] // ldp/x-64
	ldp x0, x1, [sp, #-16]! // ldp/x-64-pre
	ldp x0, x1, [x3], #504 // ldp/x-64-post
	ldnp x4, x5, [x6, #64] // ldnp/x-64
	stp s0, s1, [x2, #-256] // stp/s-32
	stp s0, s1, [sp, #-8]! // stp/s-32-pre
	stp s0, s1, [x3], #252 // stp/s-32-post
	stnp s4, s5, [x6, #32] // stnp/s-32
	ldp s0, s1, [x2, #-256] // ldp/s-32
	ldp s0, s1, [sp, #-8]! // ldp/s-32-pre
	ldp s0, s1, [x3], #252 // ldp/s-32-post
	ldnp s4, s5, [x6, #32] // ldnp/s-32
	stp d0, d1, [x2, #-512] // stp/d-64
	stp d0, d1, [sp, #-16]! // stp/d-64-pre
	stp d0, d1, [x3], #504 // stp/d-64-post
	stnp d4, d5, [x6, #64] // stnp/d-64
	ldp d0, d1, [x2, #-512] // ldp/d-64
	ldp d0, d1, [sp, #-16]! // ldp/d-64-pre
	ldp d0, d1, [x3], #504 // ldp/d-64-post
	ldnp d4, d5, [x6, #64] // ldnp/d-64
	stp q0, q1, [x2, #-1024] // stp/q-128
	stp q0, q1, [sp, #-32]! // stp/q-128-pre
	stp q0, q1, [x3], #1008 // stp/q-128-post
	stnp q4, q5, [x6, #128] // stnp/q-128
	ldp q0, q1, [x2, #-1024] // ldp/q-128
	ldp q0, q1, [sp, #-32]! // ldp/q-128-pre
	ldp q0, q1, [x3], #1008 // ldp/q-128-post
	ldnp q4, q5, [x6, #128] // ldnp/q-128
	stxrb w1, w2, [x3] // stxrb/w-00
	stlxrb w1, w2, [x3] // stlxrb/w-00
	ldxrb w4, [sp, #0] // ldxrb/w-00
	ldaxrb w4, [sp, #0] // ldaxrb/w-00
	stlrb w5, [x6] // stlrb/w-00
	ldarb w5, [x6] // ldarb/w-00
	stxrh w1, w2, [x3] // stxrh/w-01
	stlxrh w1, w2, [x3] // stlxrh/w-01
	ldxrh w4, [sp, #0] // ldxrh/w-01
	ldaxrh w4, [sp, #0] // ldaxrh/w-01
	stlrh w5, [x6] // stlrh/w-01
	ldarh w5, [x6] // ldarh/w-01
	stxr w1, w2, [x3] // stxr/w-10
	stlxr w1, w2, [x3] // stlxr/w-10
	ldxr w4, [sp, #0] // ldxr/w-10
	ldaxr w4, [sp, #0] // ldaxr/w-10
	stlr w5, [x6] // stlr/w-10
	ldar w5, [x6] // ldar/w-10
	stxr w1, x2, [x3] // stxr/x-11
	stlxr w1, x2, [x3] // stlxr/x-11
	ldxr x4, [sp, #0] // ldxr/x-11
	ldaxr x4, [sp, #0] // ldaxr/x-11
	stlr x5, [x6] // stlr/x-11
	ldar x5, [x6] // ldar/x-11
	stxp w7, w8, w9, [x10] // stxp/w
	stlxp w7, w8, w9, [x10] // stlxp/w
	ldxp w11, w12, [x13] // ldxp/w
	ldaxp w11, w12, [x13] // ldaxp/w
	stxp w7, x8, x9, [x10] // stxp/x
	stlxp w7, x8, x9, [x10] // stlxp/x
	ldxp x11, x12, [x13] // ldxp/x
	ldaxp x11, x12, [x13] // ldaxp/x
	casb w1, w2, [x3] // casb/w-00
	swpb w4, w5, [sp] // swpb/w-00
	ldaddb w6, w7, [x8] // ldaddb/w-00
	staddb w9, [x10] // staddb/w-00
	ldclrb w6, w7, [x8] // ldclrb/w-00
	stclrb w9, [x10] // stclrb/w-00
	ldeorb w6, w7, [x8] // ldeorb/w-00
	steorb w9, [x10] // steorb/w-00
	ldsetb w6, w7, [x8] // ldsetb/w-00
	stsetb w9, [x10] // stsetb/w-00
	ldsmaxb w6, w7, [x8] // ldsmaxb/w-00
	stsmaxb w9, [x10] // stsmaxb/w-00
	ldsminb w6, w7, [x8] // ldsminb/w-00
	stsminb w9, [x10] // stsminb/w-00
	ldumaxb w6, w7, [x8] // ldumaxb/w-00
	stumaxb w9, [x10] // stumaxb/w-00
	lduminb w6, w7, [x8] // lduminb/w-00
	stuminb w9, [x10] // stuminb/w-00
	casab w1, w2, [x3] // casab/w-00
	swpab w4, w5, [sp] // swpab/w-00
	ldaddab w6, w7, [x8] // ldaddab/w-00
	ldclrab w6, w7, [x8] // ldclrab/w-00
	ldeorab w6, w7, [x8] // ldeorab/w-00
	ldsetab w6, w7, [x8] // ldsetab/w-00
	ldsmaxab w6, w7, [x8] // ldsmaxab/w-00
	ldsminab w6, w7, [x8] // ldsminab/w-00
	ldumaxab w6, w7, [x8] // ldumaxab/w-00
	lduminab w6, w7, [x8] // lduminab/w-00
	caslb w1, w2, [x3] // caslb/w-00
	swplb w4, w5, [sp] // swplb/w-00
	ldaddlb w6, w7, [x8] // ldaddlb/w-00
	staddlb w9, [x10] // staddlb/w-00
	ldclrlb w6, w7, [x8] // ldclrlb/w-00
	stclrlb w9, [x10] // stclrlb/w-00
	ldeorlb w6, w7, [x8] // ldeorlb/w-00
	steorlb w9, [x10] // steorlb/w-00
	ldsetlb w6, w7, [x8] // ldsetlb/w-00
	stsetlb w9, [x10] // stsetlb/w-00
	ldsmaxlb w6, w7, [x8] // ldsmaxlb/w-00
	stsmaxlb w9, [x10] // stsmaxlb/w-00
	ldsminlb w6, w7, [x8] // ldsminlb/w-00
	stsminlb w9, [x10] // stsminlb/w-00
	ldumaxlb w6, w7, [x8] // ldumaxlb/w-00
	stumaxlb w9, [x10] // stumaxlb/w-00
	lduminlb w6, w7, [x8] // lduminlb/w-00
	stuminlb w9, [x10] // stuminlb/w-00
	casalb w1, w2, [x3] // casalb/w-00
	swpalb w4, w5, [sp] // swpalb/w-00
	ldaddalb w6, w7, [x8] // ldaddalb/w-00
	ldclralb w6, w7, [x8] // ldclralb/w-00
	ldeoralb w6, w7, [x8] // ldeoralb/w-00
	ldsetalb w6, w7, [x8] // ldsetalb/w-00
	ldsmaxalb w6, w7, [x8] // ldsmaxalb/w-00
	ldsminalb w6, w7, [x8] // ldsminalb/w-00
	ldumaxalb w6, w7, [x8] // ldumaxalb/w-00
	lduminalb w6, w7, [x8] // lduminalb/w-00
	cash w1, w2, [x3] // cash/w-01
	swph w4, w5, [sp] // swph/w-01
	ldaddh w6, w7, [x8] // ldaddh/w-01
	staddh w9, [x10] // staddh/w-01
	ldclrh w6, w7, [x8] // ldclrh/w-01
	stclrh w9, [x10] // stclrh/w-01
	ldeorh w6, w7, [x8] // ldeorh/w-01
	steorh w9, [x10] // steorh/w-01
	ldseth w6, w7, [x8] // ldseth/w-01
	stseth w9, [x10] // stseth/w-01
	ldsmaxh w6, w7, [x8] // ldsmaxh/w-01
	stsmaxh w9, [x10] // stsmaxh/w-01
	ldsminh w6, w7, [x8] // ldsminh/w-01
	stsminh w9, [x10] // stsminh/w-01
	ldumaxh w6, w7, [x8] // ldumaxh/w-01
	stumaxh w9, [x10] // stumaxh/w-01
	lduminh w6, w7, [x8] // lduminh/w-01
	stuminh w9, [x10] // stuminh/w-01
	casah w1, w2, [x3] // casah/w-01
	swpah w4, w5, [sp] // swpah/w-01
	ldaddah w6, w7, [x8] // ldaddah/w-01
	ldclrah w6, w7, [x8] // ldclrah/w-01
	ldeorah w6, w7, [x8] // ldeorah/w-01
	ldsetah w6, w7, [x8] // ldsetah/w-01
	ldsmaxah w6, w7, [x8] // ldsmaxah/w-01
	ldsminah w6, w7, [x8] // ldsminah/w-01
	ldumaxah w6, w7, [x8] // ldumaxah/w-01
	lduminah w6, w7, [x8] // lduminah/w-01
	caslh w1, w2, [x3] // caslh/w-01
	swplh w4, w5, [sp] // swplh/w-01
	ldaddlh w6, w7, [x8] // ldaddlh/w-01
	staddlh w9, [x10] // staddlh/w-01
	ldclrlh w6, w7, [x8] // ldclrlh/w-01
	stclrlh w9, [x10] // stclrlh/w-01
	ldeorlh w6, w7, [x8] // ldeorlh/w-01
	steorlh w9, [x10] // steorlh/w-01
	ldsetlh w6, w7, [x8] // ldsetlh/w-01
	stsetlh w9, [x10] // stsetlh/w-01
	ldsmaxlh w6, w7, [x8] // ldsmaxlh/w-01
	stsmaxlh w9, [x10] // stsmaxlh/w-01
	ldsminlh w6, w7, [x8] // ldsminlh/w-01
	stsminlh w9, [x10] // stsminlh/w-01
	ldumaxlh w6, w7, [x8] // ldumaxlh/w-01
	stumaxlh w9, [x10] // stumaxlh/w-01
	lduminlh w6, w7, [x8] // lduminlh/w-01
	stuminlh w9, [x10] // stuminlh/w-01
	casalh w1, w2, [x3] // casalh/w-01
	swpalh w4, w5, [sp] // swpalh/w-01
	ldaddalh w6, w7, [x8] // ldaddalh/w-01
	ldclralh w6, w7, [x8] // ldclralh/w-01
	ldeoralh w6, w7, [x8] // ldeoralh/w-01
	ldsetalh w6, w7, [x8] // ldsetalh/w-01
	ldsmaxalh w6, w7, [x8] // ldsmaxalh/w-01
	ldsminalh w6, w7, [x8] // ldsminalh/w-01
	ldumaxalh w6, w7, [x8] // ldumaxalh/w-01
	lduminalh w6, w7, [x8] // lduminalh/w-01
	cas w1, w2, [x3] // cas/w-10
	swp w4, w5, [sp] // swp/w-10
	ldadd w6, w7, [x8] // ldadd/w-10
	stadd w9, [x10] // stadd/w-10
	ldclr w6, w7, [x8] // ldclr/w-10
	stclr w9, [x10] // stclr/w-10
	ldeor w6, w7, [x8] // ldeor/w-10
	steor w9, [x10] // steor/w-10
	ldset w6, w7, [x8] // ldset/w-10
	stset w9, [x10] // stset/w-10
	ldsmax w6, w7, [x8] // ldsmax/w-10
	stsmax w9, [x10] // stsmax/w-10
	ldsmin w6, w7, [x8] // ldsmin/w-10
	stsmin w9, [x10] // stsmin/w-10
	ldumax w6, w7, [x8] // ldumax/w-10
	stumax w9, [x10] // stumax/w-10
	ldumin w6, w7, [x8] // ldumin/w-10
	stumin w9, [x10] // stumin/w-10
	casa w1, w2, [x3] // casa/w-10
	swpa w4, w5, [sp] // swpa/w-10
	ldadda w6, w7, [x8] // ldadda/w-10
	ldclra w6, w7, [x8] // ldclra/w-10
	ldeora w6, w7, [x8] // ldeora/w-10
	ldseta w6, w7, [x8] // ldseta/w-10
	ldsmaxa w6, w7, [x8] // ldsmaxa/w-10
	ldsmina w6, w7, [x8] // ldsmina/w-10
	ldumaxa w6, w7, [x8] // ldumaxa/w-10
	ldumina w6, w7, [x8] // ldumina/w-10
	casl w1, w2, [x3] // casl/w-10
	swpl w4, w5, [sp] // swpl/w-10
	ldaddl w6, w7, [x8] // ldaddl/w-10
	staddl w9, [x10] // staddl/w-10
	ldclrl w6, w7, [x8] // ldclrl/w-10
	stclrl w9, [x10] // stclrl/w-10
	ldeorl w6, w7, [x8] // ldeorl/w-10
	steorl w9, [x10] // steorl/w-10
	ldsetl w6, w7, [x8] // ldsetl/w-10
	stsetl w9, [x10] // stsetl/w-10
	ldsmaxl w6, w7, [x8] // ldsmaxl/w-10
	stsmaxl w9, [x10] // stsmaxl/w-10
	ldsminl w6, w7, [x8] // ldsminl/w-10
	stsminl w9, [x10] // stsminl/w-10
	ldumaxl w6, w7, [x8] // ldumaxl/w-10
	stumaxl w9, [x10] // stumaxl/w-10
	lduminl w6, w7, [x8] // lduminl/w-10
	stuminl w9, [x10] // stuminl/w-10
	casal w1, w2, [x3] // casal/w-10
	swpal w4, w5, [sp] // swpal/w-10
	ldaddal w6, w7, [x8] // ldaddal/w-10
	ldclral w6, w7, [x8] // ldclral/w-10
	ldeoral w6, w7, [x8] // ldeoral/w-10
	ldsetal w6, w7, [x8] // ldsetal/w-10
	ldsmaxal w6, w7, [x8] // ldsmaxal/w-10
	ldsminal w6, w7, [x8] // ldsminal/w-10
	ldumaxal w6, w7, [x8] // ldumaxal/w-10
	lduminal w6, w7, [x8] // lduminal/w-10
	cas x1, x2, [x3] // cas/x-11
	swp x4, x5, [sp] // swp/x-11
	ldadd x6, x7, [x8] // ldadd/x-11
	stadd x9, [x10] // stadd/x-11
	ldclr x6, x7, [x8] // ldclr/x-11
	stclr x9, [x10] // stclr/x-11
	ldeor x6, x7, [x8] // ldeor/x-11
	steor x9, [x10] // steor/x-11
	ldset x6, x7, [x8] // ldset/x-11
	stset x9, [x10] // stset/x-11
	ldsmax x6, x7, [x8] // ldsmax/x-11
	stsmax x9, [x10] // stsmax/x-11
	ldsmin x6, x7, [x8] // ldsmin/x-11
	stsmin x9, [x10] // stsmin/x-11
	ldumax x6, x7, [x8] // ldumax/x-11
	stumax x9, [x10] // stumax/x-11
	ldumin x6, x7, [x8] // ldumin/x-11
	stumin x9, [x10] // stumin/x-11
	casa x1, x2, [x3] // casa/x-11
	swpa x4, x5, [sp] // swpa/x-11
	ldadda x6, x7, [x8] // ldadda/x-11
	ldclra x6, x7, [x8] // ldclra/x-11
	ldeora x6, x7, [x8] // ldeora/x-11
	ldseta x6, x7, [x8] // ldseta/x-11
	ldsmaxa x6, x7, [x8] // ldsmaxa/x-11
	ldsmina x6, x7, [x8] // ldsmina/x-11
	ldumaxa x6, x7, [x8] // ldumaxa/x-11
	ldumina x6, x7, [x8] // ldumina/x-11
	casl x1, x2, [x3] // casl/x-11
	swpl x4, x5, [sp] // swpl/x-11
	ldaddl x6, x7, [x8] // ldaddl/x-11
	staddl x9, [x10] // staddl/x-11
	ldclrl x6, x7, [x8] // ldclrl/x-11
	stclrl x9, [x10] // stclrl/x-11
	ldeorl x6, x7, [x8] // ldeorl/x-11
	steorl x9, [x10] // steorl/x-11
	ldsetl x6, x7, [x8] // ldsetl/x-11
	stsetl x9, [x10] // stsetl/x-11
	ldsmaxl x6, x7, [x8] // ldsmaxl/x-11
	stsmaxl x9, [x10] // stsmaxl/x-11
	ldsminl x6, x7, [x8] // ldsminl/x-11
	stsminl x9, [x10] // stsminl/x-11
	ldumaxl x6, x7, [x8] // ldumaxl/x-11
	stumaxl x9, [x10] // stumaxl/x-11
	lduminl x6, x7, [x8] // lduminl/x-11
	stuminl x9, [x10] // stuminl/x-11
	casal x1, x2, [x3] // casal/x-11
	swpal x4, x5, [sp] // swpal/x-11
	ldaddal x6, x7, [x8] // ldaddal/x-11
	ldclral x6, x7, [x8] // ldclral/x-11
	ldeoral x6, x7, [x8] // ldeoral/x-11
	ldsetal x6, x7, [x8] // ldsetal/x-11
	ldsmaxal x6, x7, [x8] // ldsmaxal/x-11
	ldsminal x6, x7, [x8] // ldsminal/x-11
	ldumaxal x6, x7, [x8] // ldumaxal/x-11
	lduminal x6, x7, [x8] // lduminal/x-11
	b .+0x7fffffc // b
	bl .-0x8000000 // bl
	b.eq .+0 // b.eq
	b.ne .+4 // b.ne
	b.cs .+8 // b.cs
	b.cc .+12 // b.cc
	b.mi .+16 // b.mi
	b.pl .+20 // b.pl
	b.vs .+24 // b.vs
	b.vc .+28 // b.vc
	b.hi .+32 // b.hi
	b.ls .+36 // b.ls
	b.ge .+40 // b.ge
	b.lt .+44 // b.lt
	b.gt .+48 // b.gt
	b.le .+52 // b.le
	b.al .+56 // b.al
	b.nv .+60 // b.nv
	b.hs .+4 // b.hs
	b.lo .+4 // b.lo
	beq .-4 // beq
	bne .-4 // bne
	bcs .-4 // bcs
	bcc .-4 // bcc
	bmi .-4 // bmi
	bpl .-4 // bpl
	bvs .-4 // bvs
	bvc .-4 // bvc
	bhi .-4 // bhi
	bls .-4 // bls
	bge .-4 // bge
	blt .-4 // blt
	bgt .-4 // bgt
	ble .-4 // ble
	bhs .-4 // bhs
	blo .-4 // blo
	cbz x1, .+0xffffc // cbz/x
	cbz w1, .+0xffffc // cbz/w
	cbnz x1, .+0xffffc // cbnz/x
	cbnz w1, .+0xffffc // cbnz/w
	tbz x2, #63, .-0x8000 // tbz/x
	tbz w3, #31, .+0x7ffc // tbz/w
	tbnz x2, #63, .-0x8000 // tbnz/x
	tbnz w3, #31, .+0x7ffc // tbnz/w
	br x4 // br
	blr x5 // blr
	ret // ret
	ret x6 // ret/register
	eret // eret
	svc #0xffff // svc
	hvc #0xffff // hvc
	smc #0xffff // smc
	brk #0xffff // brk
	hlt #0xffff // hlt
	udf #0x1234 // udf
	nop // nop
	yield // yield
	wfe // wfe
	wfi // wfi
	sev // sev
	sevl // sevl
	hint #127 // hint
	clrex // clrex
	clrex #3 // clrex/immediate
	dsb #5 // dsb
	dmb ishld // dmb
	isb // isb
	isb sy // isb/option
	ssbb // ssbb
	pssbb // pssbb
	sb // sb
	mrs x1, tpidr_el0 // mrs
	msr s3_3_c13_c0_2, x2 // msr
	msr daifclr, #15 // msr/pstate
	fmov s1, s2 // fmov/s
	fmov s3, #-1.9375 // fmov/s-immediate
	fmov d1, d2 // fmov/d
	fmov d3, #-1.9375 // fmov/d-immediate
	fmov h1, h2 // fmov/h
	fmov h3, #-1.9375 // fmov/h-immediate
	fmov w4, s5 // fmov/w-s
	fmov s6, w7 // fmov/s-w
	fmov x4, d5 // fmov/x-d
	fmov d6, x7 // fmov/d-x
	fmov w4, h5 // fmov/w-h
	fmov h6, w7 // fmov/h-w
	fmov x4, h5 // fmov/x-h
	fmov h6, x7 // fmov/h-x
	fmul s8, s9, s10 // fmul/s
	fmul d8, d9, d10 // fmul/d
	fmul h8, h9, h10 // fmul/h
	fdiv s8, s9, s10 // fdiv/s
	fdiv d8, d9, d10 // fdiv/d
	fdiv h8, h9, h10 // fdiv/h
	fadd s8, s9, s10 // fadd/s
	fadd d8, d9, d10 // fadd/d
	fadd h8, h9, h10 // fadd/h
	fsub s8, s9, s10 // fsub/s
	fsub d8, d9, d10 // fsub/d
	fsub h8, h9, h10 // fsub/h
	fmax s8, s9, s10 // fmax/s
	fmax d8, d9, d10 // fmax/d
	fmax h8, h9, h10 // fmax/h
	fmin s8, s9, s10 // fmin/s
	fmin d8, d9, d10 // fmin/d
	fmin h8, h9, h10 // fmin/h
	fmaxnm s8, s9, s10 // fmaxnm/s
	fmaxnm d8, d9, d10 // fmaxnm/d
	fmaxnm h8, h9, h10 // fmaxnm/h
	fminnm s8, s9, s10 // fminnm/s
	fminnm d8, d9, d10 // fminnm/d
	fminnm h8, h9, h10 // fminnm/h
	fnmul s8, s9, s10 // fnmul/s
	fnmul d8, d9, d10 // fnmul/d
	fnmul h8, h9, h10 // fnmul/h
	fabs s11, s12 // fabs/s
	fabs d11, d12 // fabs/d
	fabs h11, h12 // fabs/h
	fneg s11, s12 // fneg/s
	fneg d11, d12 // fneg/d
	fneg h11, h12 // fneg/h
	fsqrt s11, s12 // fsqrt/s
	fsqrt d11, d12 // fsqrt/d
	fsqrt h11, h12 // fsqrt/h
	frintn s11, s12 // frintn/s
	frintn d11, d12 // frintn/d
	frintn h11, h12 // frintn/h
	frintp s11, s12 // frintp/s
	frintp d11, d12 // frintp/d
	frintp h11, h12 // frintp/h
	frintm s11, s12 // frintm/s
	frintm d11, d12 // frintm/d
	frintm h11, h12 // frintm/h
	frintz s11, s12 // frintz/s
	frintz d11, d12 // frintz/d
	frintz h11, h12 // frintz/h
	frinta s11, s12 // frinta/s
	frinta d11, d12 // frinta/d
	frinta h11, h12 // frinta/h
	frintx s11, s12 // frintx/s
	frintx d11, d12 // frintx/d
	frintx h11, h12 // frintx/h
	frinti s11, s12 // frinti/s
	frinti d11, d12 // frinti/d
	frinti h11, h12 // frinti/h
	fcvt s13, d14 // fcvt/s-d
	fcvt s13, h14 // fcvt/s-h
	fcvt d13, s14 // fcvt/d-s
	fcvt d13, h14 // fcvt/d-h
	fcvt h13, s14 // fcvt/h-s
	fcvt h13, d14 // fcvt/h-d
	fmadd s15, s16, s17, s18 // fmadd/s
	fmadd d15, d16, d17, d18 // fmadd/d
	fmadd h15, h16, h17, h18 // fmadd/h
	fmsub s15, s16, s17, s18 // fmsub/s
	fmsub d15, d16, d17, d18 // fmsub/d
	fmsub h15, h16, h17, h18 // fmsub/h
	fnmadd s15, s16, s17, s18 // fnmadd/s
	fnmadd d15, d16, d17, d18 // fnmadd/d
	fnmadd h15, h16, h17, h18 // fnmadd/h
	fnmsub s15, s16, s17, s18 // fnmsub/s
	fnmsub d15, d16, d17, d18 // fnmsub/d
	fnmsub h15, h16, h17, h18 // fnmsub/h
	fcmp s19, s20 // fcmp/s
	fcmp s21, #0.0 // fcmp/s-zero
	fcmp d19, d20 // fcmp/d
	fcmp d21, #0.0 // fcmp/d-zero
	fcmp h19, h20 // fcmp/h
	fcmp h21, #0.0 // fcmp/h-zero
	fcmpe s19, s20 // fcmpe/s
	fcmpe s21, #0.0 // fcmpe/s-zero
	fcmpe d19, d20 // fcmpe/d
	fcmpe d21, #0.0 // fcmpe/d-zero
	fcmpe h19, h20 // fcmpe/h
	fcmpe h21, #0.0 // fcmpe/h-zero
	fccmp s22, s23, #8, mi // fccmp/s
	fccmp d22, d23, #8, mi // fccmp/d
	fccmp h22, h23, #8, mi // fccmp/h
	fccmpe s22, s23, #8, mi // fccmpe/s
	fccmpe d22, d23, #8, mi // fccmpe/d
	fccmpe h22, h23, #8, mi // fccmpe/h
	fcsel s24, s25, s26, vc // fcsel/s
	fcsel d24, d25, d26, vc // fcsel/d
	fcsel h24, h25, h26, vc // fcsel/h
	fcvtns w27, s28 // fcvtns/w-s
	fcvtns w27, d28 // fcvtns/w-d
	fcvtns w27, h28 // fcvtns/w-h
	fcvtns x27, s28 // fcvtns/x-s
	fcvtns x27, d28 // fcvtns/x-d
	fcvtns x27, h28 // fcvtns/x-h
	fcvtnu w27, s28 // fcvtnu/w-s
	fcvtnu w27, d28 // fcvtnu/w-d
	fcvtnu w27, h28 // fcvtnu/w-h
	fcvtnu x27, s28 // fcvtnu/x-s
	fcvtnu x27, d28 // fcvtnu/x-d
	fcvtnu x27, h28 // fcvtnu/x-h
	fcvtas w27, s28 // fcvtas/w-s
	fcvtas w27, d28 // fcvtas/w-d
	fcvtas w27, h28 // fcvtas/w-h
	fcvtas x27, s28 // fcvtas/x-s
	fcvtas x27, d28 // fcvtas/x-d
	fcvtas x27, h28 // fcvtas/x-h
	fcvtau w27, s28 // fcvtau/w-s
	fcvtau w27, d28 // fcvtau/w-d
	fcvtau w27, h28 // fcvtau/w-h
	fcvtau x27, s28 // fcvtau/x-s
	fcvtau x27, d28 // fcvtau/x-d
	fcvtau x27, h28 // fcvtau/x-h
	fcvtps w27, s28 // fcvtps/w-s
	fcvtps w27, d28 // fcvtps/w-d
	fcvtps w27, h28 // fcvtps/w-h
	fcvtps x27, s28 // fcvtps/x-s
	fcvtps x27, d28 // fcvtps/x-d
	fcvtps x27, h28 // fcvtps/x-h
	fcvtpu w27, s28 // fcvtpu/w-s
	fcvtpu w27, d28 // fcvtpu/w-d
	fcvtpu w27, h28 // fcvtpu/w-h
	fcvtpu x27, s28 // fcvtpu/x-s
	fcvtpu x27, d28 // fcvtpu/x-d
	fcvtpu x27, h28 // fcvtpu/x-h
	fcvtms w27, s28 // fcvtms/w-s
	fcvtms w27, d28 // fcvtms/w-d
	fcvtms w27, h28 // fcvtms/w-h
	fcvtms x27, s28 // fcvtms/x-s
	fcvtms x27, d28 // fcvtms/x-d
	fcvtms x27, h28 // fcvtms/x-h
	fcvtmu w27, s28 // fcvtmu/w-s
	fcvtmu w27, d28 // fcvtmu/w-d
	fcvtmu w27, h28 // fcvtmu/w-h
	fcvtmu x27, s28 // fcvtmu/x-s
	fcvtmu x27, d28 // fcvtmu/x-d
	fcvtmu x27, h28 // fcvtmu/x-h
	fcvtzs w27, s28 // fcvtzs/w-s
	fcvtzs w27, d28 // fcvtzs/w-d
	fcvtzs w27, h28 // fcvtzs/w-h
	fcvtzs x27, s28 // fcvtzs/x-s
	fcvtzs x27, d28 // fcvtzs/x-d
	fcvtzs x27, h28 // fcvtzs/x-h
	fcvtzu w27, s28 // fcvtzu/w-s
	fcvtzu w27, d28 // fcvtzu/w-d
	fcvtzu w27, h28 // fcvtzu/w-h
	fcvtzu x27, s28 // fcvtzu/x-s
	fcvtzu x27, d28 // fcvtzu/x-d
	fcvtzu x27, h28 // fcvtzu/x-h
	scvtf s29, w30 // scvtf/s-w
	scvtf d29, w30 // scvtf/d-w
	scvtf h29, w30 // scvtf/h-w
	scvtf s29, x30 // scvtf/s-x
	scvtf d29, x30 // scvtf/d-x
	scvtf h29, x30 // scvtf/h-x
	ucvtf s29, w30 // ucvtf/s-w
	ucvtf d29, w30 // ucvtf/d-w
	ucvtf h29, w30 // ucvtf/h-w
	ucvtf s29, x30 // ucvtf/s-x
	ucvtf d29, x30 // ucvtf/d-x
	ucvtf h29, x30 // ucvtf/h-x
	fcvtzs w0, s1, #32 // fcvtzs/w-s-fixed
	fcvtzs w0, d1, #32 // fcvtzs/w-d-fixed
	fcvtzs w0, h1, #32 // fcvtzs/w-h-fixed
	fcvtzs x0, s1, #64 // fcvtzs/x-s-fixed
	fcvtzs x0, d1, #64 // fcvtzs/x-d-fixed
	fcvtzs x0, h1, #64 // fcvtzs/x-h-fixed
	fcvtzu w0, s1, #32 // fcvtzu/w-s-fixed
	fcvtzu w0, d1, #32 // fcvtzu/w-d-fixed
	fcvtzu w0, h1, #32 // fcvtzu/w-h-fixed
	fcvtzu x0, s1, #64 // fcvtzu/x-s-fixed
	fcvtzu x0, d1, #64 // fcvtzu/x-d-fixed
	fcvtzu x0, h1, #64 // fcvtzu/x-h-fixed
	scvtf s0, w1, #32 // scvtf/s-w-fixed
	scvtf d0, w1, #32 // scvtf/d-w-fixed
	scvtf h0, w1, #32 // scvtf/h-w-fixed
	scvtf s0, x1, #64 // scvtf/s-x-fixed
	scvtf d0, x1, #64 // scvtf/d-x-fixed
	scvtf h0, x1, #64 // scvtf/h-x-fixed
	ucvtf s0, w1, #32 // ucvtf/s-w-fixed
	ucvtf d0, w1, #32 // ucvtf/d-w-fixed
	ucvtf h0, w1, #32 // ucvtf/h-w-fixed
	ucvtf s0, x1, #64 // ucvtf/s-x-fixed
	ucvtf d0, x1, #64 // ucvtf/d-x-fixed
	ucvtf h0, x1, #64 // ucvtf/h-x-fixed
	fjcvtzs w2, d3 // fjcvtzs

// One statement per form of the shipped AArch64 instruction table
// that GNU as takes and llvm-mc rejects, each selecting that form;
// the comment names the entry.
	prfm plil3strm, [x4, #-1] // prfm/unscaled

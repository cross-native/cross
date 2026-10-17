# Branches whose short forms cannot reach their targets relax to near forms;
# the layout loop iterates until stable, as llvm-mc does.
	.text
	.globl relax
relax:
	jmp far
	je near_target
	jne back
back:
	.space 200
	jmp back
near_target:
	.space 300
	je relax
far:
	call external@PLT
	jmp *%rax
	retq

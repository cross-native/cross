/* A static image in the RAM of QEMU's virt machine, which loads it at
   0x40080000, with a 1 MiB stack. */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .got : { *(.got) *(.got.plt) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(16) + 0x100000;
    stack_top = .;
}

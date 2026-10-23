/* A static image in the RAM of QEMU's virt machine, which loads it at
   0x40080000. */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .got : { *(.got) *(.got.plt) }
    .tdata : { tls_start = .; *(.tdata .tdata.*) }
    .tbss : { *(.tbss .tbss.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(16) + 0x4000;
    stack_top = .;
}

/* Code and nearby read-only data at QEMU virt's load address, and data
   more than 1 MiB later at a page offset that the low 12 bits show. */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(0x1000) + 0x123450;
    .data : { *(.data .data.*) }
    .got : { *(.got) }
    .tdata : { *(.tdata .tdata.*) }
    .tbss : { *(.tbss .tbss.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    /* Code that only some linkers take, apart from the rest. */
    .extra 0x40070000 : { *(.extra .extra.*) }
}

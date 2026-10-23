/* Code at QEMU virt's load address and 256 MiB later, in reach of adrp. */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : { *(.text .text.*) }
    . = ALIGN(16) + 0x10000;
    stack_top = .;
    .far 0x50080000 : { *(.far) }
}

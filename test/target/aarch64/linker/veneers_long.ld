/* Code at QEMU virt's load address and more than 4 GiB later, out of the
   reach of adrp. */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : { *(.text .text.*) }
    . = ALIGN(16) + 0x10000;
    stack_top = .;
    .far 0x150080000 : { *(.far) }
}

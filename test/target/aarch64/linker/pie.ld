/* A position-independent image from address 0, which a flat binary of it
   holds from its start: code first, then the dynamic sections and the
   data at fixed addresses, so that the sections linkers size differently
   (such as .dynamic and hash tables) move nothing else. */
ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .dynsym 0x1000 : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .rela.dyn 0x1100 : { *(.rela.dyn) }
    .data.rel.ro 0x2000 : { *(.data.rel.ro .data.rel.ro.*) }
    .got : { *(.got) }
    .data : { *(.data .data.*) }
    .tdata : { tls_start = .; *(.tdata .tdata.*) }
    .tbss : { *(.tbss .tbss.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    .dynamic 0x3000 : { *(.dynamic) }
    .hash 0x4000 : { *(.hash) }
    .gnu.hash : { *(.gnu.hash) }
}

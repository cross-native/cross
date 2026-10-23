/* An image from BASE (0 for a position-independent one) in one segment,
   with the symbols clang_start.s reads. */
ENTRY(_start)
SECTIONS
{
    . = BASE;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .dynsym : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .rela.dyn : { __rela_start = .; *(.rela.dyn) __rela_end = .; }
    . = ALIGN(16);
    .init_array : { __init_array_start = .; KEEP (*(.init_array)) __init_array_end = .; }
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) }
    .dynamic : { *(.dynamic) }
    .got : { *(.got) }
    .data : { *(.data .data.*) }
    .tdata : { __tls_start = .; *(.tdata .tdata.*) }
    .tbss : { *(.tbss .tbss.*) }
    .bss : { *(.bss .bss.*) *(COMMON) . = ALIGN(16); . += 0x1000; stack_top = .; }
}

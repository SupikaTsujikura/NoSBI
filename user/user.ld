OUTPUT_ARCH(riscv)
ENTRY(_start)

USER_BASE = 0x00400000;

PHDRS
{
    text PT_LOAD FLAGS(5);   /* R + X */
    rodata PT_LOAD FLAGS(4); /* R */
    data PT_LOAD FLAGS(6);   /* R + W */
}

SECTIONS
{
    . = USER_BASE;

    .text : ALIGN(16) {
        *(.text .text.*)
    } :text

    . = ALIGN(0x1000);
    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } :rodata

    . = ALIGN(0x1000);
    .data : ALIGN(16) {
        *(.data .data.*)
        *(.sdata .sdata.*)
    } :data

    .bss : ALIGN(16) {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
    } :data
}

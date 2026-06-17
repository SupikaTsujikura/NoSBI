OUTPUT_ARCH(riscv)
ENTRY(_start)

BASE_ADDR = 0x80200000;
STACK_SIZE = 0x4000;

PHDRS
{
    text PT_LOAD FLAGS(5);   /* R + X */
    rodata PT_LOAD FLAGS(4); /* R */
    data PT_LOAD FLAGS(6);   /* R + W */
    bss PT_LOAD FLAGS(6);    /* R + W */
}

SECTIONS
{
    . = BASE_ADDR;

    PROVIDE(kernel_start = .);

    .text : ALIGN(16) {
        PROVIDE(text_start = .);
        *(.text.boot)
        *(.text .text.*)
        PROVIDE(text_end = .);
    } :text

    . = ALIGN(4096);
    .rodata : ALIGN(16) {
        PROVIDE(rodata_start = .);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        PROVIDE(rodata_end = .);
    } :rodata

    . = ALIGN(4096);
    .data : ALIGN(16) {
        PROVIDE(data_start = .);
        *(.data .data.*)
        *(.sdata .sdata.*)
        PROVIDE(data_end = .);
    } :data

    . = ALIGN(4096);
    .bss : ALIGN(16) {
        PROVIDE(bss_start = .);
        *(.bss.stack)
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(.scommon)
        *(COMMON)
        . = ALIGN(4096);
        PROVIDE(bss_end = .);
    } :bss

    PROVIDE(kernel_end = .);
}

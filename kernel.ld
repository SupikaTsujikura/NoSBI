OUTPUT_ARCH(riscv)
ENTRY(_start)

BASE_ADDR = 0x80200000;
STACK_SIZE = 0x4000;

SECTIONS
{
    . = BASE_ADDR;

    PROVIDE(kernel_start = .);

    .text : ALIGN(16) {
        PROVIDE(text_start = .);
        *(.text.boot)
        *(.text .text.*)
        PROVIDE(text_end = .);
    }

    .rodata : ALIGN(16) {
        PROVIDE(rodata_start = .);
        *(.rodata .rodata.*)
        PROVIDE(rodata_end = .);
    }

    .data : ALIGN(16) {
        PROVIDE(data_start = .);
        *(.data .data.*)
        *(.sdata .sdata.*)
        PROVIDE(data_end = .);
    }

    .bss : ALIGN(16) {
        PROVIDE(bss_start = .);
        *(.bss.stack)
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(.scommon)
        *(COMMON)
        . = ALIGN(4096);
        PROVIDE(bss_end = .);
    }

    PROVIDE(kernel_end = .);
}

# 指定架构为riscv 
OUTPUT_ARCH(riscv)

# 指定入口函数为_start
ENTRY(_start)

# 定义变量
# 基地址设置为 0x80200000，因为OpenSBI默认跳转到这个地址
BASE_ADDR = 0x80200000;
KERNEL_END_ADDR = 0x80400000;
# 定义段
SECTIONS {
    . = BASE_ADDR; # 将当前地址设为BASE_ADDR
    # 将大括号内段放入text段
    .text : {
        # PROVIDE表示如果未定义stext，则设置stext为当前地址
        PROVIDE(stext = .);
        *(.text.boot)
        *(.text)
        PROVIDE(etext = .);
    }
    . = ALIGN(4K); # 将下一个地址设为4K对齐
    .data : {
        PROVIDE(sdata = .);
        *(.data .data.*)
        PROVIDE(edata = .);
    }
    . = ALIGN(4K);
    .bss : {
        # 内核栈位置
        .bss.stack = .;
        # 实际的bss段从内核栈之后开始
        PROVIDE(sbss = .);
        *(.bss .bss.*)
        PROVIDE(ebss = .);
    }
    . = KERNEL_END_ADDR;
}
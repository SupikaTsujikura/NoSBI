/**
 * @file start.S
 * @brief 内核启动汇编代码
 * 
 * 该文件包含内核的入口点,初始化栈指针,并调用C语言的main函数。
 */

// 定义引导代码段,这是程序的入口区域
.section .text.boot
// 声明_start符号为全局符号,使链接器能够找到程序入口
.globl _start
// 程序入口点
_start:
    // 设置初始栈指针到预留的栈顶位置
    la sp, _stack_top
    // 调用C语言的main函数
    call main
    // 程序结束后无限循环
    j .

// 定义未初始化数据段,用于存放栈空间
.section .bss
// 对齐到4字节边界
.align 4
// 栈底标签,分配4096字节的栈空间
_stack_bottom:
    .space 4096
// 栈顶标签,指向栈的最高地址
_stack_top:
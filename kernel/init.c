/**
 * @file init.c
 * @brief 主函数实现文件，包含向控制台输出字符串的功能
 *
 * 该文件实现了通过SBI(Supervisor Binary Interface)调用向控制台输出字符的功能，
 * 并在main函数中输出"Hello, RISC-V!"字符串。
 */

#include <stdint.h>

// 定义SBI控制台输出字符的调用号
#define SBI_CONSOLE_PUTCHAR 1

/**
 * @brief 通过SBI调用向控制台输出单个字符
 *
 * @param ch 要输出的字符
 */
static inline void sbi_putchar(char ch)
{
    // 将字符值存储到寄存器a0中
    register uint64_t a0 asm("a0") = ch;
    // 将SBI调用号存储到寄存器a7中
    register uint64_t a7 asm("a7") = SBI_CONSOLE_PUTCHAR;
    // 执行ecall指令发起SBI调用
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

/**
 * @brief 程序主入口函数
 *
 * 输出"Hello, RISC-V!"字符串到控制台
 */
void main(void)
{
    // 定义要输出的消息字符串
    const char *msg = "Hello, RISC-V!\n";
    // 遍历字符串中的每个字符并输出到控制台
    for (const char *p = msg; *p; p++)
        sbi_putchar(*p);

    return;
}
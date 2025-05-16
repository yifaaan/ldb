.global main

.section .data

my_double: .double 64.125

.section .text


.macro trap
    # 调用kill（编号62）
    movq    $62, %rax
    # kill的第一个参数pid
    movq    %r12, %rdi
    # kill的第二个参数SIGTRAP
    movq    $5, %rsi
    # 执行syscall
    syscall
.endm

main:
    # 保存上一个栈帧基址
    push    %rbp
    # 设置新的栈帧基址
    movq    %rsp, %rbp

    # 调用gitpid（编号39）
    movq    $39, %rax
    syscall
    # 将返回值存储在 %r12
    movq    %rax, %r12



    # 写入r13
    movq    $0xaaaaffff, %r13
    trap
    # 写入r13b
    movb    $8, %r13b
    trap
    # 写入mm0
    movq    $0xaaaaffff, %r13
    movq    %r13, %mm0
    trap
    # 写入xmm0
    movsd   my_double(%rip), %xmm0
    trap
    # 写入st0
    emms # 清除 MMX 状态,因为 MMX 和 x87(stx) 共享寄存器
    fldl    my_double(%rip) # 将长双精度浮点数加载到 FPU 栈顶 (st0)
    trap



    # 恢复上一个栈帧基址
    popq    %rbp
    # 返回
    movq    $0, %rax
    ret
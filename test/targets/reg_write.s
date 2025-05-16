.global main

.section .data

hex_format: .asciz "%#x"
float_format: .asciz "%.2f"
long_float_format: .asciz "%.2Lf"

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


    trap

    ## 打印第printf二个参数%rsi的内容
    # 将hex_format的相对地址存储在%rdi，即printf的第一个参数
    leaq    hex_format(%rip), %rdi
    # 用于可变参数的向量寄存器数量 (这里 printf 为 0)
    movq    $0, %rax
    call    printf@plt
    # 调用fflush
    movq    $0, %rdi
    call    fflush@plt
    trap



    ## mm0
    # 将mm0寄存器的内容存储到rsi寄存器，即printf的第二个参数
    movq    %mm0, %rsi
    leaq    hex_format(%rip), %rdi
    movq    $0, %rax
    call    printf@plt
    movq    $0, %rdi
    call    fflush@plt
    trap


    ## xmm0
    leaq    float_format(%rip), %rdi
    # 用于可变参数的向量寄存器数量 (这里 printf 为 1个向量寄存器xmm0，用于参数传递)
    movq    $1, %rax
    call    printf@plt
    movq    $0, %rdi
    call    fflush@plt
    trap

    ## st0
    # 在栈上为 long double 分配 16 字节 (实际需要10字节，16字节用于对齐)
    subq    $16, %rsp
    # 从 FPU 栈弹出 st0 并将其存储在内存栈顶 (%rsp)
    fstpt   (%rsp)
    leaq    long_float_format(%rip), %rdi
    movq    $0, %rax
    call    printf@plt
    movq    $0, %rdi
    call    fflush@plt
    addq    $16, %rsp
    trap



    # 恢复上一个栈帧基址
    popq    %rbp
    # 返回
    movq    $0, %rax
    ret
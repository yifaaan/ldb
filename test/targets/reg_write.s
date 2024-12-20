.global main

.section .data

hex_format:         .asciz "%#x"
float_format:       .asciz "%.2f"
long_float_format:  .asciz "%.2Lf"

.section .text

.macro trap
    # 62 for sys_kill
    movq    $62, %rax
    # sys_kill’s argument
    # first argument: pid
    movq    %r12, %rdi
    # second argument: SIGTRAP
    movq    $5, %rsi
    # execute sys_kill
    syscall
.endm

main:
    # 保存旧的栈帧基址
    push    %rbp
    # 将rsp栈指针的值复制到rbp寄存器，设置新的栈帧基址
    movq    %rsp, %rbp

    # sys_getpid
    movq    $39, %rax
    syscall
    # Save the sys_getpid's return value into r12
    movq    %rax, %r12
    
    # 当前进程发送一个SIGTRAP信号,这通常会导致程序暂停
    trap

    # Print contents of rsi
    # 将hex_format字符串的地址放入rdi，作为printf的第一个参数
    leaq    hex_format(%rip), %rdi
    movq    $0, %rax
    # 调用printf来打印rsi的内容
    call    printf@plt
    movq    $0, %rdi
    call    fflush@plt

    trap  
    
    # Print contents of mm0
    movq    %mm0, %rsi
    leaq    hex_format(%rip), %rdi
    movq    $0, %rax
    call    printf@plt
    movq    $0, %rdi
    call    fflush@plt

    trap

    # Print contents of xmm0
    leaq    float_format(%rip), %rdi
    movq    $1, %rax
    call    printf@plt
    movq    $0, %rdi
    call    fflush@plt

    trap

    # Print contents of st0
    # Allocating 16 bytes on the stack to store the contents of st0
    subq    $16, %rsp
    fstpt   (%rsp)
    leaq    long_float_format(%rip), %rdi
    movq    $0, %rax
    call    printf@plt
    movq    $0, %rdi
    call    fflush@plt
    addq    $16, %rsp

    trap

    # 恢复旧的栈帧基址
    popq    %rbp
    # 设置返回值
    movq    $0, %rax
    ret
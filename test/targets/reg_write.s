# add symbol `main` to the global symbol table
.global main

.section .data

hex_format: .asciz "%#x"
float_format: .asciz "%.2f"

.section .text

.macro trap
    # Trap self
    # Call sys_kill: 62
    movq $62, %rax
    # The first parameter: pid
    movq %r12, %rdi
    # The second parameter: signal
    movq $5, %rsi
    syscall
.endm

main:
    push %rbp
    movq %rsp, %rbp


    # The syscall ID goes in rax.
    # Get pid.
    # getpid syscall number: 39.
    movq $39, %rax
    syscall
    # The return value goes in rax which is pid.
    movq %rax, %r12

    trap

    # Print contents of the second parameter in rsi which is writed by the debugger.
    leaq hex_format(%rip), %rdi
    movq $0, %rax
    call printf@plt
    # Call fflush
    movq $0, %rdi
    call fflush@plt
    # Trap, because the debugger need to write value into mm0
    trap

    # Print contents of mm0
    movq %mm0, %rsi
    leaq hex_format(%rip), %rdi
    movq $0, %rax
    call printf@plt
    # Call fflush
    movq $0, %rdi
    call fflush@plt
    trap


    # Print contents of xmm0
    leaq float_format(%rip), %rdi
    # The second parameter:值为1-8：表示通过XMM0-XMM7传递的浮点参数数量
    movq $1, %rax
    call printf@plt
    movq $0, %rdi
    call fflush@plt
    trap

    popq %rbp
    movq $0, %rax
    ret

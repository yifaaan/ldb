# add symbol `main` to the global symbol table
.global main

.section .data

hex_format: .asciz "%#x"

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

    # Print contents of rsi.
    leaq hex_format(%rip), %rdi
    movq $0, %rax
    call printf@plt
    # Call fflush
    movq $0, %rdi
    call fflush@plt

    popq %rbp
    movq $0, %rax
    ret

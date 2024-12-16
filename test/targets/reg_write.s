.global main

.section .data

hex_format: .asciz "%#x"

.section .text

.macro trap
    # Trap: 62 for sys_kill
    movq $62, %rax
    # first argument: pid
    movq %r12, %rdi
    # second argument: SIGTRAP
    movq $5, %rsi
    syscall
.endm

main:
    push %rbp
    movq $rsp, %rbp

    # Get pid
    movq $39, %rax
    syscall
    # Save the syscall's return value into r12
    movq %rax, %r12

    # Print contents of rsi
    leaq hex_format(%rip), %rdi
    movq $0, %rax
    call printf@plt
    movq $0, %rdi
    call fflush@plt
    trap
    
    popq %rbp
    movq $0, %rax
    ret
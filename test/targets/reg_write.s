.global main

.section .data

.section .text

main:
    push %rbp
    movq $rsp, %rbp
    popq %rbp
    movq $0, %rax
    ret
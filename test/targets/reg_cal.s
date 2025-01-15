.global main


.section .data



.section .text

.macro trap
    # trap: kill SIGTRAP
    movq    $62, %rax
    # kill's argument: pid
    movq    %r12, %rdi
    # kill's argument: SIGTRAP
    movq    $5, %rsi
    syscall
.endm

main:
    push    %rbp
    movq    %rsp, %rbp

    # get pid
    movq    $39, %rax
    syscall
    # save pid to r12
    movq    %rax, %r12


    movq    $10, %r13
    movq    $5, %r14
    addq    %r13, %r14
    trap


    movq    $2, %r13
    movq    $4, %r14
    imulq    %r13, %r14
    trap

    popq    %rbp
    ret
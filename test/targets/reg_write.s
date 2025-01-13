.global main


.section .data

hex_format:     .asciz "%#x"
float_format:   .asciz "%.2f"
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

    trap

    # print contents of rsi
    # printf: first argument
    leaq    hex_format(%rip), %rdi
    movq    $0, %rax
    call    printf@plt
    # fflush: first argument
    movq    $0, %rdi
    call    fflush@plt
    trap

    # print contents of mm0
    movq    %mm0, %rsi
    # printf: first argument
    leaq    hex_format(%rip), %rdi
    movq    $0, %rax
    call    printf@plt
    # fflush: first argument
    movq    $0, %rdi
    call    fflush@plt
    trap

    # print contents of xmm0(sse)
    # printf: first argument
    leaq    float_format(%rip), %rdi
    movq    $1, %rax
    call    printf@plt
    # fflush: first argument
    movq    $0, %rdi
    call    fflush@plt
    trap

    
    popq    %rbp
    ret
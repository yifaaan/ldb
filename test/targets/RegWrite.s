.global main
.section .data
HexFormat: .asciz "%#x"
FloatFormat: .asciz "%.2f"
LongFloatFormat: .asciz "%.2Lf"

.section .text
.macro Trap
    movq $62, %rax # SYS_kill
    movq %r12, %rdi # PID
    movq $5, %rsi # SIGTRAP
    syscall
.endm

main:
    push %rbp
    movq %rsp, %rbp

    # Get PID
    movq $39, %rax # SYS_getpid
    syscall
    movq %rax, %r12 # Save PID in r12

    trap

    # Print contents of rsi
    leaq HexFormat(%rip), %rdi
    movq $0, %rax
    call printf@plt

    movq $0, %rdi
    call fflush@plt
    trap

    ############
    ### MMX ####
    ############
    movq %mm0, %rsi
    leaq HexFormat(%rip), %rdi
    movq $0, %rax
    call printf@plt

    movq $0, %rdi
    call fflush@plt
    trap

    ############
    ### XMM0 ###
    ############
    leaq FloatFormat(%rip), %rdi
    movq $1, %rax
    call printf@plt

    movq $0, %rdi
    call fflush@plt
    trap

    ############
    ### ST0 ####
    subq $16, %rsp
    fstpt (%rsp)
    leaq LongFloatFormat(%rip), %rdi
    movq $0, %rax
    call printf@plt

    movq $0, %rdi
    call fflush@plt
    addq $16, %rsp
    trap

    popq %rbp
    movq $0, %rax
    ret
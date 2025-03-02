# add symbol `main` to the global symbol table
.global main

.section .data
my_double: .double 64.125

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

    ### Test GPRS ###
    # Store to r13
    movq $0xcafecafe, %r13
    trap

    # Store to r13b
    movb $42, %r13b
    trap

    ### Test MMX ###
    # Store to mm0
    movq $0xba5eba11, %r13
    movq %r13, %mm0
    trap

    ### Test XMM(SSE) ###
    # Store to xmm0
    movsd my_double(%rip), %xmm0
    trap

    ### Test x87 ###
    # Store to st0
    emms
    fldl my_double(%rip)
    trap

    popq %rbp
    movq $0, %rax
    ret

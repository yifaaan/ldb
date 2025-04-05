.global main

.section .data

my_double: .double 64.125

.section .text

.macro trap
	# trap (kill(pid, 5)) SIGTRAP
	movq $62, %rax
	movq %r12, %rdi
	movq $5, %rsi
	syscall
.endm

main:
	push %rbp
	movq %rsp, %rbp

	# get pid
	movq $39, %rax
	syscall
	# store pid in r12
	movq %rax, %r12

	# store to r13
	movq $0xcafecafe, %r13
	trap

	# store to r13b
	movb $42, %r13b
	trap

	# store to mm0
	movq $0xba5eba11, %r13
	movq %r13, %mm0
	trap

	# store to xmm0
	movsd my_double(%rip), %xmm0
	trap

	# store to st0
	emms
	fldl my_double(%rip)
	trap



	
	popq %rbp
    movq $0, %rax
    ret
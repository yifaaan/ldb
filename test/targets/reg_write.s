.global main

.section .data

hex_format: .asciz "%#x"

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

	trap

	# print contents of rsi
	leaq hex_format(%rip), %rdi
	movq $0, %rax
	call printf@plt
	movq $0, %rdi
	call fflush@plt
	trap
	

	popq %rbp
    movq $0, %rax
    ret
	.global	main

	.text
main:
	pushq	%rbp
	movq	%rsp, %rbp
	mov	$message, %rdi
	call	puts
	mov	$0, %rax
	popq	%rbp
	ret

message:	
	.asciz	"hello again, world!"

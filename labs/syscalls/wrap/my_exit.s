	.global my_exit

	.text
my_exit:	
	mov	$60, %rax
	mov	$0, %rdi
	syscall

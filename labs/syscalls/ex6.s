	.global	_start

	.text
_start:
	mov	$1, %rax
	mov	$1, %rdi
	mov	$message, %rsi
	mov	$14, %rdx
	syscall
	
	mov	$60, %rax	/* pass # of exit system call in reg. ax */
	mov	$1, %rdi	/* pass 0 as a paramter in reg. di */
	syscall			/* doesn't return */
	
message:
	.ascii	"hello, world!\n"

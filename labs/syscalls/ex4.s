	.global	_start		/* label of program entry point */

	.text
_start:
	nop
	nop
	nop
#	jmp	_start		/* a do-nothing infinite loop */

	.ascii	"This is some data that is not meant to be fetched and"
	.ascii	"executed as machine code.  Sequentially falling through to"
	.ascii	"try to execute this as code would be folly!"

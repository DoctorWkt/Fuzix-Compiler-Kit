	.65c816
	.a16
	.i16

	.export __xoreqxc
	.export __xoreqxcu

__xoreqxc:
__xoreqxcu:
	; A is the pointer X is the value - backwards to how we want!
	stx @tmp
	tax
	rep #0x20
	lda 0,x
	eor @tmp
	sta 0,x
	sep #0x20
	rts

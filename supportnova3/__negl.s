	.export __negl
	.code

__negl:
	sta	3,__tmp,0
	lda	0,__hireg,0
	neg	1,1,snr
	neg	0,0,skp
	com	0,0
	mffp	3
	jmp	@__tmp,0

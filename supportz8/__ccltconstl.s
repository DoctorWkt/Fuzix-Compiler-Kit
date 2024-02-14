;
;	r12-r15 v r0-r3
;
;	TODO: ponder instead putting the constant as the words following ?
;
	.export __ccltconstl
	.export __ccltconstul
	.export __ccgteqconstl
	.export __ccgteqconstul
	.export __ccltconst0l
	.export __ccltconst0ul
	.export __ccgteqconst0l
	.export __ccgteqconst0ul

;	r12-r15 > r0-r3 ?

__ccltconst0ul:
	clr r12
	clr r13
	clr r14
	clr r15
	jr __ccltconstul
__ccltconst0l:
	clr r12
	clr r13
	clr r14
	clr r15
__ccltconstl:
	cp r12,r0
	jr lt,true
	jr nz,false
	jr next

__ccltconstul:
	cp r12,r0
	jr ult,true
	jr nz,false
next:
	cp r13,r1
	jr ult,true
	jr nz,false
	cp r14,r2
	jr ult,true
	jr nz,false
	cp r15,r3
	jr ult,true
false:
	clr r3
	clr r2
	or r3,r3
	ret
true:
	ld r3,#1
	clr r2
	or r3,r3
	ret

__ccgteqconst0l:
	clr r12
	clr r13
	clr r14
	clr r15
__ccgteqconstl:
	call __ccltconstl
	xor r3,#1
	ret

__ccgteqconstul:
	clr r12
	clr r13
	clr r14
	clr r15
__ccgteqconst0ul:
	call __ccltconstul
	xor r3,#1
	ret

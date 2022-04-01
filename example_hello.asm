.org 0
JMP start ; reset vector [0]
.org 8
JMP irq   ; IRQ vector [1]

.org 0x40
start:
	LLI hello/L
	LHI hello/H
	CALL print
	HALT
	JMP start

irq:
	RET

; HL points to the string
print:
	LAM
	CPI 0
	RTZ
	OUT/1
	INL
	JMP print

hello:.set 'h' 'e' 'l' 'l' 'o' '!' 0xa 0

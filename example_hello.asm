.org 0
JMP start ; reset vector [0]
.org 8
RET       ; IRQ vector [1]

.org 0x40
start:
	LLI hello/L
	LHI hello/H
	CALL print
	HALT
	JMP start

; HL points to the string
print:
	LAM
	CPI 0
	RTZ
	OUT/1
	CALL incHL
	JMP print

incHL:
	OUT/7
	LAL
	ADI 0x01
	LLA
	LAH
	ACI 0x00
	LHA
	INP/7
	RET

hello:.set 'h' 'e' 'l' 'l' 'o' '!' 0xa 0

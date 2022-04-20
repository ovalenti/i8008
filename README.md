# Status

This project is in its very early stage and the original purpose is mainly "for fun".

# i8008 assembler

Usage:

```
i8008asm < source.asm > image.bin
```

- The assembler supports usual labels.
- The instruction parameter count is not checked, and address references are implicitely 2 bytes long. It is possible to refer to the low or high part of a symbol address by suffixing it with `/L` or `/H` respectively.
- Data can be appended using the `.set` keyword, as plain number or characters enclosed within single-quotes.
- The INP and OUT ports use a glued syntax appended with a slash character: ex `OUT/0`

Hello world example:

```
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

irq:
	RET

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

hello:.set 'hello!' 0xa 0
```


# i8008 emulator

Usage:

```
i8008emu [-t] image.bin
```

- The memory space is 2K ROM, then 2K RAM.
- The provided image file is loaded as ROM
- With the `-t` flag, instructions are printed to stderr during execution

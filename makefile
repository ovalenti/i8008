all:i8008emu i8008asm run-tests

CFLAGS+=-Wall -g3

i8008emu:i8008emu.o i8008.o

i8008asm:i8008asm.o asm_bler.o

run-tests:tests
	@echo "=== running tests ==="
	@./tests

tests:tests.o asm_bler.o

clean:
	rm -rf *.o tests i8008emu i8008asm

.PHONY:run-tests clean
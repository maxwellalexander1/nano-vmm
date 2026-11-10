CROSS_COMPILE ?= aarch64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
GDB     := gdb

CFLAGS  := -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -static -fno-pie -no-pie -mgeneral-regs-only -g -O2
LDFLAGS := -T linker.ld -nostdlib -Wl,--no-warn-rwx-segments

TARGET  := guest.elf

all: $(TARGET)

$(TARGET): boot.S kernel.c linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) boot.S kernel.c -o $(TARGET)

run: $(TARGET)
	qemu-system-aarch64 -M virt -cpu cortex-a72 -nographic -semihosting -kernel $(TARGET)

debug: $(TARGET)
	qemu-system-aarch64 -M virt -cpu cortex-a72 -nographic -semihosting -kernel $(TARGET) -S -s

clean:
	rm -f $(TARGET) *.o
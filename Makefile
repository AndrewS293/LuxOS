CC=i686-elf-gcc
LD=i686-elf-ld
AS=i686-elf-as
CFLAGS=-m32 -ffreestanding -O2 -Wall -Wextra -nostdlib -fno-builtin -Iinclude
LDFLAGS=-m elf_i386 -T linker.ld

OBJS = src/entry.o src/kernel.o src/string.o
all: kernel.bin

src/entry.o: src/entry.S
	$(AS) --32 -o $@ $<

src/kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

src/string.o: src/string.c
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<


kernel.bin: src/entry.o src/kernel.o src/string.o 
	$(LD) $(LDFLAGS) -o $@ src/entry.o src/kernel.o src/string.o 

iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/
	cp boot/grub/grub.cfg iso/boot/grub/
	grub-mkrescue -o kernal.iso iso || echo "grub-mkrescue failed - ensure grub is installed"

clean:
	rm -f src/*.o kernel.bin iso/kernel.iso kernel.iso



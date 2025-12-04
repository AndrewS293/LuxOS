.section .multiboot
.align 4
.long 0x1BADB002
.long 0x0
.long - (0x1BADB002 + 0x0)

.section .text
.globl _start
.type _start, @function
_start:
    xor %ebp, %ebp
    mov $0x90000, %esp
    call kernel_main
1:  jmp 1b


    
.section .bss
    .lcomm stack, 0x4000  # 16 KB stack
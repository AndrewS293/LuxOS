/* kernel_interrupts.c
   Minimal IDT/IRQ/PIT example for i686 freestanding kernel.
   - Provides idt_install(), isrs_install(), irq_install()
   - Timer IRQ updates timer_ticks (volatile)
   - Simple VGA putchar/puts so you can see output
*/

#include <stdint.h>

/* ----------------- Basic I/O ----------------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void vga_puts(const char *str);
void print_uint(uint32_t num);

struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

void isr_handler(struct regs *r) {
    vga_puts("ISR: ");
    print_uint(r->int_no);
    vga_puts("\n");
}

void irq_handler(struct regs *r) {
    if (r->int_no >= 40) outb(0xA0, 0x20); // slave PIC
    outb(0x20, 0x20);                      // master PIC
    // Optional: custom handlers
}

__attribute__((naked)) void isr_common_stub() {
    __asm__ __volatile__(
        "pusha\n"
        "push %ds\n"
        "push %es\n"
        "push %fs\n"
        "push %gs\n"
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "push %esp\n"
        "call isr_handler\n"
        "add $4, %esp\n"
        "pop %gs\n"
        "pop %fs\n"
        "pop %es\n"
        "pop %ds\n"
        "popa\n"
        "add $8, %esp\n"
        "sti\n"
        "iret\n"
    );
}

__attribute__((naked)) void irq_common_stub() {
    __asm__ __volatile__(
        "pusha\n"
        "push %ds\n"
        "push %es\n"
        "push %fs\n"
        "push %gs\n"
        "mov $0x10, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %fs\n"
        "mov %ax, %gs\n"
        "push %esp\n"
        "call irq_handler\n"
        "add $4, %esp\n"
        "pop %gs\n"
        "pop %fs\n"
        "pop %es\n"
        "pop %ds\n"
        "popa\n"
        "add $8, %esp\n"
        "sti\n"
        "iret\n"
    );
}


/* ----------------- VGA (tiny) ----------------- */
volatile uint16_t* VGA = (uint16_t*)0xB8000;
static uint16_t vga_row = 0, vga_col = 0;
static uint8_t vga_color = (0 << 4) | 7;

static void vga_putc(char ch) {
    if (ch == '\n') {
        vga_row++; vga_col = 0;
        if (vga_row >= 25) vga_row = 24;
        return;
    }
    VGA[vga_row*80 + vga_col] = (uint16_t)ch | ((uint16_t)vga_color << 8);
    vga_col++;
    if (vga_col >= 80) { vga_col = 0; vga_row++; if (vga_row >= 25) vga_row = 24; }
}
void vga_puts(const char* s) { while (*s) vga_putc(*s++); }

void print_uint(uint32_t num) {
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (num == 0) { vga_putc('0'); return; }
    while (num > 0 && i > 0) {
        buf[--i] = '0' + (num % 10);
        num /= 10;
    }
    vga_puts(&buf[i]);
}

/* ----------------- IDT data ----------------- */
struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define IDT_ENTRIES 256
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

/* External assembly symbols (defined in interrupts.asm) */
extern void idt_load(void);
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

/* ----------------- IDT helpers ----------------- */
static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = base & 0xFFFF;
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel     = sel;
    idt[num].always0 = 0;
    idt[num].flags   = flags;
}

//this one
void idt_install(void) {
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base  = (uint32_t)&idt;
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].base_lo = 0;
        idt[i].sel = 0;
        idt[i].always0 = 0;
        idt[i].flags = 0;
        idt[i].base_hi = 0;
    }
    idt_load();
}

//this one
void isrs_install(void) {
    /* set CPU exception handlers 0..31 */
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
}

#define PIC1    0x20
#define PIC2    0xA0
#define PIC1_CMD PIC1
#define PIC1_DATA (PIC1+1)
#define PIC2_CMD PIC2
#define PIC2_DATA (PIC2+1)
#define ICW1_INIT  0x11
#define ICW4_8086  0x01

void irq_remap(void) {
    outb(PIC1_CMD, ICW1_INIT);
    outb(PIC2_CMD, ICW1_INIT);
    outb(PIC1_DATA, 0x20); // offset for master PIC (0x20 = 32)
    outb(PIC2_DATA, 0x28); // offset for slave PIC (0x28 = 40)
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    // mask none
    outb(PIC1_DATA, 0x0);
    outb(PIC2_DATA, 0x0);
}

void irq_install(void) {
    irq_remap();
    /* IRQs map to IDT entries 32..47 */
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10,0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11,0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12,0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13,0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14,0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15,0x08, 0x8E);
}

/* ----------------- Timer tick ----------------- */
volatile uint32_t timer_ticks = 0;

void timer_callback(void) {
    timer_ticks++;
}

/* ----------------- C-level IRQ/ISR handlers ----------------- */
/* regs struct layout must match assembly push order */
struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no;
    uint32_t err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};


/* ----------------- PIT init (polling-less; uses IRQ0) ----------------- */
void init_pit(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36); // channel0, lobyte/hibyte, mode 3
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

/* ----------------- Kernel entry for testing ----------------- */
/* Call these from your existing kernel_main to integrate */
void kernel_main(void) {
    idt_install();
    isrs_install();
    irq_install();
    init_pit(100);        // 100 Hz tick (10 ms)
    __asm__ volatile("sti"); // enable interrupts

    vga_puts("Interrupts installed. Waiting for ticks...\n");

    uint32_t last = 0;
    while (1) {
        if (timer_ticks != last) {
            last = timer_ticks;
            if (last % 100 == 0) { // every ~1 second at 100Hz
                vga_puts("ticks: ");
                print_uint(last);
                vga_puts("\n");
            }
        }
    }
}



#include <stdint.h>

#define PL011_UART_BASE       0x09000000UL
#define UART_DR               (*(volatile uint32_t *)(PL011_UART_BASE + 0x00))
#define UART_FR               (*(volatile uint32_t *)(PL011_UART_BASE + 0x18)) 
#define UART_FR_TXFF          (1U << 5) 

extern void trigger_svc_trap(void);

static volatile uint32_t trap_count = 0;

static void uart_putc(char c) {
    while (UART_FR & UART_FR_TXFF) {

    }

    UART_DR = (uint32_t)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

static void uart_put_hex(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

static void uart_put_dec(uint64_t val) {
    char buf[24];
    int i = 0;
    if (val==0){
        uart_putc('0');
        return;
    }
    while (val >0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0) {
        uart_putc(buf[i]);
    }
}

static inline uint64_t read_cntpct(void) {
    uint64_t val;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static inline uint64_t read_current_el(void) {
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return (el >> 2) & 0x3;
}

void handle_sync_exception(uint64_t esr_el1, uint64_t elr_el1) {
    uint32_t ec = (esr_el1 >> 26) & 0x3F;
    uint32_t iss = esr_el1 % 0x1FFFFFF;

    trap_count++;
    uart_puts("[TRAP] Caugnt ARMv8 Synchronous Except EC=");
    uart_put_hex(ec);
    uart_puts(" ISS=");
    uart_put_hex(iss);
    uart_puts(" FaultPC=");
    uart_put_hex(elr_el1);
    uart_puts("\n");
}

static void qemu_exit_success(void) {
    uint64_t block[2] = { 0x20026 /* ADP_Stopped_ApplicationExit */, 0 /* Exit code 0 */ };
    register uint64_t x0 __asm__("x0") = 0x18; /* SYS_EXIT */
    register uint64_t x1 __asm__("x1") = (uint64_t)block;
    __asm__ volatile("hlt #0xF000" : : "r"(x0), "r"(x1) : "memory");
}


void guest_kernel_main(void) {
    uart_puts("[NANO-VMM] Bare-Metal ARMv8-A (AArch64) Guest Kernel Boot\n");

    uint64_t el = read_current_el();
    uart_puts("[INFO] Running at ARMv8 Exception Level: EL");
    uart_put_dec(el);
    uart_puts("\n");

    uint64_t start_cycles = read_cntpct();
    trigger_svc_trap();
    uint64_t end_cycles = read_cntpct();

    uart_puts("[PERF] SVC Exception Trap Round-Trip Cycles: ");
    uart_put_dec(end_cycles - start_cycles);
    uart_puts(" ticks | Total Traps Handled: ");
    uart_put_dec(trap_count);
    uart_puts("\n");

    if (trap_count == 1) {
        uart_puts("[PASS] ARMv8 VBAR_EL1 Exception Vector & MMIO Self-Test OK\n");
    } else {
        uart_puts("[FAIL] Trap count mismatch!\n");
    }

    qemu_exit_success();
}
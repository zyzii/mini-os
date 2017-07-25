#ifndef _ARM32_OS_H
#define _ARM32_OS_H

#define BUG() while(1){asm volatile (".word 0xe7f000f0\n");} /* Undefined instruction; will call our fault handler. */

static inline void local_irq_disable(void) {
    __asm__ __volatile__("cpsid i":::"memory");
}

static inline void local_irq_enable(void) {
    __asm__ __volatile__("cpsie i":::"memory");
}

#define local_irq_save(x) { \
    __asm__ __volatile__("mrs %0, cpsr;cpsid i":"=r"(x)::"memory");    \
}

#define local_irq_restore(x) {    \
    __asm__ __volatile__("msr cpsr_c, %0"::"r"(x):"memory");    \
}

#define local_save_flags(x)    { \
    __asm__ __volatile__("mrs %0, cpsr":"=r"(x)::"memory");    \
}

/* We probably only need "dmb" here, but we'll start by being paranoid. */
#define mb() __asm__("dsb":::"memory");
#define rmb() __asm__("dsb":::"memory");
#define wmb() __asm__("dsb":::"memory");

/* The AAPCS requires the callee (e.g. __arch_switch_threads) to preserve r4-r11. */
#define CALLEE_SAVED_REGISTERS 8

#endif

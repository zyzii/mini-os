#ifndef _ARM32_OS_H
#define _ARM32_OS_H

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

#endif

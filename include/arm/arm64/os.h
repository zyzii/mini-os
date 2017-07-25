#ifndef _ARM64_OS_H_
#define _ARM64_OS_H_

#define BUG()           __asm__ __volatile("wfi" ::: "memory")

static inline void local_irq_disable(void)
{
    __asm__ __volatile__("msr daifset, #2":::"memory");
}

static inline void local_irq_enable(void)
{
    __asm__ __volatile__("msr daifclr, #2":::"memory");
}

#define local_irq_save(x) { \
    __asm__ __volatile__("mrs %0, daif; msr daifset, #2":"=r"(x)::"memory"); \
}

#define local_irq_restore(x) { \
    __asm__ __volatile__("msr daif, %0"::"r"(x):"memory"); \
}

#define local_save_flags(x) { \
    __asm__ __volatile__("mrs %0, daif":"=r"(x)::"memory"); \
}

/* The Callee-saved registers : x19 ~ x29 */
#define CALLEE_SAVED_REGISTERS 11

#endif

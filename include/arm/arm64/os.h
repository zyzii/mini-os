#ifndef _ARM64_OS_H_
#define _ARM64_OS_H_

static inline void local_irq_disable(void)
{
    __asm__ __volatile__("msr daifset, #2": : :"memory");
}

static inline void local_irq_enable(void)
{
    __asm__ __volatile__("msr daifclr, #2": : :"memory");
}

#define local_irq_save(x) { \
    __asm__ __volatile__("mrs %0, daif; msr daifset, #2":"=r"(x)::"memory"); \
}

#define local_irq_restore(x) { \
    __asm__ __volatile__("msr daif, %0": :"r"(x):"memory"); \
}

#define local_save_flags(x) { \
    __asm__ __volatile__("mrs %0, daif":"=r"(x): :"memory"); \
}

#define isb()           __asm__ __volatile("isb" : : : "memory")

#define dmb(opt)        __asm__ __volatile("dmb " #opt : : : "memory")
#define dsb(opt)        __asm__ __volatile("dsb " #opt : : : "memory")

#define mb()            dmb(sy) /* Full system memory barrier all */
#define wmb()           dmb(st) /* Full system memory barrier store */
#define rmb()           dmb(ld) /* Full system memory barrier load */

/* The Callee-saved registers : x19 ~ x29 */
#define CALLEE_SAVED_REGISTERS 11

#endif

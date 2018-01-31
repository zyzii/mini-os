#ifndef __ARM32_TIME_H
#define __ARM32_TIME_H

static inline uint64_t read_virtual_count(void)
{
    uint32_t c_lo, c_hi;
    __asm__ __volatile__("mrrc p15, 1, %0, %1, c14":"=r"(c_lo), "=r"(c_hi));
    return (((uint64_t) c_hi) << 32) + c_lo;
}

/* Set the timer and mask. */
static inline void write_timer_ctl(uint32_t value)
{
    __asm__ __volatile__("mcr p15, 0, %0, c14, c3, 1\n"
                         "isb"::"r"(value));
}

static inline void set_vtimer_compare(uint64_t value)
{
    __asm__ __volatile__("mcrr p15, 3, %0, %H0, c14" ::"r"(value));

    /* Enable timer and unmask the output signal */
    write_timer_ctl(1);
}

#endif

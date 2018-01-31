#ifndef __ARM64_TIME_H
#define __ARM64_TIME_H

static inline uint64_t read_virtual_count(void)
{
    uint64_t c;

    __asm__ __volatile__("mrs %0, cntvct_el0":"=r"(c));
    return c;
}

/* Set the timer and mask. */
static inline void write_timer_ctl(uint32_t value)
{
    __asm__ __volatile__("msr cntv_ctl_el0, %0" :: "r" (value));
}

static inline void set_vtimer_compare(uint64_t value)
{
    __asm__ __volatile__("msr cntv_cval_el0, %0" : : "r" (value));

    /* Enable timer and unmask the output signal */
    write_timer_ctl(1);
}

static inline uint32_t read_frequency(void)
{
    uint32_t counter_freq;

    __asm__ __volatile__("mrs %0, cntfrq_el0":"=r"(counter_freq));
    return counter_freq;
}

#endif

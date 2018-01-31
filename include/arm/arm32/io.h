#ifndef __ARM32_IO_H__
#define __ARM32_IO_H__

static inline uint32_t REG_READ32(volatile uint32_t *addr)
{
    uint32_t value;
    __asm__ __volatile__("ldr %0, [%1]":"=&r"(value):"r"(addr));
    rmb();
    return value;
}

static inline void REG_WRITE32(volatile uint32_t *addr, unsigned int value)
{
    __asm__ __volatile__("str %0, [%1]"::"r"(value), "r"(addr));
    wmb();
}

#endif

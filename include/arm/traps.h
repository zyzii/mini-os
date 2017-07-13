#ifndef _TRAPS_H_
#define _TRAPS_H_

#if defined(__arm__)
struct pt_regs {
    unsigned long r0;
    unsigned long r1;
    unsigned long r2;
    unsigned long r3;
    unsigned long r4;
    unsigned long r5;
    unsigned long r6;
    unsigned long r7;
    unsigned long r8;
    unsigned long r9;
    unsigned long r10;
    unsigned long r11;
    unsigned long r12;
};
#elif defined(__aarch64__)
struct pt_regs {
    /* From x0 ~ x29 */
    uint64_t x[30];
    union {
        uint64_t x30;
        uint64_t lr;
    };
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};
#endif

#endif

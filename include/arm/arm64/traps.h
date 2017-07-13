#ifndef _TRAPS_H_
#define _TRAPS_H_

#ifndef __ASSEMBLY__
struct pt_regs {
    uint64_t sp;
    uint64_t pc;
    uint64_t lr;  /* elr */
    uint32_t pstate;
    uint32_t esr;

    /* From x0 ~ x29 */
    uint64_t x[30];
};

#else

#define PT_REG_SIZE   (272)

#define PT_REG_SP     (0)
#define PT_REG_ELR    (16)
#define PT_REG_SPSR   (24)
#define PT_REG_X      (32)

#endif

#endif

#include <mini-os/os.h>
#include <mini-os/traps.h>
#include <console.h>

static void dump_regs(struct pt_regs *regs,
                  unsigned long esr, unsigned long far)
{
    printk("*** Sync exception at PC = %lx *** \n", regs->pc);
    printk("Thread state:\n");
    printk("\tX0  = 0x%016lx X1  = 0x%016lx\n", regs->x[0], regs->x[1]);
    printk("\tX2  = 0x%016lx X3  = 0x%016lx\n", regs->x[2], regs->x[3]);
    printk("\tX4  = 0x%016lx X5  = 0x%016lx\n", regs->x[4], regs->x[5]);
    printk("\tX6  = 0x%016lx X7  = 0x%016lx\n", regs->x[6], regs->x[7]);
    printk("\tX8  = 0x%016lx X9  = 0x%016lx\n", regs->x[8], regs->x[9]);
    printk("\tX10 = 0x%016lx X11 = 0x%016lx\n", regs->x[10], regs->x[11]);
    printk("\tX12 = 0x%016lx X13 = 0x%016lx\n", regs->x[12], regs->x[13]);
    printk("\tX14 = 0x%016lx X15 = 0x%016lx\n", regs->x[14], regs->x[15]);
    printk("\tX16 = 0x%016lx X17 = 0x%016lx\n", regs->x[16], regs->x[17]);
    printk("\tX18 = 0x%016lx X19 = 0x%016lx\n", regs->x[18], regs->x[19]);
    printk("\tX20 = 0x%016lx X21 = 0x%016lx\n", regs->x[20], regs->x[21]);
    printk("\tX22 = 0x%016lx X23 = 0x%016lx\n", regs->x[22], regs->x[23]);
    printk("\tX24 = 0x%016lx X25 = 0x%016lx\n", regs->x[24], regs->x[25]);
    printk("\tX26 = 0x%016lx X27 = 0x%016lx\n", regs->x[26], regs->x[27]);
    printk("\tX28 = 0x%016lx X29 = 0x%016lx\n", regs->x[28], regs->x[29]);
    printk("\tX30 (lr) = 0x%016lx\n", regs->lr);
    printk("\tsp  = 0x%016lx\n", regs->sp);
    printk("\tpstate  = 0x%016lx\n", regs->pstate);
    printk("\tesr_el1 = %08lx\n", esr);
    printk("\tfar_el1 = %08lx\n", far);
}

void do_bad_mode(struct pt_regs *regs, int reason,
                  unsigned long esr, unsigned long far)
{
    printk(" Bad abort number : %d\n", reason);
    dump_regs(regs, esr, far);
    do_exit();
}

void do_sync(struct pt_regs *regs, unsigned long esr, unsigned long far)
{
    dump_regs(regs, esr, far);
    do_exit();
}

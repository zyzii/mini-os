#include <mini-os/os.h>
#include <mini-os/arm64/traps.h>
#include <console.h>

void do_bad_mode(struct pt_regs *regs, int reason,
                  unsigned long esr, unsigned long far)
{
    /* TO DO */
    do_exit();
}

void do_sync(struct pt_regs *regs, unsigned long esr, unsigned long far)
{
    /* TO DO */
    do_exit();
}

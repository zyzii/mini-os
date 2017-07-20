#include "asm.h"
#include <arm64/pagetable.h>
#include <xen/xen.h>

/* This macro will use the x0/x1/x2/x16 */
#define PRINT(_s)                              \
    adr     x2, 97f;                           \
    adr     x1, 98f;                           \
    sub     x1, x1, x2;                        \
    mov     x0, #CONSOLEIO_write;              \
    mov     x16, #__HYPERVISOR_console_io;     \
    hvc     #XEN_HYPERCALL_TAG;                \
    b       99f;                               \
97: .asciz _s;                                 \
98: ;                                          \
    .align  2;                                 \
99:                                            \

    .macro mem_clear, addr, len
    adr     x0, \addr
    add     x1, x0, \len
1:  stp     xzr, xzr, [x0], #16
    stp     xzr, xzr, [x0], #16
    stp     xzr, xzr, [x0], #16
    stp     xzr, xzr, [x0], #16
    cmp     x0, x1
    b.lo    1b
    .endm

    .data
    .globl _boot_stack
    .globl boot_l1_pgtable, boot_l2_pgtable, boot_l2_pgtable1
    .globl idmap_pgtable, shared_info

    .align 12
boot_l1_pgtable:
    .space PAGE_SIZE
boot_l2_pgtable:
    .space PAGE_SIZE
boot_l2_pgtable1:
    .space PAGE_SIZE
idmap_pgtable:
    .space PAGE_SIZE
shared_info:
    .space PAGE_SIZE

    .align 12
_boot_stack:
    .space (4 * PAGE_SIZE)
stack_end:

/*
 * Kernel startup entry point.
 *
 * Please refer to kernel file Documentation/arm64/booting.txt
 * for the header format.
 */
    .text

    b       _start                  /* branch to kernel start, magic */
    .long   0                       /* reserved */
    .quad   0x0                     /* Image load offset from start of RAM */
    .quad   _end - _start           /* Effective Image size */
    .quad   2                       /* kernel flages: LE, 4K page size */
    .quad   0                       /* reserved */
    .quad   0                       /* reserved */
    .quad   0                       /* reserved */
    .byte   0x41                    /* Magic number, "ARM\x64" */
    .byte   0x52
    .byte   0x4d
    .byte   0x64
    .long   0                       /* reserved */

/*
 * Primary CPU general-purpose register settings
 * x0 = physical address of device tree blob (dtb) in system RAM.
 * x1 = 0 (reserved for future use)
 * x2 = 0 (reserved for future use)
 * x3 = 0 (reserved for future use)
 *
 * The registers used by _start:
 * x20 - FDT pointer
 * x22 - offset between PA and VA
 */
ENTRY(_start)
    /* Save the FDT pointer */
    mov     x20, x0

    /* Calculate where we are */
    bl      _calc_offset

    PRINT("- Mini-OS booting -\n")

    PRINT("- Setup CPU -\n")
    /* Setup CPU for turning on the MMU. */
    bl      _setup_cpu

    PRINT("- Setup booting pagetable -\n")
    /* Setup the initial page table. */
    bl      _setup_initial_pgtable

    /* Setup the identity mapping */
    bl      _setup_idmap_pgtable

    /* Load TTBRx */
    msr     ttbr1_el1, x4
    msr     ttbr0_el1, x5
    isb

    /* Load the exception vectors */
    ldr     x2, =vector_table
    msr     vbar_el1, x2
    isb

    /* Turning on MMU */
    tlbi    vmalle1
    dsb     nsh
    isb
    ldr     x1, =(SCTLR_M | SCTLR_C | SCTLR_I)
    msr     sctlr_el1, x1
    isb

    PRINT("- MMU on -\n")
    ldr     x0, =mmu_on
    br      x0

mmu_on:
    /* Setup stack */
    PRINT("- Setup stack -\n")
    mem_clear _boot_stack,#(4*PAGE_SIZE)

    ldr     x1, =stack_end
    mov     sp, x1

    PRINT("- Jumping to C entry -\n")
    add     x20, x20, x22
    mov     x0, x20                  /* x0 <- device tree (virtual address) */
    mov     x1, x22                  /* x1 <- phys_offset */

    b      arch_init
ENDPROC(_start)

/*
 * Get the phys-offset, and save it in x22
 */
ENTRY(_calc_offset)
    ldr     x22, =_start             /* x0 := vaddr(_start)  */
    adr     x21, _start              /* x21 := paddr(_start) */
    sub     x22, x22, x21            /* x22 := phys-offset (vaddr - paddr) */
    ret
ENDPROC(_calc_offset)

/*
 * Setup the memory region attribute;
 * Setup the TCR.
 */
ENTRY(_setup_cpu)
    /*
     * Setup memory attribute type tables
     *
     * Memory region attributes for LPAE:
     *
     *   n = AttrIndx[2:0]
     *                      n       MAIR
     *   DEVICE_nGnRnE      000     00000000 (0x00)
     *   DEVICE_nGnRE       001     00000100 (0x04)
     *   DEVICE_GRE         010     00001100 (0x0c)
     *   NORMAL_NC          011     01000100 (0x44)
     *   NORMAL             100     11111111 (0xff)
     */
    ldr     x0, =(SET_MAIR(0x00, MEM_DEVICE_nGnRnE) | \
                  SET_MAIR(0x04, MEM_DEVICE_nGnRE)  | \
                  SET_MAIR(0x0c, MEM_DEVICE_GRE)    | \
                  SET_MAIR(0x44, MEM_NORMAL_NC)     | \
                  SET_MAIR(0xff, MEM_NORMAL))
    msr     mair_el1, x0

    /*
     * Setup translation control register (TCR)
     */
    ldr     x0, =(TCR_TxSZ(VA_BITS) | TCR_ASID16 | TCR_TG1_4K  | TCR_FLAGS )
    msr     tcr_el1, x0

    ret
ENDPROC(_setup_cpu)


/*
 * Setup the mapping for code section and device tree
 *
 * => x20 = device tree address
 * <= x4 -> for TTBR1_EL1
 */
ENTRY(_setup_initial_pgtable)
    /* Clear page tables */
    mem_clear boot_l1_pgtable,#PAGE_SIZE
    mem_clear boot_l2_pgtable,#PAGE_SIZE
    mem_clear boot_l2_pgtable1,#PAGE_SIZE

    adr     x4, boot_l1_pgtable        /* x4 := paddr (boot_l1_pgtable) */
    adr     x5, boot_l2_pgtable        /* x5 := paddr (boot_l2_pgtable) */

    /* Find the size of the kernel */
    ldr     x0, =_text                 /* x0 := vaddr(_text)            */
    ldr     x1, =_end                  /* x1 := vaddr(_end)             */
    sub     x2, x1, x0
    /* Get the number of l2 pages to allocate, rounded down */
    lsr     x2, x2, #L2_SHIFT
    /* Add 2 MiB for rounding above */
    add     x2, x2, #1                 /* x2 := total number of entries */

    /* Find the table index */
    lsr     x3, x0, #L2_SHIFT          /* L2_SHIFT = 21                 */
    and     x3, x3, #Ln_ADDR_MASK      /* x3 := index of l2 table       */


    /* Build the L2 block entries */
    sub     x6, x0, x22                /* x6 := paddr(_text)            */
    lsr     x6, x6, #L2_SHIFT          /* L2_SHIFT = 21                 */
    mov     x7, #PT_MEM
1:  orr     x7, x7, x6, lsl #L2_SHIFT  /* x7 := l2 pgtbl entry content  */

    /* Store the entry */
    str     x7, [x5, x3, lsl #3]

    /* Clear the address bits */
    and     x7, x7, #ATTR_MASK_L

    sub     x2, x2, #1
    add     x3, x3, #1
    add     x6, x6, #1
    cbnz    x2, 1b

    /* Link the l1 -> l2 table */
    /* Find the table index */
    lsr     x3, x0, #L1_SHIFT
    and     x3, x3, #Ln_ADDR_MASK       /* x3 := index of l1 table */

    /* Build the L1 page table entry */
    ldr     x7, =PT_PT
    lsr     x9, x5, #12
    orr     x7, x7, x9, lsl #12

    /* Store the L1 entry */
    str     x7, [x4, x3, lsl #3]

    /* Start to map the Device-Tree */
    lsr     x3, x20, #L1_SHIFT
    and     x3, x3, #Ln_ADDR_MASK       /* x3 := index of l1 table */
    ldr     x0, [x4, x3, lsl #3]
    cbz     x0, 2f

    /* Setup the new l2 page table */
    ldr     x7, =PT_PT
    adr     x6, boot_l2_pgtable1
    lsr     x9, x6, #12
    orr     x7, x7, x9, lsl #12

    /* Store the L1 entry */
    str     x7, [x4, x3, lsl #3]

    mov     x5, x6
2:
    mov     x8, x5                      /* x8 := the l2 page table */

    lsr     x3, x20, #L2_SHIFT
    and     x3, x3, #Ln_ADDR_MASK       /* x3 := index of l2 table */

    /* Build the L2 block entries */
    mov     x6, x20
    lsr     x6, x6, #L2_SHIFT
    mov     x7, #PT_MEM
    orr     x7, x7, x6, lsl #L2_SHIFT   /* x7 := l2 pgtbl entry content */

    /* Store the entry */
    str     x7, [x8, x3, lsl #3]

    dsb     sy
    ret
ENDPROC(_setup_initial_pgtable)

/*
 * Setup the page table for TTBR0_EL1:
 *   Mapping the page table for the code section.
 *   We use 39bit address, and just use level 1
 *   for the mapping (we do not use level 0, level 2 and level 3).
 *
 * => none
 * <= x5 : save the page table pointer for TTBR0_EL1.
 */
ENTRY(_setup_idmap_pgtable)
    /* Clear identity mapping page table */
    mem_clear idmap_pgtable,#PAGE_SIZE

    adr     x5, idmap_pgtable

    /* Create the VA = PA map */
    adr     x6, _text

    /* Find the table index */
    lsr     x0, x6, #L1_SHIFT
    and     x0, x0, #Ln_ADDR_MASK       /* x0 := index of l1 table */

    /* Build the L1 block entry */
    ldr     x1, =PT_MEM
    lsr     x2, x6, #L1_SHIFT
    orr     x1, x1, x2, lsl #L1_SHIFT

    /* Store the entry */
    str     x1, [x5, x0, lsl #3]

    dsb     sy
    ret
ENDPROC(_setup_idmap_pgtable)

/* please refer to struct pt_regs{} in traps.h */
#define FRAME_SIZE 272

    .macro trap_entry, el
    sub   sp, sp, #FRAME_SIZE
    stp   x0, x1, [sp, #16 * 0]
    stp   x2, x3, [sp, #16 * 1]
    stp   x4, x5, [sp, #16 * 2]
    stp   x6, x7, [sp, #16 * 3]
    stp   x8, x9, [sp, #16 * 4]
    stp   x10, x11, [sp, #16 * 5]
    stp   x12, x13, [sp, #16 * 6]
    stp   x14, x15, [sp, #16 * 7]
    stp   x16, x17, [sp, #16 * 8]
    stp   x18, x19, [sp, #16 * 9]
    stp   x20, x21, [sp, #16 * 10]
    stp   x22, x23, [sp, #16 * 11]
    stp   x24, x25, [sp, #16 * 12]
    stp   x26, x27, [sp, #16 * 13]
    stp   x28, x29, [sp, #16 * 14]

    .if     \el == 0
    mrs     x21, sp_el0
    .else
    add     x21, sp, #FRAME_SIZE
    .endif

    stp     x30, x21, [sp, #16 * 15]

    mrs     x22, elr_el1
    mrs     x23, spsr_el1
    stp     x22, x23, [sp, #16 * 16]
    .endm

    .macro trap_exit, el
    ldp   x22, x23, [sp, #16 * 16]

    /* restore the elr and spsr */
    msr   elr_el1, x22
    msr   spsr_el1, x23

    /* restore the lr(x30) and sp_el0 */
    ldp   x30, x21, [sp, #16 * 15]

    .if     \el == 0
    msr     sp_el0, x21
    .endif

    ldp   x0, x1, [sp, #16 * 0]
    ldp   x2, x3, [sp, #16 * 1]
    ldp   x4, x5, [sp, #16 * 2]
    ldp   x6, x7, [sp, #16 * 3]
    ldp   x8, x9, [sp, #16 * 4]
    ldp   x10, x11, [sp, #16 * 5]
    ldp   x12, x13, [sp, #16 * 6]
    ldp   x14, x15, [sp, #16 * 7]
    ldp   x16, x17, [sp, #16 * 8]
    ldp   x18, x19, [sp, #16 * 9]
    ldp   x20, x21, [sp, #16 * 10]
    ldp   x22, x23, [sp, #16 * 11]
    ldp   x24, x25, [sp, #16 * 12]
    ldp   x26, x27, [sp, #16 * 13]
    ldp   x28, x29, [sp, #16 * 14]

    add     sp, sp, #FRAME_SIZE
    eret
    .endm

    .globl IRQ_handler
IRQ_handler:
    .long 0x0

    .align 6
el1_sync:
    trap_entry 1
    mov     x0, sp
    mrs     x1, esr_el1;
    mrs     x2, far_el1;
    bl      do_sync
    trap_exit 1
ENDPROC(el1_sync)

    .align 6
el1_irq:
    trap_entry 1
    ldr     x0, IRQ_handler
    blr     x0
    trap_exit 1
ENDPROC(el1_irq)

    /* Exception vector entry */
    .macro vector_entry label
    .align  7
    b       \label
    .endm

    .align  11
ENTRY(vector_table)
    /* Current Exception level with SP_EL0 */
    vector_entry el1_sync_invalid         /* Synchronous EL1t       */
    vector_entry el1_irq_invalid          /* IRQ EL1t               */
    vector_entry el1_fiq_invalid          /* FIQ EL1t               */
    vector_entry el1_error_invalid        /* Error EL1t             */

    /* Current Executable level with SP_EL1 */
    vector_entry el1_sync                 /* Synchronous EL1h       */
    vector_entry el1_irq                  /* IRQ EL1h               */
    vector_entry el1_fiq_invalid          /* FIQ EL1h               */
    vector_entry el1_error_invalid        /* Error EL1h             */

    /* Lower Executable level using AArch64 */
    vector_entry el0_sync_invalid         /* Synchronous 64-bit EL0 */
    vector_entry el0_irq_invalid          /* IRQ 64-bit EL0         */
    vector_entry el0_fiq_invalid          /* FIQ 64-bit EL0         */
    vector_entry el0_error_invalid        /* Error 64-bit EL0       */

    /* Lower Executable level using AArch32 */
    vector_entry el0_sync_invalid         /* Synchronous 32-bit EL0 */
    vector_entry el0_irq_invalid          /* IRQ 32-bit EL0         */
    vector_entry el0_fiq_invalid          /* FIQ 32-bit EL0         */
    vector_entry el0_error_invalid        /* Error 32-bit EL0       */
END(vector_table)

/* Bad Abort numbers */
#define BAD_SYNC    0
#define BAD_IRQ     1
#define BAD_FIQ     2
#define BAD_ERROR   3

#define el_invalid(name, reason, el)  \
    .align 6;                         \
name##_invalid:                       \
    trap_entry  el;                   \
    mov     x0, sp;                   \
    mov     x1, #(reason);            \
    mrs     x2, esr_el1;              \
    mrs     x3, far_el1;              \
    b       do_bad_mode;              \
ENDPROC(name##_invalid);              \

el_invalid(el1_sync, BAD_SYNC, 1);
el_invalid(el0_sync, BAD_SYNC, 0);
el_invalid(el1_irq, BAD_IRQ, 1);
el_invalid(el0_irq, BAD_IRQ, 0);
el_invalid(el1_fiq, BAD_FIQ, 1);
el_invalid(el0_fiq, BAD_FIQ, 0);
el_invalid(el1_error, BAD_ERROR, 1);
el_invalid(el0_error, BAD_ERROR, 0);

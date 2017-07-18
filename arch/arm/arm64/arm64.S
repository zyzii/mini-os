#include "asm.h"
#include <arch_limits.h>
#include <arm64/os.h>
#include <arm64/pagetable.h>
#include <arm64/traps.h>
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

    .data
    .globl _boot_stack
    .globl boot_l0_pgtable
    .globl boot_l1_pgtable
    .globl boot_l2_pgtable
    .globl idmap_l0_pgtable
    .globl idmap_l1_pgtable
    .globl shared_info

    .align 12
boot_l0_pgtable:
    .fill  PAGE_SIZE,1,0
boot_l1_pgtable:
    .fill  PAGE_SIZE,1,0
boot_l2_pgtable:
    .fill  PAGE_SIZE,1,0
idmap_l0_pgtable:
    .fill  PAGE_SIZE,1,0
idmap_l1_pgtable:
    .fill  PAGE_SIZE,1,0
shared_info:
    .fill  PAGE_SIZE,1,0

    .align 12
_boot_stack:
    .fill  __STACK_SIZE,1,0
stack_end:

/*
 * Kernel startup entry point.
 *
 * Please refer to linux kernel file Documentation/arm64/booting.txt
 * for the header format.
 */
    .text

    b       _start                  /* branch to kernel start, magic */
    .long   0                       /* reserved */
    .quad   0x0                     /* Image load offset from start of RAM */
    .quad   _end - _start           /* Effective Image size */
    .quad   2                       /* kernel flags: LE, 4K page size */
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
    mov     x19, x0

    /* Setup the identity mapping */
    bl      _setup_idmap_pgtable

    /* Load TTBRx */
    msr     ttbr1_el1, x19
    msr     ttbr0_el1, x0
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
    /* Do not use the TTBR0_EL1 any more */
    mrs     x19, tcr_el1
    add     x19, x19, TCR_EPD0
    msr     tcr_el1, x19

    /* Setup stack */
    PRINT("- Setup stack -\n")
    ldr     x1, =stack_end
    mov     sp, x1

    PRINT("- Jumping to C entry -\n")
    mov     x0, x20                  /* x0 <- device tree (physical address) */
    mov     x1, x22                  /* x1 <- phys_offset */

    b      arch_init
ENDPROC(_start)

/*
 * Get the phys-offset, and save it in x22
 */
_calc_offset:
    ldr     x22, =_start             /* x0 := vaddr(_start)  */
    adr     x21, _start              /* x21 := paddr(_start) */
    sub     x22, x21, x22            /* x22 := phys-offset (paddr - vaddr) */
    ret

/*
 * Setup the memory region attribute;
 * Setup the TCR.
 */
_setup_cpu:
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
    ldr     x0, =(TCR_TxSZ(VA_BITS) | TCR_ASID16 | TCR_TG1_4K | \
                  TCR_IRGN_WBWA | TCR_ORGN_WBWA | TCR_SHARED | TCR_IPS_48BIT)
    msr     tcr_el1, x0

    ret

/*
 * Setup the page table mapping for @addr at @level with @prot.
 *
 * Note: x22 stores the offset between virtual address and physical address.
 */
.macro set_page_table, addr, level, prot
    /* Find the table index in @level, save it in x3  */
.if \level == 0
    lsr     x3, \addr, #L0_SHIFT
    adr     x8, boot_l1_pgtable
    adr     x11, boot_l0_pgtable
.endif

.if \level == 1
    lsr     x3, \addr, #L1_SHIFT
    adr     x8, boot_l2_pgtable
    adr     x11, boot_l1_pgtable
.endif

.if \level == 2
    lsr     x3, \addr, #L2_SHIFT
    adr     x11, boot_l2_pgtable
    /* Get the physical address, the @addr should be 2M aligned. */
    add     x8, \addr, x22
.endif

    and     x3, x3, #Ln_ADDR_MASK

    /* Build the page table entry */
    ldr     x7, = \prot
    lsr     x9, x8, #PAGE_SHIFT
    orr     x7, x7, x9, lsl #PAGE_SHIFT

    /* Store entry */
    str     x7, [x11, x3, lsl #3]
.endm

/*
 * Setup the mapping for code section
 *
 * => null
 * <= x0 -> for TTBR1_EL1
 */
_setup_initial_pgtable:
    /* Start to map the code */
    ldr     x0, =_text                 /* x0 := vaddr(_text)            */
    ldr     x1, =_end                  /* x1 := vaddr(_end)             */

    /* Map extra 2M for first_free_pfn */
    add     x1, x1, L2_SIZE

    set_page_table x0, 0, PT_PT
    set_page_table x0, 1, PT_PT
1:
    set_page_table x0, 2, PT_MEM

    add     x0, x0, L2_SIZE
    cmp     x1, x0
    b.gt    1b

    adr     x0, boot_l0_pgtable
    dsb     sy
    ret

/*
 * Setup the page table mapping for @addr at @level with @prot.
 *
 * Only used for identity mapping.
 */
.macro set_ident_page_table, addr, level, prot
    /* Find the table index in @level, save it in x3  */
.if \level == 0
    lsr     x3, \addr, #L0_SHIFT
    adr     x8, idmap_l1_pgtable
    adr     x11, idmap_l0_pgtable
.endif

.if \level == 1
    lsr     x3, \addr, #L1_SHIFT
    mov     x8, \addr
    adr     x11, idmap_l1_pgtable
.endif

    and     x3, x3, #Ln_ADDR_MASK

    /* Build the page table entry */
    ldr     x7, = \prot
    lsr     x9, x8, #PAGE_SHIFT
    orr     x7, x7, x9, lsl #PAGE_SHIFT

    /* Store entry */
    str     x7, [x11, x3, lsl #3]
.endm

/*
 * Setup the page table for TTBR0_EL1:
 *   Mapping the page table for the code section.
 *   We use 48bit address, and just use level 0/1
 *   for the mapping (we do not use level 2 and level 3).
 *
 * => none
 * <= x0 : save the page table pointer for TTBR0_EL1.
 */
_setup_idmap_pgtable:
    /* Create the VA = PA map */
    adr     x0, _text

    set_ident_page_table x0, 0, PT_PT
    set_ident_page_table x0, 1, PT_MEM

    adr     x0, idmap_l0_pgtable
    dsb     sy
    ret

/* The save_registers/restore_registers are based on the code in FreeBSD */
.macro	save_registers el
	mov	x18, sp

	sub	sp, sp, #(PT_REG_SIZE)

	stp	x28, x29, [sp, #(PT_REG_X + 28 * 8)]
	stp	x26, x27, [sp, #(PT_REG_X + 26 * 8)]
	stp	x24, x25, [sp, #(PT_REG_X + 24 * 8)]
	stp	x22, x23, [sp, #(PT_REG_X + 22 * 8)]
	stp	x20, x21, [sp, #(PT_REG_X + 20 * 8)]
	stp	x18, x19, [sp, #(PT_REG_X + 18 * 8)]
	stp	x16, x17, [sp, #(PT_REG_X + 16 * 8)]
	stp	x14, x15, [sp, #(PT_REG_X + 14 * 8)]
	stp	x12, x13, [sp, #(PT_REG_X + 12 * 8)]
	stp	x10, x11, [sp, #(PT_REG_X + 10 * 8)]
	stp	x8,  x9,  [sp, #(PT_REG_X + 8  * 8)]
	stp	x6,  x7,  [sp, #(PT_REG_X + 6  * 8)]
	stp	x4,  x5,  [sp, #(PT_REG_X + 4  * 8)]
	stp	x2,  x3,  [sp, #(PT_REG_X + 2  * 8)]
	stp	x0,  x1,  [sp, #(PT_REG_X + 0  * 8)]

	mrs	x10, elr_el1
	mrs	x11, spsr_el1
	mrs	x12, esr_el1
.if \el == 0
	mrs	x18, sp_el0
.endif
	str	x10, [sp, #(PT_REG_ELR)]
	stp	w11, w12, [sp, #(PT_REG_SPSR)]
	stp	x18, x30, [sp, #(PT_REG_SP)]
.endm

.macro	restore_registers el
	ldp	x18, x30, [sp, #(PT_REG_SP)]
	ldp	x10, x11, [sp, #(PT_REG_ELR)]
.if \el == 0
	msr	sp_el0, x18
.endif
	msr	spsr_el1, x11
	msr	elr_el1, x10

	ldp	x0,  x1,  [sp, #(PT_REG_X + 0  * 8)]
	ldp	x2,  x3,  [sp, #(PT_REG_X + 2  * 8)]
	ldp	x4,  x5,  [sp, #(PT_REG_X + 4  * 8)]
	ldp	x6,  x7,  [sp, #(PT_REG_X + 6  * 8)]
	ldp	x8,  x9,  [sp, #(PT_REG_X + 8  * 8)]
	ldp	x10, x11, [sp, #(PT_REG_X + 10 * 8)]
	ldp	x12, x13, [sp, #(PT_REG_X + 12 * 8)]
	ldp	x14, x15, [sp, #(PT_REG_X + 14 * 8)]
	ldp	x16, x17, [sp, #(PT_REG_X + 16 * 8)]
	ldp	x18, x19, [sp, #(PT_REG_X + 18 * 8)]
	ldp	x20, x21, [sp, #(PT_REG_X + 20 * 8)]
	ldp	x22, x23, [sp, #(PT_REG_X + 22 * 8)]
	ldp	x24, x25, [sp, #(PT_REG_X + 24 * 8)]
	ldp	x26, x27, [sp, #(PT_REG_X + 26 * 8)]
	ldp	x28, x29, [sp, #(PT_REG_X + 28 * 8)]

	mov	sp, x18
        eret
.endm

    .globl IRQ_handler
IRQ_handler:
    .long 0x0

    .align 6
el1_sync:
    save_registers 1
    mov     x0, sp
    mrs     x1, esr_el1;
    mrs     x2, far_el1;
    bl      do_sync
    restore_registers 1

    .align 6
el1_irq:
    save_registers 1
    ldr     x0, IRQ_handler
    blr     x0
    restore_registers 1

/* Bad Abort numbers */
#define BAD_SYNC    0
#define BAD_IRQ     1
#define BAD_FIQ     2
#define BAD_ERROR   3

#define el_invalid(name, reason, el)  \
    .align 6;                         \
name##_invalid:                       \
    save_registers  el;               \
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

    /* Current Exception level with SP_EL1 */
    vector_entry el1_sync                 /* Synchronous EL1h       */
    vector_entry el1_irq                  /* IRQ EL1h               */
    vector_entry el1_fiq_invalid          /* FIQ EL1h               */
    vector_entry el1_error_invalid        /* Error EL1h             */

    /* Lower Exception level using AArch64 */
    vector_entry el0_sync_invalid         /* Synchronous 64-bit EL0 */
    vector_entry el0_irq_invalid          /* IRQ 64-bit EL0         */
    vector_entry el0_fiq_invalid          /* FIQ 64-bit EL0         */
    vector_entry el0_error_invalid        /* Error 64-bit EL0       */

    /* Lower Exception level using AArch32 */
    vector_entry el0_sync_invalid         /* Synchronous 32-bit EL0 */
    vector_entry el0_irq_invalid          /* IRQ 32-bit EL0         */
    vector_entry el0_fiq_invalid          /* FIQ 32-bit EL0         */
    vector_entry el0_error_invalid        /* Error 32-bit EL0       */
END(vector_table)

/*
 * => x0 = &prev->sp
 *    x1 = &next->sp
 * <= switch to the next thread
 */
ENTRY(__arch_switch_threads)
    /* Store the callee-saved registers to prev's struct thread */
    mov   x2, sp

    sub   x2, x2, #(CALLEE_SAVED_REGISTERS * 8)
    stp   x19, x20, [x2, #16 * 0]
    stp   x21, x22, [x2, #16 * 1]
    stp   x23, x24, [x2, #16 * 2]
    stp   x25, x26, [x2, #16 * 3]
    stp   x27, x28, [x2, #16 * 4]
    str   x29, [x2, #16 * 5]

    /* Store current sp/ip to prev's struct thread */
    stp   x2, x30, [x0]

    /* Load the sp/ip from next's struct thread */
    ldp   x2, x30, [x1]

    /* Restore the callee-saved registers */
    ldp   x19, x20, [x2, #16 * 0]
    ldp   x21, x22, [x2, #16 * 1]
    ldp   x23, x24, [x2, #16 * 2]
    ldp   x25, x26, [x2, #16 * 3]
    ldp   x27, x28, [x2, #16 * 4]
    ldr   x29, [x2, #16 * 5]

    add   x2, x2, #(CALLEE_SAVED_REGISTERS * 8)
    mov   sp, x2

    br    x30
ENDPROC(__arch_switch_threads)

/*
 * => sp = thread->sp
 * <= x0 -> user data
 *    x1 -> thread's main function
 *
 *  Get the x0/x1, and set the lr(x30) with exit_thread.
 */
ENTRY(arm_start_thread)
    mov  x2, sp
    ldp  x0, x1, [x2]
    ldr  x30, =exit_thread
    br  x1
ENDPROC(arm_start_thread)

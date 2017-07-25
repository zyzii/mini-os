#ifndef __ARM64_PAGE_TABLE__

#define __ARM64_PAGE_TABLE__

/* TCR flags */
#define TCR_TxSZ(x)         ((((64) - (x)) << 16) | (((64) - (x)) << 0))
#define TCR_IRGN_WBWA       (((1) << 8) | ((1) << 24))
#define TCR_ORGN_WBWA       (((1) << 10) | ((1) << 26))
#define TCR_SHARED          (((3) << 12) | ((3) << 28))
#define TCR_ASID16          ((1) << 36)
#define TCR_IPS_40BIT       ((2) << 32)
#define TCR_TG1_4K          ((2) << 30)
#define TCR_EPD0            (1 << 7)

#define TCR_FLAGS           (TCR_IRGN_WBWA | TCR_ORGN_WBWA | TCR_SHARED | TCR_IPS_40BIT)

/* Number of virtual address bits */
#define VA_BITS             39

/*
 * Memory types available.
 */
#define MEM_DEVICE_nGnRnE    0
#define MEM_DEVICE_nGnRE     1
#define MEM_DEVICE_GRE       2
#define MEM_NORMAL_NC        3
#define MEM_NORMAL           4

#define SET_MAIR(attr, mt)  ((attr) << ((mt) * 8))

/* SCTLR_EL1 - System Control Register */
#define SCTLR_M             0x00000001
#define SCTLR_C             0x00000004
#define SCTLR_I             0x00001000

/* Level 0 table, 512GiB per entry */
#define L0_SHIFT            39
#define L0_INVAL            0x0 /* An invalid address */
#define L0_TABLE            0x3 /* A next-level table */

/* Level 1 table, 1GiB per entry */
#define L1_SHIFT            30
#define L1_SIZE             (1 << L1_SHIFT)
#define L1_OFFSET           (L1_SIZE - 1)
#define L1_INVAL            L0_INVAL
#define L1_BLOCK            0x1
#define L1_TABLE            L0_TABLE
#define L1_MASK             (~(L1_SIZE-1))

/* Level 2 table, 2MiB per entry */
#define L2_SHIFT            21
#define L2_SIZE             (1 << L2_SHIFT)
#define L2_OFFSET           (L2_SIZE - 1)
#define L2_INVAL            L0_INVAL
#define L2_BLOCK            L1_BLOCK
#define L2_TABLE            L0_TABLE
#define L2_MASK             (~(L2_SIZE-1))

/* Level 3 table, 4KiB per entry */
#define L3_SHIFT            12
#define L3_SIZE             (1 << L3_SHIFT)
#define L3_OFFSET           (L3_SIZE - 1)
#define L3_INVAL            0x0
#define L3_PAGE             0x3
#define L3_MASK             (~(L3_SIZE-1))

#define Ln_ENTRIES          (1 << 9)
#define Ln_ADDR_MASK        (Ln_ENTRIES - 1)

#define ATTR_MASK_L         0xfff

#define l1_pgt_idx(va)      (((va) >> L1_SHIFT) & Ln_ADDR_MASK)
#define l2_pgt_idx(va)      (((va) >> L2_SHIFT) & Ln_ADDR_MASK)
#define l3_pgt_idx(va)      (((va) >> L3_SHIFT) & Ln_ADDR_MASK)

/*
 * Lower attributes fields in Stage 1 VMSAv8-A Block and Page descriptor
 */
#define ATTR_nG            (1 << 11)
#define ATTR_AF            (1 << 10)
#define ATTR_SH(x)         ((x) << 8)
#define ATTR_SH_MASK       ATTR_SH(3)
#define ATTR_SH_NS         0               /* Non-shareable */
#define ATTR_SH_OS         2               /* Outer-shareable */
#define ATTR_SH_IS         3               /* Inner-shareable */
#define ATTR_AP_RW_BIT     (1 << 7)
#define ATTR_AP(x)         ((x) << 6)
#define ATTR_AP_MASK       ATTR_AP(3)
#define ATTR_AP_RW         (0 << 1)
#define ATTR_AP_RO         (1 << 1)
#define ATTR_AP_USER       (1 << 0)
#define ATTR_NS            (1 << 5)
#define ATTR_IDX(x)        ((x) << 2)
#define ATTR_IDX_MASK      (7 << 2)

#define BLOCK_DEF_ATTR     (ATTR_AF|ATTR_SH(ATTR_SH_IS)|ATTR_IDX(MEM_NORMAL))
#define BLOCK_NC_ATTR      (ATTR_AF|ATTR_SH(ATTR_SH_IS)|ATTR_IDX(MEM_NORMAL_NC))
#define BLOCK_DEV_ATTR     (ATTR_AF|ATTR_SH(ATTR_SH_IS)|ATTR_IDX(MEM_DEVICE_nGnRnE))

#define PT_PT              (L0_TABLE)
#define PT_MEM             (BLOCK_DEF_ATTR | L1_BLOCK)

#ifndef PAGE_SIZE
#define PAGE_SIZE          L3_SIZE
#endif

#ifndef STACK_SIZE
#define STACK_SIZE        (4 * PAGE_SIZE)
#endif

#endif

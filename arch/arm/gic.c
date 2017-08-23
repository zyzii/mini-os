// ARM GIC implementation

#include <mini-os/os.h>
#include <mini-os/hypervisor.h>
#include <mini-os/console.h>
#include <libfdt.h>

//#define VGIC_DEBUG
#ifdef VGIC_DEBUG
#define DEBUG(_f, _a...) \
    printk("MINI_OS(file=vgic.c, line=%d) " _f , __LINE__, ## _a)
#else
#define DEBUG(_f, _a...)    ((void)0)
#endif

extern void (*IRQ_handler)(void);

struct gic {
    volatile char *gicd_base;
    volatile char *gicc_base;
};

static struct gic gic;

// Distributor Interface
#define GICD_CTLR        0x0
#define GICD_ISENABLER    0x100
#define GICD_IPRIORITYR   0x400
#define GICD_ITARGETSR    0x800
#define GICD_ICFGR        0xC00

// CPU Interface
#define GICC_CTLR    0x0
#define GICC_PMR    0x4
#define GICC_IAR    0xc
#define GICC_EOIR    0x10
#define GICC_HPPIR    0x18

#define gicd(gic, offset) ((gic)->gicd_base + (offset))
#define gicc(gic, offset) ((gic)->gicc_base + (offset))

#define REG(addr) ((uint32_t *)(addr))

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

static void gic_set_priority(struct gic *gic, int irq_number, unsigned char priority)
{
    uint32_t value;
    uint32_t *addr = REG(gicd(gic, GICD_IPRIORITYR)) + (irq_number >> 2);
    value = REG_READ32(addr);
    value &= ~(0xff << (8 * (irq_number & 0x3))); // clear old priority
    value |= priority << (8 * (irq_number & 0x3)); // set new priority
    REG_WRITE32(addr, value);
}

static void gic_route_interrupt(struct gic *gic, int irq_number, unsigned char cpu_set)
{
    uint32_t value;
    uint32_t *addr = REG(gicd(gic, GICD_ITARGETSR)) + (irq_number >> 2);
    value = REG_READ32(addr);
    value &= ~(0xff << (8 * (irq_number & 0x3))); // clear old target
    value |= cpu_set << (8 * (irq_number & 0x3)); // set new target
    REG_WRITE32(addr, value);
}

/* When accessing the GIC registers, we can't use LDREX/STREX because it's not regular memory. */
static __inline__ void clear_bit_non_atomic(int nr, volatile void *base)
{
    volatile uint32_t *tmp = base;
    tmp[nr >> 5] &= (unsigned long)~(1 << (nr & 0x1f));
}

static __inline__ void set_bit_non_atomic(int nr, volatile void *base)
{
    volatile uint32_t *tmp = base;
    tmp[nr >> 5] |= (1 << (nr & 0x1f));
}

/* Note: not thread safe (but we only support one CPU for now anyway) */
static void gic_enable_interrupt(struct gic *gic, int irq_number,
        unsigned char cpu_set, unsigned char level_sensitive)
{
    int *set_enable_reg;
    void *cfg_reg;

    // set priority
    gic_set_priority(gic, irq_number, 0x0);

    // set target cpus for this interrupt
    gic_route_interrupt(gic, irq_number, cpu_set);

    // set level/edge triggered
    cfg_reg = (void *)gicd(gic, GICD_ICFGR);
    if (level_sensitive) {
        clear_bit_non_atomic((irq_number * 2) + 1, cfg_reg);
    } else {
        set_bit_non_atomic((irq_number * 2) + 1, cfg_reg);
    }

    wmb();

    // enable forwarding interrupt from distributor to cpu interface
    set_enable_reg = (int *)gicd(gic, GICD_ISENABLER);
    set_enable_reg[irq_number >> 5] = 1 << (irq_number & 0x1f);
    wmb();
}

static void gic_enable_interrupts(struct gic *gic)
{
    // Global enable forwarding interrupts from distributor to cpu interface
    REG_WRITE32(REG(gicd(gic, GICD_CTLR)), 0x00000001);

    // Global enable signalling of interrupt from the cpu interface
    REG_WRITE32(REG(gicc(gic, GICC_CTLR)), 0x00000001);
}

static void gic_disable_interrupts(struct gic *gic)
{
    // Global disable signalling of interrupt from the cpu interface
    REG_WRITE32(REG(gicc(gic, GICC_CTLR)), 0x00000000);

    // Global disable forwarding interrupts from distributor to cpu interface
    REG_WRITE32(REG(gicd(gic, GICD_CTLR)), 0x00000000);
}

static void gic_cpu_set_priority(struct gic *gic, char priority)
{
    REG_WRITE32(REG(gicc(gic, GICC_PMR)), priority & 0x000000FF);
}

static unsigned long gic_readiar(struct gic *gic) {
    return REG_READ32(REG(gicc(gic, GICC_IAR))) & 0x000003FF; // Interrupt ID
}

static void gic_eoir(struct gic *gic, uint32_t irq) {
    REG_WRITE32(REG(gicc(gic, GICC_EOIR)), irq & 0x000003FF);
}

//FIXME Get event_irq from dt
#define EVENTS_IRQ 31
#define VIRTUALTIMER_IRQ 27

static void gic_handler(void) {
    unsigned int irq = gic_readiar(&gic);

    DEBUG("IRQ received : %i\n", irq);
    switch(irq) {
    case EVENTS_IRQ:
        do_hypervisor_callback(NULL);
        break;
    case VIRTUALTIMER_IRQ:
        /* We need to get this event to wake us up from block_domain,
         * but we don't need to do anything special with it. */
        break;
    case 1022:
    case 1023:
        return;  /* Spurious interrupt */
    default:
        DEBUG("Unhandled irq\n");
        break;
    }

    DEBUG("EIRQ\n");

    gic_eoir(&gic, irq);
}

static unsigned long parse_out_value(const uint32_t *reg, uint32_t size)
{
    if (size == 1)
        return (unsigned long)fdt32_to_cpu(reg[0]);

    if (size == 2) {
	uint64_t *reg64 = (uint64_t *)reg;

	return (unsigned long)fdt64_to_cpu(reg64[0]);
    }
    BUG();
}

/*
 * Parse out the address/size for gicd/gicc.
 *
 * Return 0 on success; return 1 on error.
 */
static int gic_parse(int node, unsigned long *gicd_addr, unsigned long *gicd_size,
           unsigned long *gicc_addr, unsigned long *gicc_size)
{
    uint32_t addr_size = 2, cell_size = 1; /* The default, refer to Spec. */
    const uint32_t *reg32;
    int pnode;

    pnode = fdt_parent_offset(device_tree, node);
    if (pnode < 0)
         return 1;

    reg32 = (uint32_t *)fdt_getprop(device_tree, pnode, "#address-cells", NULL);
    if (reg32)
         addr_size = fdt32_to_cpu(reg32[0]);

    reg32 = (uint32_t *)fdt_getprop(device_tree, pnode, "#size-cells", NULL);
    if (reg32)
         cell_size = fdt32_to_cpu(reg32[0]);

    if (addr_size > 2 || cell_size > 2) {
         printk("Unsupported #address-cells: %d, #size-cells: %d\n",
                addr_size, cell_size);
	 return 1;
    }

    reg32 = fdt_getprop(device_tree, node, "reg", NULL);
    if (reg32) {
         *gicd_addr = parse_out_value(reg32, addr_size);
         *gicd_size = parse_out_value(reg32 + addr_size, cell_size);
         *gicc_addr = parse_out_value(reg32 + addr_size + cell_size, addr_size);
         *gicc_size = parse_out_value(reg32 + addr_size + cell_size + addr_size,
                                      cell_size);
    }

    return 0;
}

void gic_init(void) {
    gic.gicd_base = NULL;
    int node = 0;
    int depth = 0;
    for (;;)
    {
        node = fdt_next_node(device_tree, node, &depth);
        if (node <= 0 || depth < 0)
            break;

        if (fdt_getprop(device_tree, node, "interrupt-controller", NULL)) {
            unsigned long gicd_addr, gicd_size, gicc_addr, gicc_size;

            if (fdt_node_check_compatible(device_tree, node, "arm,cortex-a15-gic") &&
                fdt_node_check_compatible(device_tree, node, "arm,cortex-a7-gic")) {
                printk("Skipping incompatible interrupt-controller node\n");
                continue;
            }

            if (gic_parse(node, &gicd_addr, &gicd_size, &gicc_addr, &gicc_size))
		    continue;

            gic.gicd_base = ioremap(gicd_addr, gicd_size);
            gic.gicc_base = ioremap(gicc_addr, gicc_size);
            printk("Found GIC: gicd_base = %p, gicc_base = %p\n", gic.gicd_base, gic.gicc_base);
            break;
        }
    }
    if (!gic.gicd_base) {
        printk("GIC not found!\n");
        BUG();
    }
    wmb();

    /* Note: we could mark this as "device" memory here, but Xen will have already
     * set it that way in the second stage translation table, so it's not necessary.
     * See "Overlaying the memory type attribute" in the Architecture Reference Manual.
     */

    IRQ_handler = gic_handler;

    gic_disable_interrupts(&gic);
    gic_cpu_set_priority(&gic, 0xff);

    /* Must call gic_enable_interrupts before enabling individual interrupts, otherwise our IRQ handler
     * gets called endlessly with spurious interrupts. */
    gic_enable_interrupts(&gic);

    gic_enable_interrupt(&gic, EVENTS_IRQ /* interrupt number */, 0x1 /*cpu_set*/, 1 /*level_sensitive*/);
    gic_enable_interrupt(&gic, VIRTUALTIMER_IRQ /* interrupt number */, 0x1 /*cpu_set*/, 1 /*level_sensitive*/);
}

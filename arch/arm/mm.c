#include <mini-os/console.h>
#include <xen/memory.h>
#include <arch_mm.h>
#include <mini-os/errno.h>
#include <mini-os/hypervisor.h>
#include <mini-os/posix/limits.h>
#include <libfdt.h>
#include <lib.h>

paddr_t physical_address_offset;

unsigned mem_blocks = 1;

int arch_check_mem_block(int index, unsigned long *r_min, unsigned long *r_max)
{
    *r_min = 0;
    *r_max = ULONG_MAX - 1;
    return 0;
}

unsigned long allocate_ondemand(unsigned long n, unsigned long alignment)
{
    // FIXME
    BUG();
}

#if defined(__aarch64__)

#include <arm64/pagetable.h>

extern lpae_t boot_l1_pgtable[512];

static inline void set_pgt_entry(lpae_t *ptr, lpae_t val)
{
    *ptr = val;
    dsb(ishst);
    isb();
}

static void build_pte(lpae_t *pud, unsigned long vaddr, unsigned long vend,
                      paddr_t phys, long mem_type)
{
    lpae_t *pte;

    pte = (lpae_t *)to_virt((*pud) & ~ATTR_MASK_L) + l3_pgt_idx(vaddr);
    do {
        set_pgt_entry(pte, (phys & L3_MASK) | mem_type | L3_PAGE);

        vaddr += L3_SIZE;
        phys += L3_SIZE;
        pte++;
    } while (vaddr < vend);
}

static void build_pud(lpae_t *pgd, unsigned long vaddr, unsigned long vend,
                      paddr_t phys, long mem_type,
                      paddr_t (*new_page)(void), int level)
{
    lpae_t *pud;
    unsigned long next;

    pud = (lpae_t *)to_virt((*pgd) & ~ATTR_MASK_L) + l2_pgt_idx(vaddr);
    do {
        if (level == 2) {
             set_pgt_entry(pud, (phys & L2_MASK) | mem_type | L2_BLOCK);
	} else if (level == 3) {
             next = vaddr + L2_SIZE;
             if (next > vend)
                 next = vend;

             if ((*pud) == L2_INVAL)
                 set_pgt_entry(pud, (new_page()) | PT_PT);

             build_pte(pud, vaddr, next, phys, mem_type);
        }

        vaddr += L2_SIZE;
        phys += L2_SIZE;
        pud++;
    } while (vaddr < vend);
}

/* Setup the page table when the MMU is enabled */
void build_pagetable(unsigned long vaddr, unsigned long start_pfn,
                     unsigned long max_pfn, long mem_type,
                     paddr_t (*new_page)(void), int level)
{
    paddr_t p_start;
    unsigned long v_end, next;
    lpae_t *pgd;

    v_end = vaddr + max_pfn * PAGE_SIZE;
    p_start = PFN_PHYS(start_pfn);

    /* The boot_l1_pgtable can span 512G address space */
    pgd = &boot_l1_pgtable[l1_pgt_idx(vaddr)];

    do {
        next = (vaddr + L1_SIZE);
        if (next > v_end)
            next = v_end;

        if ((*pgd) == L1_INVAL) {
            set_pgt_entry(pgd, (new_page()) | PT_PT);
        }

        build_pud(pgd, vaddr, next, p_start, mem_type, new_page, level);
        p_start += next - vaddr;
        vaddr = next;
        pgd++;
    } while (vaddr != v_end);
}

static unsigned long first_free_pfn;
static paddr_t get_new_page(void)
{
    paddr_t new_page;

    memset(pfn_to_virt(first_free_pfn), 0, PAGE_SIZE);
    dsb(ishst);

    new_page = PFN_PHYS(first_free_pfn);
    first_free_pfn++;
    return new_page;
}

/*
 * This function will setup the page table for the memory system.
 *
 * Note: We get the page for page table from the first free PFN,
 *       the get_new_page() will increase the @first_free_pfn.
 */
void init_pagetable(unsigned long *start_pfn, unsigned long base_pfn,
                    unsigned long max_pfn)
{
    first_free_pfn = *start_pfn;

    build_pagetable((unsigned long)pfn_to_virt(base_pfn), base_pfn,
		    max_pfn, BLOCK_DEF_ATTR, get_new_page, 2);
    *start_pfn = first_free_pfn;
}


static paddr_t alloc_new_page(void)
{
    unsigned long page;

    page = alloc_page();
    if (!page)
        BUG();
    memset((void *)page, 0, PAGE_SIZE);
    dsb(ishst);
    return to_phys(page);
}

unsigned long map_frame_virt(unsigned long mfn)
{
    unsigned long vaddr = (unsigned long)mfn_to_virt(mfn);

    build_pagetable(vaddr, mfn, 1, BLOCK_DEF_ATTR, alloc_new_page, 3);

    return vaddr;
}

void *ioremap(paddr_t addr, unsigned long size)
{
    build_pagetable((unsigned long)to_virt(addr), PHYS_PFN(addr), PFN_UP(size),
		    BLOCK_DEV_ATTR, alloc_new_page, 3);
    return to_virt(addr);
}

#else
void init_pagetable(unsigned long *start_pfn, unsigned long base_pfn,
                    unsigned long max_pfn)
{
}

unsigned long map_frame_virt(unsigned long mfn)
{
    return mfn_to_virt(mfn);
}

void *ioremap(paddr_t addr, unsigned long size)
{
    return to_virt(addr);
}
#endif

void arch_init_mm(unsigned long *start_pfn_p, unsigned long *max_pfn_p)
{
    int memory;
    int prop_len = 0;
    const uint64_t *regs;
    uintptr_t end;
    paddr_t mem_base;
    uint64_t mem_size;
    uint64_t heap_len;
    uint32_t fdt_size;
    void *new_device_tree;

    printk("    _text:       %p(VA)\n", &_text);
    printk("    _etext:      %p(VA)\n", &_etext);
    printk("    _erodata:    %p(VA)\n", &_erodata);
    printk("    _edata:      %p(VA)\n", &_edata);
    printk("    stack start: %p(VA)\n", _boot_stack);
    printk("    _end:        %p(VA)\n", &_end);

    if (fdt_num_mem_rsv(device_tree) != 0)
        printk("WARNING: reserved memory not supported!\n");

    memory = fdt_node_offset_by_prop_value(device_tree, -1, "device_type", "memory", sizeof("memory"));
    if (memory < 0) {
        printk("No memory found in FDT!\n");
        BUG();
    }

    /* Xen will always provide us at least one bank of memory.
     * Mini-OS will use the first bank for the time-being. */
    regs = fdt_getprop(device_tree, memory, "reg", &prop_len);

    /* The property must contain at least the start address
     * and size, each of which is 8-bytes. */
    if (regs == NULL || prop_len < 16) {
        printk("Bad 'reg' property: %p %d\n", regs, prop_len);
        BUG();
    }

    end = (uintptr_t) &_end;
    mem_base = fdt64_to_cpu(regs[0]);
    mem_size = fdt64_to_cpu(regs[1]);
    printk("Found memory at 0x%llx (len 0x%llx)\n",
            (unsigned long long) mem_base, (unsigned long long) mem_size);

    BUG_ON(to_virt(mem_base) > (void *) &_text);          /* Our image isn't in our RAM! */
    *start_pfn_p = PFN_UP(to_phys(end));
    heap_len = mem_size - (PFN_PHYS(*start_pfn_p) - mem_base);
    *max_pfn_p = *start_pfn_p + PFN_DOWN(heap_len);

    init_pagetable(start_pfn_p, PHYS_PFN(mem_base), PHYS_PFN(mem_size));

    printk("Using pages %lu to %lu as free space for heap.\n", *start_pfn_p, *max_pfn_p);

    /* The device tree is probably in memory that we're about to hand over to the page
     * allocator, so move it to the end and reserve that space.
     */
    fdt_size = fdt_totalsize(device_tree);
    new_device_tree = to_virt(((*max_pfn_p << PAGE_SHIFT) - fdt_size) & PAGE_MASK);
    if (new_device_tree != device_tree) {
        memmove(new_device_tree, device_tree, fdt_size);
    }
    device_tree = new_device_tree;
    *max_pfn_p = to_phys(new_device_tree) >> PAGE_SHIFT;
}

void arch_init_demand_mapping_area(void)
{
}

int do_map_frames(unsigned long addr,
        const unsigned long *f, unsigned long n, unsigned long stride,
        unsigned long increment, domid_t id, int *err, unsigned long prot)
{
    return -ENOSYS;
}

/* Get Xen's suggested physical page assignments for the grant table. */
static paddr_t get_gnttab_base(void)
{
    int hypervisor;
    int len = 0;
    const uint64_t *regs;
    paddr_t gnttab_base;

    hypervisor = fdt_node_offset_by_compatible(device_tree, -1, "xen,xen");
    BUG_ON(hypervisor < 0);

    regs = fdt_getprop(device_tree, hypervisor, "reg", &len);
    /* The property contains the address and size, 8-bytes each. */
    if (regs == NULL || len < 16) {
        printk("Bad 'reg' property: %p %d\n", regs, len);
        BUG();
    }

    gnttab_base = fdt64_to_cpu(regs[0]);

    printk("FDT suggests grant table base %llx\n", (unsigned long long) gnttab_base);

    return gnttab_base;
}

grant_entry_v1_t *arch_init_gnttab(int nr_grant_frames)
{
    struct xen_add_to_physmap xatp;
    struct gnttab_setup_table setup;
    xen_pfn_t frames[nr_grant_frames];
    paddr_t gnttab_table;
    int i, rc;

    gnttab_table = get_gnttab_base();

    for (i = 0; i < nr_grant_frames; i++)
    {
        xatp.domid = DOMID_SELF;
        xatp.size = 0;      /* Seems to be unused */
        xatp.space = XENMAPSPACE_grant_table;
        xatp.idx = i;
        xatp.gpfn = (gnttab_table >> PAGE_SHIFT) + i;
        rc = HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp);
        BUG_ON(rc != 0);
    }

    setup.dom = DOMID_SELF;
    setup.nr_frames = nr_grant_frames;
    set_xen_guest_handle(setup.frame_list, frames);
    HYPERVISOR_grant_table_op(GNTTABOP_setup_table, &setup, 1);
    if (setup.status != 0)
    {
        printk("GNTTABOP_setup_table failed; status = %d\n", setup.status);
        BUG();
    }

    return to_virt(gnttab_table);
}


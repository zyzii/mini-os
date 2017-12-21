#include <mini-os/console.h>
#include <xen/memory.h>
#include <arch_mm.h>
#include <mini-os/errno.h>
#include <mini-os/hypervisor.h>
#include <mini-os/posix/limits.h>
#include <libfdt.h>
#include <lib.h>
#include <arm64/pagetable.h>

paddr_t physical_address_offset;
unsigned mem_blocks = 1;

int arch_check_mem_block(int index, unsigned long *r_min, unsigned long *r_max)
{
    *r_min = 0;
    *r_max = ULONG_MAX - 1;
    return 0;
}

extern lpae_t boot_l0_pgtable[512];

static inline void set_pgt_entry(lpae_t *ptr, lpae_t val)
{
    *ptr = val;
    dsb(ishst);
    isb();
}

static void build_pte(lpae_t *pmd, unsigned long vaddr, unsigned long vend,
                      paddr_t phys, uint64_t mem_type)
{
    lpae_t *pte;

    pte = (lpae_t *)to_virt((*pmd) & ~ATTR_MASK_L) + l3_pgt_idx(vaddr);
    do {
        set_pgt_entry(pte, (phys & L3_MASK) | mem_type | L3_PAGE);

        vaddr += L3_SIZE;
        phys += L3_SIZE;
        pte++;
    } while (vaddr < vend);
}

static int build_pmd(lpae_t *pud, unsigned long vaddr, unsigned long vend,
                      paddr_t phys, uint64_t mem_type,
                      paddr_t (*new_page)(void), int level)
{
    lpae_t *pmd;
    unsigned long next;

    pmd = (lpae_t *)to_virt((*pud) & ~ATTR_MASK_L) + l2_pgt_idx(vaddr);
    do {
        if (level == 2) {
             set_pgt_entry(pmd, (phys & L2_MASK) | mem_type | L2_BLOCK);
        } else {
             next = vaddr + L2_SIZE;
             if (next > vend)
                 next = vend;

             if ((*pmd) == L2_INVAL) {
                 paddr_t newpage = new_page();
                 if (!newpage)
                         return -ENOMEM;
                 set_pgt_entry(pmd, newpage | PT_PT);
             }

             build_pte(pmd, vaddr, next, phys, mem_type);
        }

        vaddr += L2_SIZE;
        phys += L2_SIZE;
        pmd++;
    } while (vaddr < vend);

    return 0;
}

static int build_pud(lpae_t *pgd, unsigned long vaddr, unsigned long vend,
                      paddr_t phys, uint64_t mem_type,
                      paddr_t (*new_page)(void), int level)
{
    lpae_t *pud;
    unsigned long next;
    int ret;

    pud = (lpae_t *)to_virt((*pgd) & ~ATTR_MASK_L) + l1_pgt_idx(vaddr);
    do {
        if (level == 1) {
             set_pgt_entry(pud, (phys & L1_MASK) | mem_type | L1_BLOCK);
        } else {
             next = vaddr + L1_SIZE;
             if (next > vend)
                 next = vend;

             if ((*pud) == L1_INVAL) {
                 paddr_t newpage = new_page();
                 if (!newpage)
                     return -ENOMEM;
                 set_pgt_entry(pud, newpage | PT_PT);
             }

             ret = build_pmd(pud, vaddr, next, phys, mem_type, new_page, level);
             if (ret)
                 return ret;
        }

        vaddr += L1_SIZE;
        phys += L1_SIZE;
        pud++;
    } while (vaddr < vend);

    return 0;
}

static int build_pagetable(unsigned long vaddr, unsigned long start_pfn,
                     unsigned long max_pfn, uint64_t mem_type,
                     paddr_t (*new_page)(void), int level)
{
    paddr_t p_start;
    unsigned long v_end, next;
    lpae_t *pgd;
    int ret;

    v_end = vaddr + max_pfn * PAGE_SIZE;
    p_start = PFN_PHYS(start_pfn);

    pgd = &boot_l0_pgtable[l0_pgt_idx(vaddr)];

    do {
        next = (vaddr + L0_SIZE);
        if (next > v_end)
            next = v_end;

        if ((*pgd) == L0_INVAL) {
            paddr_t newpage = new_page();
            if (!newpage)
                return -ENOMEM;
            set_pgt_entry(pgd, newpage | PT_PT);
        }

        ret = build_pud(pgd, vaddr, next, p_start, mem_type, new_page, level);
        if (ret)
            return ret;

        p_start += next - vaddr;
        vaddr = next;
        pgd++;
    } while (vaddr != v_end);

    return 0;
}

/*
 * Before the page allocator is ready, we use first_free_pfn to record
 * the first free page. The first_free_pfn will be increased by
 * early_alloc_page().
 */
static unsigned long first_free_pfn;

/* The pfn for MIN_MEM_SIZE */
static unsigned long min_mem_pfn;

static paddr_t early_alloc_page(void)
{
    paddr_t new_page;

    memset(pfn_to_virt(first_free_pfn), 0, PAGE_SIZE);
    dsb(ishst);

    new_page = PFN_PHYS(first_free_pfn);
    first_free_pfn++;
    ASSERT(first_free_pfn < min_mem_pfn);
    return new_page;
}

static int init_pagetable_ok;
/*
 * This function will setup the page table for the memory system.
 */
void init_pagetable(unsigned long *start_pfn, unsigned long base,
                    unsigned long size)
{
    unsigned long vaddr = (unsigned long)to_virt(base);
    paddr_t phys = base;
    paddr_t sz = L1_SIZE;
    lpae_t *pgd;
    lpae_t *pud;
    int level;

    do {
        /*
         * We cannot set block mapping for PGD(level 0),
         * but we can set block mapping for PUD(level 1) and PMD(level 2).
         * Get the proper level for build_pagetable().
         */
        if (size >= L1_SIZE) {
            pgd = &boot_l0_pgtable[l0_pgt_idx(vaddr)];
            if ((*pgd) == L0_INVAL) {
                 level = 1;
            } else {
                 pud = (lpae_t *)to_virt((*pgd) & ~ATTR_MASK_L) + l1_pgt_idx(vaddr);
                 if ((*pud) == L1_INVAL)
                     level = 1;
                 else
                     level = 2;
            }
        } else {
             sz = size & L2_MASK;
             level = 2;
        }

        build_pagetable(vaddr, PHYS_PFN(phys), PFN_UP(sz),
                        MEM_DEF_ATTR, early_alloc_page, level);

        vaddr += sz;
        phys  += sz;
        size -= sz;
    } while (size > L2_SIZE);

    /* Use the page mapping (level 3) for the left */
    if (size)
        build_pagetable(vaddr, PHYS_PFN(phys), PFN_UP(size),
                        MEM_DEF_ATTR, early_alloc_page, 3);

    *start_pfn = first_free_pfn;
    init_pagetable_ok = 1;
}

static unsigned long virt_kernel_area_end;
static unsigned long virt_demand_area_end;
void arch_mm_preinit(void *dtb_pointer)
{
    paddr_t **dtb_p = dtb_pointer;
    paddr_t *dtb = *dtb_p;
    uintptr_t end = (uintptr_t) &_end;

    virt_kernel_area_end = VIRT_KERNEL_AREA;
    virt_demand_area_end = VIRT_DEMAND_AREA;

    dtb = to_virt(((paddr_t)dtb));
    first_free_pfn = PFN_UP(to_phys(end));
    min_mem_pfn = PFN_UP(to_phys(_text) + MIN_MEM_SIZE);

    /*
     * Setup the mapping for Device Tree, only map 2M(L2_SIZE) size.
     *
     * Note: The early_alloc_page() will increase @first_free_pfn.
     */
    build_pagetable((unsigned long)dtb, virt_to_pfn((unsigned long)dtb),
                    PHYS_PFN(L2_SIZE), MEM_DEF_ATTR, early_alloc_page, 2);

    *dtb_p = dtb;
}

static unsigned long alloc_virt_kernel(unsigned n_pages)
{
    unsigned long addr;

    addr = virt_kernel_area_end;
    virt_kernel_area_end += PAGE_SIZE * n_pages;
    ASSERT(virt_kernel_area_end <= VIRT_DEMAND_AREA);

    return addr;
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
    unsigned long addr;
    int ret;

    addr = alloc_virt_kernel(1);
    ret = build_pagetable(addr, mfn, 1, MEM_DEF_ATTR,
                    init_pagetable_ok? alloc_new_page: early_alloc_page, 3);
    ASSERT(ret == 0);

    return addr;
}

unsigned long allocate_ondemand(unsigned long n, unsigned long alignment)
{
    unsigned long addr;

    addr = virt_demand_area_end;

    /* Just for simple, make it page aligned. */
    virt_demand_area_end += (n + PAGE_SIZE - 1) & PAGE_MASK;

    ASSERT(virt_demand_area_end <= VIRT_HEAP_AREA);

    return addr;
}

void *ioremap(paddr_t paddr, unsigned long size)
{
    unsigned long addr;
    int ret;

    addr = allocate_ondemand(size, 1);
    if (!addr)
        return NULL;

    ret = build_pagetable(addr, PHYS_PFN(paddr), PFN_UP(size), MEM_DEV_ATTR,
                  init_pagetable_ok? alloc_new_page: early_alloc_page, 3);
    if (ret < 0)
        return NULL;
    return (void*)addr;
}

void *map_frames_ex(const unsigned long *f, unsigned long n, unsigned long stride,
                    unsigned long increment, unsigned long alignment, domid_t id,
                    int *err, unsigned long prot_origin)
{
    unsigned long addr, va;
    unsigned long done = 0;
    unsigned long mfn;
    unsigned long prot = MEM_DEF_ATTR;
    int ret;

    if (!f)
        return NULL;

    addr = allocate_ondemand(n, alignment);
    if (!addr)
        return NULL;

    va = addr;
    if (prot_origin == MEM_RO_ATTR)
        prot = prot_origin;

    while (done < n) {
        mfn = f[done * stride] + done * increment;
        ret = build_pagetable(va, mfn, 1, prot, alloc_new_page, 3);
        if (ret)
            return NULL;
        done++;
        va += PAGE_SIZE;
    }

    return (void *)addr;
}

void *map_frames(unsigned long *frames, unsigned long pfn_num)
{
    return map_frames_ex(frames, pfn_num, 1, 0, 1, DOMID_SELF, NULL, MEM_DEF_ATTR);
}

static lpae_t *get_ptep(unsigned long vaddr)
{
    lpae_t *pgd, *pud, *pmd, *pte;

    pgd = &boot_l0_pgtable[l0_pgt_idx(vaddr)];
    ASSERT((*pgd) != L0_INVAL);

    pud = (lpae_t *)to_virt((*pgd) & ~ATTR_MASK_L) + l1_pgt_idx(vaddr);
    ASSERT((*pud) != L0_INVAL);

    pmd = (lpae_t *)to_virt((*pud) & ~ATTR_MASK_L) + l2_pgt_idx(vaddr);
    ASSERT((*pmd) != L0_INVAL);

    pte = (lpae_t *)to_virt((*pmd) & ~ATTR_MASK_L) + l3_pgt_idx(vaddr);
    ASSERT((*pte) != L0_INVAL);

    return pte;
}

int unmap_frames(unsigned long va, unsigned long num_frames)
{
    lpae_t *pte;

    ASSERT(!((unsigned long)va & ~PAGE_MASK));

    while (num_frames) {
        pte = get_ptep(va);
	*pte = (lpae_t)0;

        flush_tlb_page(va);

        va += PAGE_SIZE;
        num_frames--;
    }
    return 0;
}

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

    BUG_ON(mem_size < MIN_MEM_SIZE);
    if (mem_size > MAX_MEM_SIZE)
        mem_size = MAX_MEM_SIZE;

    printk("Found memory at 0x%llx (len 0x%llx)\n",
            (unsigned long long) mem_base, (unsigned long long) mem_size);

    BUG_ON(to_virt(mem_base) > (void *) &_text);          /* Our image isn't in our RAM! */
    *start_pfn_p = PFN_UP(to_phys(end));
    heap_len = mem_size - (PFN_PHYS(*start_pfn_p) - mem_base);
    *max_pfn_p = *start_pfn_p + PFN_DOWN(heap_len);

    init_pagetable(start_pfn_p, mem_base, mem_size);

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

    return map_frames(frames, nr_grant_frames);
}

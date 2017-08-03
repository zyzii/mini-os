#ifndef _ARCH_MM_H_
#define _ARCH_MM_H_

typedef uint64_t paddr_t;

extern char _text, _etext, _erodata, _edata, _end, __bss_start;
extern int _boot_stack[];
extern int _boot_stack_end[];

/* Add this to a virtual address to get the physical address */
extern paddr_t physical_address_offset;

#define PAGE_SHIFT        12
#define PAGE_SIZE        (1 << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE-1))

#define DEF_PAGE_PROT     0

#if defined(__aarch64__)
#define ADDR_MASK         0xffffffffffffffff
#else
#define ADDR_MASK         0xffffffff
#endif

#define to_phys(x)                 (((paddr_t)(x)-physical_address_offset) & ADDR_MASK)
#define to_virt(x)                 ((void *)(((x)+physical_address_offset) & ADDR_MASK))

#define PFN_UP(x)                  (unsigned long)(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)                (unsigned long)((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)                ((uint64_t)(x) << PAGE_SHIFT)
#define PHYS_PFN(x)                (unsigned long)((x) >> PAGE_SHIFT)

#define virt_to_pfn(_virt)         (PFN_DOWN(to_phys(_virt)))
#define virt_to_mfn(_virt)         (PFN_DOWN(to_phys(_virt)))
#define mfn_to_virt(_mfn)          (to_virt(PFN_PHYS(_mfn)))
#define pfn_to_virt(_pfn)          (to_virt(PFN_PHYS(_pfn)))

#define virtual_to_mfn(_virt)	   virt_to_mfn(_virt)

// FIXME
#define map_frames(f, n) (NULL)

#endif

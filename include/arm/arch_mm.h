#ifndef _ARCH_MM_H_
#define _ARCH_MM_H_

#if defined(__arm__)
typedef uint32_t paddr_t;
#define PRIpaddr "x"
#define MIN_MEM_SIZE            (0x400000)
#define MAX_MEM_SIZE            (~0UL)
#else
typedef uint64_t paddr_t;
#define PRIpaddr "lx"
#define MIN_MEM_SIZE            (0x400000)
#define MAX_MEM_SIZE            (1UL << 39)
#define VIRT_KERNEL_AREA        MAX_MEM_SIZE
#define VIRT_DEMAND_AREA        (MAX_MEM_SIZE << 5)
#define VIRT_HEAP_AREA          (MAX_MEM_SIZE << 6)
#endif

typedef uint64_t lpae_t;

extern char _text, _etext, _erodata, _edata, _end, __bss_start;
extern int _boot_stack[];
extern int _boot_stack_end[];

/* Add this to a virtual address to get the physical address */
extern paddr_t physical_address_offset;

#include <page_def.h>

#define DEF_PAGE_PROT     0

#define to_phys(x)                 (((paddr_t)(x)+physical_address_offset) & (~0UL))
#define to_virt(x)                 ((void *)(((x)-physical_address_offset) & (~0UL)))

#define PFN_UP(x)                  (unsigned long)(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)                (unsigned long)((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)                ((uint64_t)(x) << PAGE_SHIFT)
#define PHYS_PFN(x)                (unsigned long)((x) >> PAGE_SHIFT)

#define virt_to_pfn(_virt)         (PFN_DOWN(to_phys(_virt)))
#define virt_to_mfn(_virt)         (PFN_DOWN(to_phys(_virt)))
#define mfn_to_virt(_mfn)          (to_virt(PFN_PHYS(_mfn)))
#define pfn_to_virt(_pfn)          (to_virt(PFN_PHYS(_pfn)))

#define virtual_to_mfn(_virt)	   virt_to_mfn(_virt)

void arch_mm_preinit(void *dtb_pointer);
// FIXME
#define map_frames(f, n) (NULL)

void *ioremap(paddr_t addr, unsigned long size);
#endif

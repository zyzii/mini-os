#ifndef __ARCH_LIMITS_H__
#define __ARCH_LIMITS_H__

#include <page_def.h>

#define __PAGE_SIZE       (1UL << PAGE_SHIFT)

#define __STACK_SIZE_PAGE_ORDER  2
#define __STACK_SIZE (4 * PAGE_SIZE)

#endif

#ifndef __ASM_H__
#define __ASM_H__

#define ALIGN   .align 4

#define ENTRY(name) \
    .globl name; \
    ALIGN; \
    name:

#define END(name) \
    .size name, .-name

#define ENDPROC(name) \
    .type name, @function; \
    END(name)

#endif /* __ASM_H__ */

#ifndef __MEMMAP_H
#define __MEMMAP_H

#include "types.h" // u32

// A typical OS page size
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

static inline u32 virt_to_phys(void *v) {
    return (u32)v;
}

#endif // memmap.h

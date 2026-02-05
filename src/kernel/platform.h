#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

void platform_init(void);
uint64_t p2v(uint64_t phys);
uint64_t v2p(uint64_t virt);

#endif

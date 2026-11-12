#pragma once
#include <stdint.h>
#define __scratch_y(x)
#define __dmb() __asm__ volatile ("" ::: "memory")
static inline void tight_loop_contents(void) {}

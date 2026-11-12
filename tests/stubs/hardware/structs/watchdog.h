#pragma once
#include <stdint.h>
typedef struct { uint32_t scratch[8]; } test_watchdog_t;
extern test_watchdog_t *watchdog_hw;

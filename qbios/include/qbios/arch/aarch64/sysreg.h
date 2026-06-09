#pragma once
#include "qbios/types.h"

// AArch64 specific macros for reading and writing system registers (e.g., TTBR0_EL1, SCTLR_EL1)
#define QBIOS_READ_SYSREG(reg) ({ \
    uint64_t _val; \
    __asm__ volatile("mrs %0, " #reg : "=r"(_val)); \
    _val; \
})

#define QBIOS_WRITE_SYSREG(reg, val) \
    __asm__ volatile("msr " #reg ", %0" :: "r"((uint64_t)(val)))

// Exception level checks
static inline uint8_t get_current_el(void) {
    uint64_t current_el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    return (uint8_t)((current_el >> 2) & 0x3);
}


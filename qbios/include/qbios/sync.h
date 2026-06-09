#pragma once
#include "qbios/types.h"

// Portable CPU relax for spinlocks to reduce power consumption and memory contention
static inline void cpu_relax() {
#ifdef __aarch64__
    __asm__ volatile("yield" ::: "memory");
#else
    __asm__ volatile("pause" ::: "memory");
#endif
}

// Simple atomic spinlock suitable for kernel-level synchronization
static inline void spin_lock(volatile uint32_t* lock) {
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(lock, __ATOMIC_RELAXED)) {
            cpu_relax();
        }
    }
}

static inline void spin_unlock(volatile uint32_t* lock) {
    __atomic_clear(lock, __ATOMIC_RELEASE);
}

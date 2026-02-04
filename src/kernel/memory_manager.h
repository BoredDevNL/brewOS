#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Memory Manager Configuration
#define MEMORY_POOL_SIZE (4 * 1024 * 1024)  // 4MB pool
#define MAX_ALLOCATIONS 1024
#define MAX_FRAGMENTATION_SLOTS 2048

// Allocation block metadata
typedef struct {
    void *address;
    size_t size;
    bool allocated;
    uint32_t allocation_id;
    uint32_t timestamp;
} MemBlock;

// Memory statistics
typedef struct {
    size_t total_memory;
    size_t used_memory;
    size_t available_memory;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t largest_free_block;
    size_t smallest_free_block;
    size_t fragmentation_percent;
    size_t peak_memory_used;
} MemStats;

// Public API
void memory_manager_init(void);

// Allocation/Deallocation
void* kmalloc(size_t size);
void kfree(void *ptr);
void* krealloc(void *ptr, size_t new_size);

// Statistics and Information
MemStats memory_get_stats(void);
void memory_print_stats(void);
void memory_print_detailed(void);

void memory_validate(void);
void memory_dump_blocks(void);

// Internal utilities
size_t memory_get_peak_usage(void);
void memory_reset_peak(void);
bool memory_is_valid_ptr(void *ptr);

#endif // MEMORY_MANAGER_H

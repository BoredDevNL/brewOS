#include "memory_manager.h"
#include "io.h"
#include <stdint.h>

// --- Internal State ---
static uint8_t *memory_pool = NULL;  // Dynamically allocated
static size_t memory_pool_size = DEFAULT_POOL_SIZE;  // Track actual pool size
static MemBlock block_list[MAX_ALLOCATIONS];
static int block_count = 0;
static size_t total_allocated = 0;
static size_t peak_allocated = 0;
static uint32_t allocation_counter = 0;
static bool initialized = false;

// --- Helper Functions ---

// Simple memset for internal use
static void mem_memset(void *dest, int val, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    while (len-- > 0) {
        *ptr++ = (uint8_t)val;
    }
}

// Simple memmove
static void mem_memmove(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    
    if (d < s) {
        while (len--) *d++ = *s++;
    } else {
        d += len;
        s += len;
        while (len--) *(--d) = *(--s);
    }
}

// Get current time in ticks (simple counter)
static uint32_t get_timestamp(void) {
    static uint32_t tick = 0;
    return tick++;
}

// Find free space in memory pool
static void* find_free_space(size_t size) {
    size_t offset = 0;
    
    while (offset + size <= memory_pool_size) {
        bool space_free = true;
        
        // Check if this range is free
        for (int i = 0; i < block_count; i++) {
            if (!block_list[i].allocated) continue;
            
            void *block_start = block_list[i].address;
            void *block_end = (uint8_t *)block_start + block_list[i].size;
            void *check_start = (uint8_t *)memory_pool + offset;
            void *check_end = (uint8_t *)check_start + size;
            
            // Check for overlap
            if (check_start < block_end && check_end > block_start) {
                space_free = false;
                
                // Skip past this block
                size_t block_offset = (uintptr_t)block_end - (uintptr_t)memory_pool;
                if (block_offset > offset) {
                    offset = block_offset;
                }
                break;
            }
        }
        
        if (space_free) {
            return (uint8_t *)memory_pool + offset;
        }
    }
    
    return NULL;
}

// Calculate fragmentation
static size_t calculate_fragmentation(void) {
    if (total_allocated == 0) return 0;
    
    // Sort blocks by address
    for (int i = 0; i < block_count - 1; i++) {
        for (int j = i + 1; j < block_count; j++) {
            if ((uintptr_t)block_list[i].address > (uintptr_t)block_list[j].address) {
                MemBlock tmp = block_list[i];
                block_list[i] = block_list[j];
                block_list[j] = tmp;
            }
        }
    }
    
    // Count gaps between allocated blocks
    size_t total_gaps = 0;
    void *pool_end = (uint8_t *)memory_pool + memory_pool_size;
    
    void *current_end = memory_pool;
    
    for (int i = 0; i < block_count; i++) {
        if (!block_list[i].allocated) continue;
        
        if (block_list[i].address > current_end) {
            total_gaps += (uintptr_t)block_list[i].address - (uintptr_t)current_end;
        }
        
        current_end = (uint8_t *)block_list[i].address + block_list[i].size;
    }
    
    if (total_allocated == 0) return 0;
    return (total_gaps * 100) / total_allocated;
}

// --- Public API ---

void memory_manager_init_with_size(size_t pool_size) {
    if (initialized) return;
    
    memory_pool_size = pool_size;
    
    // Initialize memory pool - in a real kernel, this would be passed a physical address
    // For now, we use a simple tracking mechanism where allocations are tracked virtually
    // The caller is responsible for ensuring the pool_size is valid
    if (memory_pool == NULL) {
        memory_pool = (uint8_t *)0x100000000;  // Start from 4GB boundary as virtual tracking
    }
    
    // Clear metadata
    mem_memset(block_list, 0, sizeof(block_list));
    block_count = 0;
    total_allocated = 0;
    peak_allocated = 0;
    allocation_counter = 0;
    
    // Create initial free block representing entire pool
    block_list[0].address = memory_pool;
    block_list[0].size = memory_pool_size;
    block_list[0].allocated = false;
    block_list[0].allocation_id = 0;
    block_count = 1;
    
    initialized = true;
}

void memory_manager_init(void) {
    memory_manager_init_with_size(DEFAULT_POOL_SIZE);
}

void* kmalloc(size_t size) {
    if (!initialized) {
        memory_manager_init();
    }
    
    if (size == 0 || size > memory_pool_size) {
        return NULL;
    }
    
    // Check if we can allocate
    if (total_allocated + size > memory_pool_size) {
        return NULL;
    }
    
    // Find free space
    void *ptr = find_free_space(size);
    if (ptr == NULL) {
        return NULL;
    }
    
    // Add block entry
    if (block_count >= MAX_ALLOCATIONS) {
        return NULL;
    }
    
    allocation_counter++;
    int idx = block_count++;
    
    block_list[idx].address = ptr;
    block_list[idx].size = size;
    block_list[idx].allocated = true;
    block_list[idx].allocation_id = allocation_counter;
    block_list[idx].timestamp = get_timestamp();
    
    total_allocated += size;
    if (total_allocated > peak_allocated) {
        peak_allocated = total_allocated;
    }
    
    // Clear memory
    mem_memset(ptr, 0, size);
    
    return ptr;
}

void kfree(void *ptr) {
    if (ptr == NULL || !initialized) {
        return;
    }
    
    // Find and free the block
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated && block_list[i].address == ptr) {
            total_allocated -= block_list[i].size;
            block_list[i].allocated = false;
            
            // Compact: remove freed entry and shift remaining
            for (int j = i; j < block_count - 1; j++) {
                block_list[j] = block_list[j + 1];
            }
            block_count--;
            return;
        }
    }
}

void* krealloc(void *ptr, size_t new_size) {
    if (!initialized) {
        memory_manager_init();
    }
    
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    if (ptr == NULL) {
        return kmalloc(new_size);
    }
    
    // Find the block
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated && block_list[i].address == ptr) {
            if (block_list[i].size >= new_size) {
                // Allocation is large enough
                return ptr;
            }
            
            // Need to allocate new space
            void *new_ptr = kmalloc(new_size);
            if (new_ptr == NULL) {
                return NULL;
            }
            
            // Copy data
            mem_memmove(new_ptr, ptr, block_list[i].size);
            
            // Free old pointer
            kfree(ptr);
            
            return new_ptr;
        }
    }
    
    return NULL;
}

MemStats memory_get_stats(void) {
    MemStats stats;
    
    stats.total_memory = memory_pool_size;
    stats.used_memory = total_allocated;
    stats.available_memory = memory_pool_size - total_allocated;
    stats.allocated_blocks = 0;
    stats.free_blocks = 0;
    stats.largest_free_block = 0;
    stats.smallest_free_block = memory_pool_size;
    stats.peak_memory_used = peak_allocated;
    
    // Count and analyze blocks
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated) {
            stats.allocated_blocks++;
        } else {
            stats.free_blocks++;
            if (block_list[i].size > stats.largest_free_block) {
                stats.largest_free_block = block_list[i].size;
            }
            if (block_list[i].size < stats.smallest_free_block) {
                stats.smallest_free_block = block_list[i].size;
            }
        }
    }
    
    if (stats.free_blocks == 0) {
        stats.smallest_free_block = 0;
    }
    
    stats.fragmentation_percent = calculate_fragmentation();
    
    return stats;
}

void memory_print_stats(void) {
    MemStats stats = memory_get_stats();
    
    // We need to use the CLI write functions - declare them as extern
    extern void cmd_write(const char *str);
    extern void cmd_write_int(int n);
    extern void cmd_putchar(char c);
    
    cmd_write("\n=== MEMORY STATISTICS ===\n");
    cmd_write("Total Memory:     ");
    cmd_write_int(stats.total_memory / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Used Memory:      ");
    cmd_write_int(stats.used_memory / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Available Memory: ");
    cmd_write_int(stats.available_memory / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Allocated Blocks: ");
    cmd_write_int(stats.allocated_blocks);
    cmd_write("\n");
    
    cmd_write("Free Blocks:      ");
    cmd_write_int(stats.free_blocks);
    cmd_write("\n");
    
    cmd_write("Largest Free:     ");
    cmd_write_int(stats.largest_free_block / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Peak Usage:       ");
    cmd_write_int(stats.peak_memory_used / 1024);
    cmd_write(" KB\n");
    
    cmd_write("Fragmentation:    ");
    cmd_write_int(stats.fragmentation_percent);
    cmd_write("%\n");
    
    cmd_write("Usage:            ");
    int usage_percent = (stats.used_memory * 100) / stats.total_memory;
    cmd_write_int(usage_percent);
    cmd_write("%\n");
    
    cmd_write("========================\n\n");
}

void memory_print_detailed(void) {
    extern void cmd_write(const char *str);
    extern void cmd_write_int(int n);
    extern void cmd_putchar(char c);
    
    cmd_write("\n=== DETAILED MEMORY BLOCKS ===\n");
    cmd_write("ID       Address   Size        Status\n");
    cmd_write("------   --------  --------    --------\n");
    
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].size == 0) continue;
        
        // ID
        cmd_write_int(block_list[i].allocation_id);
        cmd_write("       ");
        
        // Address (simplified hex output)
        cmd_write("0x");
        cmd_write_int((uintptr_t)block_list[i].address / 1024);
        cmd_write("  ");
        
        // Size
        cmd_write_int(block_list[i].size / 1024);
        cmd_write("KB      ");
        
        // Status
        if (block_list[i].allocated) {
            cmd_write("ALLOC\n");
        } else {
            cmd_write("FREE\n");
        }
    }
    
    cmd_write("==============================\n\n");
}

void memory_validate(void) {
    extern void cmd_write(const char *str);
    extern void cmd_write_int(int n);
    
    int errors = 0;
    
    // Check for overlapping blocks
    for (int i = 0; i < block_count; i++) {
        for (int j = i + 1; j < block_count; j++) {
            void *i_start = block_list[i].address;
            void *i_end = (uint8_t *)i_start + block_list[i].size;
            void *j_start = block_list[j].address;
            void *j_end = (uint8_t *)j_start + block_list[j].size;
            
            if (i_start < j_end && i_end > j_start) {
                errors++;
                cmd_write("ERROR: Overlapping blocks detected!\n");
            }
        }
    }
    
    if (errors == 0) {
        cmd_write("Memory validation: OK\n");
    } else {
        cmd_write("Memory validation failed with ");
        cmd_write_int(errors);
        cmd_write(" errors\n");
    }
}

void memory_dump_blocks(void) {
    extern void cmd_write(const char *str);
    extern void cmd_write_int(int n);
    
    cmd_write("\nMemory block dump:\n");
    cmd_write("Total blocks: ");
    cmd_write_int(block_count);
    cmd_write("\n");
    
    memory_print_detailed();
}

size_t memory_get_peak_usage(void) {
    return peak_allocated;
}

void memory_reset_peak(void) {
    peak_allocated = total_allocated;
}

bool memory_is_valid_ptr(void *ptr) {
    if (ptr == NULL) return false;
    
    void *pool_start = memory_pool;
    void *pool_end = (uint8_t *)memory_pool + memory_pool_size;
    
    if (ptr < pool_start || ptr >= pool_end) {
        return false;
    }
    
    // Check if it's an allocated block
    for (int i = 0; i < block_count; i++) {
        if (block_list[i].allocated && block_list[i].address == ptr) {
            return true;
        }
    }
    
    return false;
}

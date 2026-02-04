#include "cli_utils.h"
#include "../memory_manager.h"

#define MAX_TEST_ALLOCS 64
static void *test_allocs[MAX_TEST_ALLOCS];
static int test_alloc_count = 0;

void cli_cmd_malloc(char *args) {
    if (!args || !args[0]) {
        cli_write("Usage: malloc <size_in_kb>\n");
        cli_write("Example: malloc 10\n");
        return;
    }
    
    // Parse size in KB
    int size_kb = cli_atoi(args);
    if (size_kb <= 0 || size_kb > 1024) {
        cli_write("Invalid size. Use 1-1024 KB\n");
        return;
    }
    
    size_t size = size_kb * 1024;
    void *ptr = kmalloc(size);
    
    if (ptr == NULL) {
        cli_write("Allocation failed!\n");
        return;
    }
    
    // Track allocation for later freeing
    if (test_alloc_count < MAX_TEST_ALLOCS) {
        test_allocs[test_alloc_count++] = ptr;
    }
    
    cli_write("Allocated ");
    cli_write_int(size_kb);
    cli_write("KB at address 0x");
    cli_write_int((uintptr_t)ptr / 1024);
    cli_write("\n");
    cli_write("Test allocation index: ");
    cli_write_int(test_alloc_count - 1);
    cli_write("\n");
    
    memory_print_stats();
}

void cli_cmd_free_mem(char *args) {
    if (!args || !args[0]) {
        cli_write("Usage: freemem <index>\n");
        cli_write("Specify the allocation index from malloc output\n");
        return;
    }
    
    int idx = cli_atoi(args);
    if (idx < 0 || idx >= test_alloc_count) {
        cli_write("Invalid index. Must be 0-");
        cli_write_int(test_alloc_count - 1);
        cli_write("\n");
        return;
    }
    
    void *ptr = test_allocs[idx];
    if (ptr == NULL) {
        cli_write("Allocation at index ");
        cli_write_int(idx);
        cli_write(" is NULL\n");
        return;
    }
    
    kfree(ptr);
    test_allocs[idx] = NULL;
    
    cli_write("Freed allocation at index ");
    cli_write_int(idx);
    cli_write("\n");
    
    memory_print_stats();
}

void cli_cmd_memblock(char *args) {
    (void)args;
    
    cli_write("Detailed block information:\n");
    memory_print_detailed();
}

void cli_cmd_memvalid(char *args) {
    (void)args;
    
    cli_write("Validating memory integrity...\n");
    memory_validate();
}

void cli_cmd_memtest(char *args) {
    (void)args;
    
    cli_write("\n=== MEMORY STRESS TEST ===\n");
    
    // Allocate multiple blocks
    cli_write("Allocating 10 blocks of 256KB each...\n");
    void *test_ptrs[10];
    
    for (int i = 0; i < 10; i++) {
        test_ptrs[i] = kmalloc(256 * 1024);
        if (test_ptrs[i] == NULL) {
            cli_write("Allocation ");
            cli_write_int(i);
            cli_write(" failed\n");
            
            // Free previous allocations
            for (int j = 0; j < i; j++) {
                kfree(test_ptrs[j]);
            }
            return;
        }
        cli_write("Allocated block ");
        cli_write_int(i);
        cli_write("\n");
    }
    
    memory_print_stats();
    
    // Free every other block (create fragmentation)
    cli_write("\nFreeing alternate blocks to create fragmentation...\n");
    for (int i = 0; i < 10; i += 2) {
        kfree(test_ptrs[i]);
        cli_write("Freed block ");
        cli_write_int(i);
        cli_write("\n");
    }
    
    memory_print_stats();
    
    // Free remaining blocks
    cli_write("\nFreeing remaining blocks...\n");
    for (int i = 1; i < 10; i += 2) {
        kfree(test_ptrs[i]);
    }
    
    memory_print_stats();
    
    cli_write("=== TEST COMPLETE ===\n\n");
}

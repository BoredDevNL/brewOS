#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "limine.h"
#include "graphics.h"
#include "idt.h"
#include "ps2.h"
#include "wm.h"
#include "io.h"
#include "memory_manager.h"
#include "platform.h"

// --- Limine Requests ---
__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 1
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests_start")))
static volatile struct limine_request *const requests_start_marker[] = {
    (struct limine_request *)&framebuffer_request,
    (struct limine_request *)&memmap_request,
    NULL
};

__attribute__((used, section(".requests_end")))
static volatile struct limine_request *const requests_end_marker[] = {
    NULL
};

static void hcf(void) {
    asm("cli");
    for (;;) {
        asm("hlt");
    }
}

// Kernel Entry Point
void kmain(void) {
    platform_init();
    // 1. Graphics Init
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        // Warning
    }

    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    graphics_init(fb);

    // 2. Interrupts Init
    idt_init();
    
    // Register ISRs with correct CS
    idt_register_interrupts();
    
    // Load IDT and Enable Interrupts
    idt_load();

    // 2.5 Memory Manager Init - Calculate available RAM from Limine
    size_t total_usable_memory = 0;
    if (memmap_request.response != NULL) {
        for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
            struct limine_memmap_entry *entry = memmap_request.response->entries[i];
            
            // Count usable memory regions
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                total_usable_memory += entry->length;
            }
        }
    }
    
    // Initialize memory manager with available memory (cap at 2GB for practical reasons)
    size_t pool_size = total_usable_memory > (2 * 1024 * 1024 * 1024) ? 
                       (2 * 1024 * 1024 * 1024) : total_usable_memory;
    
    if (pool_size == 0) {
        pool_size = 512 * 1024 * 1024;  // Fallback to 512MB if detection fails
    }
    
    memory_manager_init_with_size(pool_size);

    // 3. PS/2 Init (Mouse/Keyboard)
    asm("cli");
    ps2_init();
    asm("sti");

    // 4. Window Manager Init (Draws initial desktop)
    wm_init();

    // 5. Main loop - just wait for interrupts
    // Timer interrupt will drive the redraw system
    while (1) {
        wm_process_input();
        asm("hlt");
    }
}

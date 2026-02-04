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

// --- Limine Requests ---
__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 1
};

__attribute__((used, section(".requests_start")))
static volatile struct limine_request *const requests_start_marker[] = {
    (struct limine_request *)&framebuffer_request,
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

    // 2.5 Memory Manager Init
    memory_manager_init();

    // 3. PS/2 Init (Mouse/Keyboard)
    asm("cli");
    ps2_init();
    asm("sti");

    // 4. Window Manager Init (Draws initial desktop)
    wm_init();

    // 5. Main loop - just wait for interrupts
    // Timer interrupt will drive the redraw system
    while (1) {
        asm("hlt");
    }
}
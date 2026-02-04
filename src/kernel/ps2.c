#include "ps2.h"
#include "io.h"
#include "wm.h"
#include <stdbool.h>

extern void serial_print(const char *s);
extern void serial_print_hex(uint64_t n);

// --- Timer Handler ---
void timer_handler(void) {
    wm_timer_tick();
    outb(0x20, 0x20); // EOI to Master PIC
}

// --- Keyboard ---
static bool shift_pressed = false;
static bool extended_scancode = false;

// Simple US QWERTY Scan Code Set 1 Map
static char scancode_map[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 
    0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

static char scancode_map_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 
    0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended_scancode = true;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) { // Shift Down
        shift_pressed = true;
    } else if (scancode == 0xAA || scancode == 0xB6) { // Shift Up
        shift_pressed = false;
    } else if (!(scancode & 0x80)) { // Key Press (not release)
        if (extended_scancode) {
            // Extended scancode - arrow keys and special keys
            extended_scancode = false;
            switch (scancode) {
                case 0x48: wm_handle_key(17); break; // Up arrow
                case 0x50: wm_handle_key(18); break; // Down arrow
                case 0x4B: wm_handle_key(19); break; // Left arrow
                case 0x4D: wm_handle_key(20); break; // Right arrow
            }
        } else {
            // Regular scancode
            char c = shift_pressed ? scancode_map_shift[scancode] : scancode_map[scancode];
            if (c) {
                wm_handle_key(c);
            }
        }
    } else if (scancode & 0x80) {
        // Key release
        extended_scancode = false;
    }

    outb(0x20, 0x20); // EOI
}

// --- Mouse ---
static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[3];

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) { // Write
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    } else { // Read
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    }
}

void mouse_write(uint8_t write) {
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, write);
}

uint8_t mouse_read(void) {
    mouse_wait(1);
    return inb(0x60);
}

void mouse_init(void) {
    uint8_t status;
    
    // Enable Aux Device
    mouse_wait(0);
    outb(0x64, 0xA8);
    
    // Enable Interrupts
    mouse_wait(0);
    outb(0x64, 0x20);
    mouse_wait(1);
    status = inb(0x60) | 2;
    mouse_wait(0);
    outb(0x64, 0x60);
    mouse_wait(0);
    outb(0x60, status);
    
    // Set Defaults
    mouse_write(0xF6);
    mouse_read();
    
    // Enable Streaming
    mouse_write(0xF4);
    mouse_read();
}

void mouse_handler(void) {
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) {
        outb(0x20, 0x20);
        outb(0xA0, 0x20);
        return;
    }

    uint8_t b = inb(0x60);
    
    if (mouse_cycle == 0) {
        if ((b & 0x08) == 0) { // Sync check
             // Skip
        } else {
             mouse_byte[0] = b;
             mouse_cycle++;
        }
    } else if (mouse_cycle == 1) {
        mouse_byte[1] = b;
        mouse_cycle++;
    } else {
        mouse_byte[2] = b;
        mouse_cycle = 0;
        
        // Packet Full
        int8_t dx = mouse_byte[1];
        int8_t dy = mouse_byte[2]; 
        
        // Send to WM
        wm_handle_mouse(dx, -dy, mouse_byte[0] & 0x07);
    }

    outb(0x20, 0x20);
    outb(0xA0, 0x20); // Slave EOI
}

void ps2_init(void) {
    mouse_init();
}
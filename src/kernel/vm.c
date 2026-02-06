#include "vm.h"
#include "cmd.h"
#include "memory_manager.h"
#include "graphics.h"
#include "wm.h"
#include "fat32.h"
#include "rtc.h"
#include "ps2.h"
#include "cli_apps/cli_utils.h"
#include "io.h"

// --- Scancode Map (Set 1) ---
static char vm_scancode_map[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 
    0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

// VM State
static int stack[VM_STACK_SIZE];
static unsigned long int rand_next = 1;
static int sp = 0;
static uint8_t memory[VM_MEMORY_SIZE]; // 64KB Linear RAM
static int vm_heap_ptr = 8192;

// --- Graphics Overlay Support ---
typedef struct {
    int x, y, w, h;
    uint32_t color;
} VMRect;

#define MAX_VM_RECTS 256
static VMRect vm_rects[MAX_VM_RECTS];
static int vm_rect_count = 0;

static void vm_paint_overlay(void) {
    for (int i = 0; i < vm_rect_count; i++) {
        draw_rect(vm_rects[i].x, vm_rects[i].y, vm_rects[i].w, vm_rects[i].h, vm_rects[i].color);
    }
}

// Helper to access memory as int
static int mem_read32(int addr) {
    if (addr < 0 || addr > VM_MEMORY_SIZE - 4) return 0;
    int val = 0;
    val |= memory[addr];
    val |= memory[addr+1] << 8;
    val |= memory[addr+2] << 16;
    val |= memory[addr+3] << 24;
    return val;
}

static void mem_write32(int addr, int val) {
    if (addr < 0 || addr > VM_MEMORY_SIZE - 4) return;
    memory[addr] = val & 0xFF;
    memory[addr+1] = (val >> 8) & 0xFF;
    memory[addr+2] = (val >> 16) & 0xFF;
    memory[addr+3] = (val >> 24) & 0xFF;
}

static void vm_reset(void) {
    sp = 0;
    cli_memset(memory, 0, VM_MEMORY_SIZE);
    vm_heap_ptr = 8192;
}

static void push(int val) {
    if (sp < VM_STACK_SIZE) {
        stack[sp++] = val;
    } else {
        cmd_write("VM Error: Stack Overflow\n");
    }
}

static int pop(void) {
    if (sp > 0) {
        return stack[--sp];
    }
    cmd_write("VM Error: Stack Underflow\n");
    return 0;
}

// Syscall Implementations
static void vm_syscall(int id) {
    switch (id) {
        case SYS_EXIT:
            // Handled by return code in main loop usually, but here just do nothing or treat as halt
            push(0);
            break;
        case SYS_PRINT_INT:
            cmd_write_int(pop());
            push(0);
            break;
        case SYS_PRINT_CHAR: {
            char c = (char)pop();
            char s[2] = {c, 0};
            cmd_write(s);
            push(0);
            break;
        }
        case SYS_PRINT_STR: {
            int addr = pop();
            if (addr >= 0 && addr < VM_MEMORY_SIZE) {
                cmd_write((char*)&memory[addr]);
            }
            push(0);
            break;
        }
        case SYS_NL:
            cmd_write("\n");
            push(0);
            break;
        case SYS_CLS:
            cmd_screen_clear();
            push(0);
            break;
        case SYS_GETCHAR: {
            int c = 0;
            // Blocking read for a valid key press
            while (1) {
                if ((inb(0x64) & 1)) { // Data available
                    uint8_t sc = inb(0x60);
                    if (!(sc & 0x80)) { // Key press
                        if (sc < 128) {
                            c = vm_scancode_map[sc];
                            if (c) break;
                        }
                    }
                }
            }
            push(c);
            break;
        }
        case SYS_KB_HIT:
            // Simple check if data is waiting in keyboard controller
            push((inb(0x64) & 1) ? 1 : 0);
            break;
        case SYS_STRLEN: {
            int addr = pop();
            if (addr >= 0 && addr < VM_MEMORY_SIZE) {
                push(cli_strlen((char*)&memory[addr]));
            } else push(0);
            break;
        }
        case SYS_STRCMP: {
            int a2 = pop();
            int a1 = pop();
            if (a1 >= 0 && a1 < VM_MEMORY_SIZE && a2 >= 0 && a2 < VM_MEMORY_SIZE) {
                push(cli_strcmp((char*)&memory[a1], (char*)&memory[a2]));
            } else push(0);
            break;
        }
        case SYS_STRCPY: {
            int src = pop();
            int dest = pop();
             if (dest >= 0 && dest < VM_MEMORY_SIZE && src >= 0 && src < VM_MEMORY_SIZE) {
                cli_strcpy((char*)&memory[dest], (char*)&memory[src]);
                push(dest);
             } else push(0);
            break;
        }
        case SYS_STRCAT: {
             // Not implemented in cli_utils, skip
             pop(); pop(); push(0);
             break;
        }
        case SYS_MEMSET: {
            int n = pop();
            int val = pop();
            int ptr = pop();
            if (ptr >= 0 && ptr + n <= VM_MEMORY_SIZE) {
                cli_memset(&memory[ptr], val, n);
                push(ptr);
            } else push(0);
            break;
        }
        case SYS_MEMCPY: {
            int n = pop();
            int src = pop();
            int dest = pop();
            if (dest >= 0 && dest+n <= VM_MEMORY_SIZE && src >= 0 && src+n <= VM_MEMORY_SIZE) {
                for(int i=0; i<n; i++) memory[dest+i] = memory[src+i];
                push(dest);
            } else push(0);
            break;
        }
        // Simplified Heap (using top of memory growing down?)
        // For now, static allocation or mapped.
        // Let's implement a dummy malloc that returns an index into memory
        // Starting at 1024 (reserve first 1K for globals)
        case SYS_MALLOC: {
            int size = pop();
            int res = vm_heap_ptr;
            vm_heap_ptr += size;
            if (vm_heap_ptr >= VM_MEMORY_SIZE) {
                push(0); // OOM
            } else {
                push(res);
            }
            break;
        }
        case SYS_FREE:
            pop(); // No-op
            push(0);
            break;
        case SYS_RAND: {
            rand_next = rand_next * 1103515245 + 12345;
            push((unsigned int)(rand_next/65536) % 32768);
            break;
        }
        case SYS_SRAND: {
            rand_next = pop();
            push(0);
            break;
        }
        case SYS_ABS: {
            int x = pop();
            push(x < 0 ? -x : x);
            break;
        }
        case SYS_MIN: {
            int b = pop();
            int a = pop();
            push(a < b ? a : b);
            break;
        }
        case SYS_MAX: {
            int b = pop();
            int a = pop();
            push(a > b ? a : b);
            break;
        }
        case SYS_POW: {
            int exp = pop();
            int base = pop();
            int res = 1;
            for(int i=0; i<exp; i++) res *= base;
            push(res);
            break;
        }
        case SYS_SQRT: {
             int n = pop();
             int res = 0;
             while ((res*res) <= n) res++;
             push(res - 1);
             break;
        }
        case SYS_SLEEP:
            cli_delay(pop() * 100000);
            push(0);
            break;
        // File IO - Not supported yet as FILE* cannot be easily passed to VM
        case SYS_FOPEN: pop(); pop(); push(0); break;
        case SYS_FCLOSE: pop(); push(0); break;
        case SYS_FREAD: pop(); pop(); pop(); pop(); push(0); break;
        case SYS_FWRITE: pop(); pop(); pop(); pop(); push(0); break;
        case SYS_FSEEK: pop(); pop(); pop(); push(0); break;
        case SYS_REMOVE: pop(); push(0); break;
        
        case SYS_DRAW_PIXEL: {
            int color = pop();
            int y = pop();
            int x = pop();
            put_pixel(x, y, color);
            push(0);
            break;
        }
        case SYS_DRAW_RECT: {
            int color = pop();
            int h = pop();
            int w = pop();
            int y = pop();
            int x = pop();
            
            // Store for overlay
            if (vm_rect_count < MAX_VM_RECTS) {
                vm_rects[vm_rect_count].x = x;
                vm_rects[vm_rect_count].y = y;
                vm_rects[vm_rect_count].w = w;
                vm_rects[vm_rect_count].h = h;
                vm_rects[vm_rect_count].color = color;
                vm_rect_count++;
            }
            
            // Trigger repaint
            wm_mark_dirty(x, y, w, h);
            
            push(0);
            break;
        }
        case SYS_GET_WIDTH: push(get_screen_width()); break;
        case SYS_GET_HEIGHT: push(get_screen_height()); break;
        
        case SYS_ATOI: {
             int addr = pop();
             if (addr >= 0 && addr < VM_MEMORY_SIZE) {
                 push(cli_atoi((char*)&memory[addr]));
             } else push(0);
             break;
        }
        case SYS_ITOA: {
            int addr = pop();
            int val = pop();
            if (addr >= 0 && addr < VM_MEMORY_SIZE) {
                cli_itoa(val, (char*)&memory[addr]);
            }
            push(0);
            break;
        }
        case SYS_PEEK: push(mem_read32(pop())); break;
        case SYS_POKE: {
            int val = pop();
            int addr = pop();
            mem_write32(addr, val);
            push(0);
            break;
        }
        case SYS_EXEC: pop(); push(-1); break; // Not impl
        case SYS_SYSTEM: pop(); push(-1); break; // Not impl
        
        // --- New Builtins ---
        case SYS_ISALNUM: {
            int c = pop();
            push(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')));
            break;
        }
        case SYS_ISALPHA: {
            int c = pop();
            push(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')));
            break;
        }
        case SYS_ISDIGIT: {
            int c = pop();
            push((c >= '0' && c <= '9'));
            break;
        }
        case SYS_TOLOWER: {
            int c = pop();
            if (c >= 'A' && c <= 'Z') push(c + 32);
            else push(c);
            break;
        }
        case SYS_TOUPPER: {
            int c = pop();
            if (c >= 'a' && c <= 'z') push(c - 32);
            else push(c);
            break;
        }
        case SYS_STRNCPY: {
            int n = pop();
            int src = pop();
            int dest = pop();
            if (dest >= 0 && dest+n <= VM_MEMORY_SIZE && src >= 0 && src+n <= VM_MEMORY_SIZE) {
                char *d = (char*)&memory[dest];
                char *s = (char*)&memory[src];
                int i;
                for (i = 0; i < n && s[i] != '\0'; i++) d[i] = s[i];
                for ( ; i < n; i++) d[i] = '\0';
                push(dest);
            } else push(0);
            break;
        }
        case SYS_STRNCAT: {
            int n = pop();
            int src = pop();
            int dest = pop();
            if (dest >= 0 && dest < VM_MEMORY_SIZE && src >= 0 && src < VM_MEMORY_SIZE) {
                char *d = (char*)&memory[dest];
                char *s = (char*)&memory[src];
                int d_len = 0; while (d[d_len]) d_len++;
                int i;
                for (i = 0; i < n && s[i] != '\0'; i++) {
                    if (dest + d_len + i < VM_MEMORY_SIZE) d[d_len + i] = s[i];
                }
                if (dest + d_len + i < VM_MEMORY_SIZE) d[d_len + i] = '\0';
                push(dest);
            } else push(0);
            break;
        }
        case SYS_STRNCMP: {
            int n = pop();
            int s2 = pop();
            int s1 = pop();
            if (s1 >= 0 && s1 < VM_MEMORY_SIZE && s2 >= 0 && s2 < VM_MEMORY_SIZE) {
                char *p1 = (char*)&memory[s1];
                char *p2 = (char*)&memory[s2];
                int res = 0;
                for (int i = 0; i < n; i++) {
                    if (p1[i] != p2[i] || p1[i] == '\0' || p2[i] == '\0') {
                        res = (unsigned char)p1[i] - (unsigned char)p2[i];
                        break;
                    }
                }
                push(res);
            } else push(0);
            break;
        }
        case SYS_STRSTR: {
            int needle = pop();
            int haystack = pop();
             if (haystack >= 0 && haystack < VM_MEMORY_SIZE && needle >= 0 && needle < VM_MEMORY_SIZE) {
                char *h = (char*)&memory[haystack];
                char *n = (char*)&memory[needle];
                int h_len = 0; while(h[h_len]) h_len++;
                int n_len = 0; while(n[n_len]) n_len++;
                int found_idx = -1;
                for (int i = 0; i <= h_len - n_len; i++) {
                    bool match = true;
                    for (int j = 0; j < n_len; j++) {
                        if (h[i+j] != n[j]) { match = false; break; }
                    }
                    if (match) { found_idx = haystack + i; break; }
                }
                push(found_idx != -1 ? found_idx : 0); // Return ptr or null (0)
             } else push(0);
            break;
        }
        case SYS_STRRCHR: {
            int c = pop();
            int s = pop();
            if (s >= 0 && s < VM_MEMORY_SIZE) {
                char *str = (char*)&memory[s];
                int len = 0; while(str[len]) len++;
                int found_idx = 0;
                for (int i = len; i >= 0; i--) {
                    if (str[i] == c) {
                        found_idx = s + i;
                        break;
                    }
                }
                push(found_idx);
            } else push(0);
            break;
        }
        case SYS_MEMMOVE: {
            int n = pop();
            int src = pop();
            int dest = pop();
            if (dest >= 0 && dest+n <= VM_MEMORY_SIZE && src >= 0 && src+n <= VM_MEMORY_SIZE) {
                uint8_t *d = &memory[dest];
                uint8_t *s = &memory[src];
                if (d < s) {
                    for (int i = 0; i < n; i++) d[i] = s[i];
                } else {
                    for (int i = n - 1; i >= 0; i--) d[i] = s[i];
                }
                push(dest);
            } else push(0);
            break;
        }

        default:
            cmd_write("VM: Unknown Syscall\n");
            break;
    }
}

int vm_exec(const uint8_t *code, int code_size) {
    if (code_size < 8) return -1;
    for (int i = 0; i < 7; i++) {
        if (code[i] != VM_MAGIC[i]) return -1;
    }

    vm_reset();
    
    // Reset Graphics Overlay
    vm_rect_count = 0;
    wm_custom_paint_hook = vm_paint_overlay;
    
    // Copy data segment if any?
    // For this simple VM, we will copy the ENTIRE bytecode to the start of memory
    // So code and data share the same space (Von Neumann)
    // But we need to be careful not to overwrite running code if we write to it
    
    // Safety check
    if (code_size > VM_MEMORY_SIZE) {
        cmd_write("VM Error: Binary too large\n");
        wm_custom_paint_hook = NULL;
        return -1;
    }
    
    // Load program into memory at address 0
    cli_memset(memory, 0, VM_MEMORY_SIZE);
    for(int i=0; i<code_size; i++) memory[i] = code[i];
    
    int pc = 8; // Skip header

    while (pc < code_size) {
        uint8_t op = memory[pc++];
        
        // DEBUGGING
        /*
        cmd_write("PC:"); cmd_write_int(pc-1); 
        cmd_write(" OP:"); cmd_write_int(op);
        cmd_write(" SP:"); cmd_write_int(sp);
        cmd_write("\n");
        */

        switch (op) {
            case OP_HALT: 
                wm_custom_paint_hook = NULL;
                return 0;
            case OP_IMM: {
                int val = 0;
                val |= memory[pc++];
                val |= memory[pc++] << 8;
                val |= memory[pc++] << 16;
                val |= memory[pc++] << 24;
                push(val);
                break;
            }
            case OP_LOAD: { // Read 32-bit int from absolute address
                int addr = 0;
                addr |= memory[pc++];
                addr |= memory[pc++] << 8;
                addr |= memory[pc++] << 16;
                addr |= memory[pc++] << 24;
                push(mem_read32(addr));
                break;
            }
            case OP_STORE: {
                int addr = 0;
                addr |= memory[pc++];
                addr |= memory[pc++] << 8;
                addr |= memory[pc++] << 16;
                addr |= memory[pc++] << 24;
                mem_write32(addr, pop());
                break;
            }
            case OP_LOAD8: { // Read byte
                int addr = 0;
                addr |= memory[pc++];
                addr |= memory[pc++] << 8;
                addr |= memory[pc++] << 16;
                addr |= memory[pc++] << 24;
                if (addr >= 0 && addr < VM_MEMORY_SIZE) push(memory[addr]);
                else push(0);
                break;
            }
            case OP_STORE8: {
                 int addr = 0;
                addr |= memory[pc++];
                addr |= memory[pc++] << 8;
                addr |= memory[pc++] << 16;
                addr |= memory[pc++] << 24;
                int val = pop();
                if (addr >= 0 && addr < VM_MEMORY_SIZE) memory[addr] = (uint8_t)val;
                break;
            }
            case OP_ADD: push(pop() + pop()); break;
            case OP_SUB: { int b=pop(); int a=pop(); push(a-b); } break;
            case OP_MUL: push(pop() * pop()); break;
            case OP_DIV: { int b=pop(); int a=pop(); push(b==0?0:a/b); } break;
            case OP_PRINT: cmd_write_int(pop()); cmd_write("\n"); break;
            case OP_PRITC: { char c=(char)pop(); char s[2]={c,0}; cmd_write(s); } break;
            case OP_JMP: {
                int addr = 0;
                addr |= memory[pc++];
                addr |= memory[pc++] << 8;
                addr |= memory[pc++] << 16;
                addr |= memory[pc++] << 24;
                pc = addr;
                break;
            }
            case OP_JZ: {
                int addr = 0;
                addr |= memory[pc++];
                addr |= memory[pc++] << 8;
                addr |= memory[pc++] << 16;
                addr |= memory[pc++] << 24;
                if (pop() == 0) pc = addr;
                break;
            }
            case OP_EQ: push(pop() == pop()); break;
            case OP_NEQ: push(pop() != pop()); break;
            case OP_LT: { int b=pop(); int a=pop(); push(a<b); } break;
            case OP_GT: { int b=pop(); int a=pop(); push(a>b); } break;
            case OP_LE: { int b=pop(); int a=pop(); push(a<=b); } break;
            case OP_GE: { int b=pop(); int a=pop(); push(a>=b); } break;
            
            case OP_SYSCALL: {
                int id = 0;
                id |= memory[pc++];
                id |= memory[pc++] << 8;
                id |= memory[pc++] << 16;
                id |= memory[pc++] << 24;
                vm_syscall(id);
                break;
            }
            case OP_PUSH_PTR: {
                 // Push immediate value (pointer)
                 // This is same as IMM but semantically distinct for future use
                int val = 0;
                val |= memory[pc++];
                val |= memory[pc++] << 8;
                val |= memory[pc++] << 16;
                val |= memory[pc++] << 24;
                push(val);
                break;
            }
            case OP_POP:
                // cmd_write("DEBUG: POP\n"); 
                pop(); 
                break;
            default: 
                wm_custom_paint_hook = NULL;
                return -1;
        }
    }
    wm_custom_paint_hook = NULL;
    return 0;
}
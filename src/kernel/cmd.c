#include "cmd.h"
#include "graphics.h"
#include "wm.h"
#include "io.h"
#include "rtc.h"
#include "notepad.h"
#include "calculator.h"
#include "fat32.h"
#include "cli_apps/cli_apps.h"
#include "licensewr.h"
#include <stddef.h>
#include "memory_manager.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define CMD_COLS 70
#define CMD_ROWS 25
#define LINE_HEIGHT 10
#define CHAR_WIDTH 8
#define PROMPT "> "

#define COLOR_RED 0xFFFF0000

#define TXT_BUFFER_SIZE 4096
#define TXT_VISIBLE_LINES (CMD_ROWS - 2)

#define FS_MAX_FILES 16
#define FS_MAX_FILENAME 64
#define FS_MAX_SIZE 4096

typedef struct {
    char name[FS_MAX_FILENAME];
    char content[FS_MAX_SIZE];
    int size;
    bool used;
} RamFile;

static RamFile ram_fs[FS_MAX_FILES];

static void fs_init() {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        ram_fs[i].used = false;
        ram_fs[i].size = 0;
    }
}

static RamFile* fs_find(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (ram_fs[i].used) {
            // Simple strcmp
            const char *a = ram_fs[i].name;
            const char *b = name;
            bool match = true;
            while (*a && *b) {
                if (*a != *b) { match = false; break; }
                a++; b++;
            }
            if (match && *a == *b) return &ram_fs[i];
        }
    }
    return NULL;
}

static RamFile* fs_create(const char *name) {
    if (fs_find(name)) return fs_find(name);
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!ram_fs[i].used) {
            ram_fs[i].used = true;
            int j = 0;
            while (name[j] && j < FS_MAX_FILENAME - 1) {
                ram_fs[i].name[j] = name[j];
                j++;
            }
            ram_fs[i].name[j] = 0;
            ram_fs[i].size = 0;
            return &ram_fs[i];
        }
    }
    return NULL;
}

// --- Structs ---
typedef struct {
    char c;
    uint32_t color;
} CharCell;

typedef enum {
    MODE_SHELL,
    MODE_PAGER
} CmdMode;

// --- State ---
Window win_cmd;

// Shell State
static CharCell screen_buffer[CMD_ROWS][CMD_COLS];
static int cursor_row = 0;
static int cursor_col = 0;
static uint32_t current_color = COLOR_LTGRAY;

// Pager State
static CmdMode current_mode = MODE_SHELL;
static char pager_wrapped_lines[2000][CMD_COLS + 1]; 
static int pager_total_lines = 0;
static int pager_top_line = 0;

// Boot time for uptime
int boot_time_init = 0;
int boot_year, boot_month, boot_day, boot_hour, boot_min, boot_sec;

// --- Helpers ---
static void cmd_memset(void *dest, int val, size_t len) {
    unsigned char *ptr = dest;
    while (len-- > 0) *ptr++ = val;
}

static size_t cmd_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static int cmd_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void cmd_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int cmd_atoi(const char *str) {
    int res = 0;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res * sign;
}

static void brewing(int iterations) {
    for (volatile int i = 0; i < iterations; i++) {
        __asm__ __volatile__("nop");
    }
}

static void itoa(int n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    int i = 0;
    int sign = n < 0;
    if (sign) n = -n;
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    if (sign) buf[i++] = '-';
    buf[i] = 0;
    // Reverse
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

// Manual and license pages are now in the individual command files

// --- Terminal Emulation ---

static void cmd_scroll_up() {
    for (int r = 1; r < CMD_ROWS; r++) {
        for (int c = 0; c < CMD_COLS; c++) {
            screen_buffer[r - 1][c] = screen_buffer[r][c];
        }
    }
    // Clear bottom row
    for (int c = 0; c < CMD_COLS; c++) {
        screen_buffer[CMD_ROWS - 1][c].c = ' ';
        screen_buffer[CMD_ROWS - 1][c].color = current_color;
    }
}


// Public for CLI apps to use
void cmd_putchar(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            screen_buffer[cursor_row][cursor_col].c = ' ';
        }
    } else {
        if (cursor_col >= CMD_COLS) {
            cursor_col = 0;
            cursor_row++;
        }
        
        if (cursor_row >= CMD_ROWS) {
            cmd_scroll_up();
            cursor_row = CMD_ROWS - 1;
        }

        screen_buffer[cursor_row][cursor_col].c = c;
        screen_buffer[cursor_row][cursor_col].color = current_color;
        cursor_col++;
    }

    if (cursor_row >= CMD_ROWS) {
        cmd_scroll_up();
        cursor_row = CMD_ROWS - 1;
    }
}

// Public for CLI apps to use
void cmd_write(const char *str) {
    while (*str) {
        cmd_putchar(*str++);
    }
}

// Public for CLI apps to use
void cmd_write_int(int n) {
    char buf[32];
    itoa(n, buf);
    cmd_write(buf);
}

// --- Pager Logic ---

// Public for CLI apps to use - clear the terminal screen
void cmd_screen_clear() {
    for(int r=0; r<CMD_ROWS; r++) {
        for(int c=0; c<CMD_COLS; c++) {
            screen_buffer[r][c].c = ' ';
            screen_buffer[r][c].color = COLOR_LTGRAY;
        }
    }
    cursor_row = 0;
    cursor_col = 0;
}

// Public for CLI apps to use - exit/close the terminal window
void cmd_window_exit() {
    win_cmd.visible = false;
}

// Public for CLI apps to use
void pager_wrap_content(const char **lines, int count) {
    pager_total_lines = 0;
    pager_top_line = 0;
    
    for (int i = 0; i < count; i++) {
        const char *line = lines[i];
        int len = cmd_strlen(line);
        
        if (len == 0) {
            pager_wrapped_lines[pager_total_lines][0] = 0;
            pager_total_lines++;
            continue;
        }
        
        // Intelligent Word Wrap
        int processed = 0;
        while (processed < len) {
            if (pager_total_lines >= 2000) break;

            int remaining = len - processed;
            int chunk_len = remaining;
            if (chunk_len > CMD_COLS) chunk_len = CMD_COLS;

            // If we are cutting a word, backtrack to last space
            if (chunk_len < remaining) { // Only check if we are actually wrapping
                int split_point = chunk_len;
                while (split_point > 0 && line[processed + split_point] != ' ') {
                    split_point--;
                }
                
                if (split_point > 0) {
                    chunk_len = split_point; // Cut at space
                }
                // If split_point == 0, the word is longer than the line, so forced split is okay.
            }

            // Copy chunk
            for (int k = 0; k < chunk_len; k++) {
                pager_wrapped_lines[pager_total_lines][k] = line[processed + k];
            }
            pager_wrapped_lines[pager_total_lines][chunk_len] = 0;
            
            pager_total_lines++;
            processed += chunk_len;
            
            // Skip the space we just split on
            if (processed < len && line[processed] == ' ') {
                processed++;
            }
        }
    }
}

// Public for CLI apps to use
void pager_set_mode(void) {
    current_mode = MODE_PAGER;
}

// --- Commands (now delegated to cli_apps/) ---

// Command dispatch table
typedef struct {
    const char *name;
    void (*func)(char *args);
} CommandEntry;

static const CommandEntry commands[] = {
    {"HELP", cli_cmd_help},
    {"help", cli_cmd_help},
    {"DATE", cli_cmd_date},
    {"date", cli_cmd_date},
    {"CLEAR", cli_cmd_clear},
    {"clear", cli_cmd_clear},
    {"ABOUT", cli_cmd_about},
    {"about", cli_cmd_about},
    {"MATH", cli_cmd_math},
    {"math", cli_cmd_math},
    {"MAN", cli_cmd_man},
    {"man", cli_cmd_man},
    {"LICENSE", cli_cmd_license},
    {"license", cli_cmd_license},
    {"TXTEDIT", cli_cmd_txtedit},
    {"txtedit", cli_cmd_txtedit},
    {"UPTIME", cli_cmd_uptime},
    {"uptime", cli_cmd_uptime},
    {"BEEP", cli_cmd_beep},
    {"beep", cli_cmd_beep},
    {"COWSAY", cli_cmd_cowsay},
    {"cowsay", cli_cmd_cowsay},
    {"REBOOT", cli_cmd_reboot},
    {"reboot", cli_cmd_reboot},
    {"SHUTDOWN", cli_cmd_shutdown},
    {"shutdown", cli_cmd_shutdown},
    {"IREADTHEMANUAL", cli_cmd_readtheman},
    {"ireadthemanual", cli_cmd_readtheman},
    {"BLIND", cli_cmd_blind},
    {"blind", cli_cmd_blind},
    {"EXIT", cli_cmd_exit},
    {"exit", cli_cmd_exit},
    // Filesystem Commands
    {"CD", cli_cmd_cd},
    {"cd", cli_cmd_cd},
    {"PWD", cli_cmd_pwd},
    {"pwd", cli_cmd_pwd},
    {"LS", cli_cmd_ls},
    {"ls", cli_cmd_ls},
    {"MKDIR", cli_cmd_mkdir},
    {"mkdir", cli_cmd_mkdir},
    {"RM", cli_cmd_rm},
    {"rm", cli_cmd_rm},
    {"ECHO", cli_cmd_echo},
    {"echo", cli_cmd_echo},
    {"CAT", cli_cmd_cat},
    {"cat", cli_cmd_cat},
    // Memory Management Commands
    {"MEMINFO", cli_cmd_meminfo},
    {"meminfo", cli_cmd_meminfo},
    {"MALLOC", cli_cmd_malloc},
    {"malloc", cli_cmd_malloc},
    {"FREEMEM", cli_cmd_free_mem},
    {"freemem", cli_cmd_free_mem},
    {"MEMBLOCK", cli_cmd_memblock},
    {"memblock", cli_cmd_memblock},
    {"MEMVALID", cli_cmd_memvalid},
    {"memvalid", cli_cmd_memvalid},
    {"MEMTEST", cli_cmd_memtest},
    {"memtest", cli_cmd_memtest},
    {NULL, NULL}
};

// --- Dispatcher ---

// Buffer for capturing command output
static char pipe_buffer[4096];
static int pipe_buffer_pos = 0;

static void pipe_capture_write(const char *str) {
    while (*str && pipe_buffer_pos < (int)sizeof(pipe_buffer) - 1) {
        pipe_buffer[pipe_buffer_pos++] = *str++;
    }
}

// Execute a single command
static void cmd_exec_single(char *cmd) {
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;

    // Split cmd and args
    char *args = cmd;
    while (*args && *args != ' ') args++;
    if (*args) {
        *args = 0; // Null terminate cmd
        args++; // Point to start of args
    }

    // Use command dispatch table
    for (int i = 0; commands[i].name != NULL; i++) {
        if (cmd_strcmp(cmd, commands[i].name) == 0) {
            commands[i].func(args);
            return;
        }
    }

    cmd_write("Unknown command: ");
    cmd_write(cmd);
    cmd_write("\n");
}

// Execute command with pipe support
static void cmd_exec(char *cmd) {
    // Check for pipe operator
    char *pipe_ptr = NULL;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '|' && (i == 0 || cmd[i-1] != '>' && cmd[i+1] != '>' )) {
            pipe_ptr = &cmd[i];
            break;
        }
    }
    
    if (!pipe_ptr) {
        // No pipe - execute normally
        cmd_exec_single(cmd);
        return;
    }
    
    // Split into two commands
    *pipe_ptr = 0;
    char *second_cmd = pipe_ptr + 1;
    
    // Execute first command with output captured
    pipe_buffer_pos = 0;
    cmd_memset(pipe_buffer, 0, sizeof(pipe_buffer));
    
    FAT32_FileHandle *pipe_file = fat32_open("_pipe_temp.tmp", "w");
    if (!pipe_file) {
        cmd_write("Error: Cannot create pipe\n");
        return;
    }
    
    cmd_exec_single(cmd);
    
    fat32_close(pipe_file);
    
    cmd_exec_single(second_cmd);
}


// --- Window Functions ---

static void cmd_paint(Window *win) {
    // Draw Window Content Background
    int offset_x = win->x + 4;
    int offset_y = win->y + 24;
    
    // Fill background
    draw_rect(offset_x, offset_y, win->w - 8, win->h - 28, COLOR_BLACK);
    
    int start_y = offset_y + 4;
    int start_x = offset_x + 4;

    if (current_mode == MODE_PAGER) {
        // Draw Pager Content (Wrapped)
        for (int i = 0; i < CMD_ROWS && (pager_top_line + i) < pager_total_lines; i++) {
            draw_string(start_x, start_y + (i * LINE_HEIGHT), pager_wrapped_lines[pager_top_line + i], COLOR_LTGRAY);
        }
        
        // Status Bar
        draw_string(start_x, start_y + (CMD_ROWS * LINE_HEIGHT), "-- Press Q to quit --", COLOR_WHITE);
        
    } else {
        // Draw Shell Buffer
        for (int r = 0; r < CMD_ROWS; r++) {
            for (int c = 0; c < CMD_COLS; c++) {
                char ch = screen_buffer[r][c].c;
                if (ch != 0 && ch != ' ') {
                    draw_char(start_x + (c * CHAR_WIDTH), start_y + (r * LINE_HEIGHT), ch, screen_buffer[r][c].color);
                }
            }
        }
        
        // Draw Cursor
        if (win->focused) {
            draw_rect(start_x + (cursor_col * CHAR_WIDTH), start_y + (cursor_row * LINE_HEIGHT) + 8, CHAR_WIDTH, 2, COLOR_WHITE);
        }
    }
}

static void cmd_key(Window *target, char c) {
    (void)target;
    if (current_mode == MODE_PAGER) {
        if (c == 'q' || c == 'Q') {
            current_mode = MODE_SHELL;
        } else if (c == 17) { // UP
            if (pager_top_line > 0) pager_top_line--;
        } else if (c == 18) { // DOWN
            if (pager_top_line < pager_total_lines - CMD_ROWS) pager_top_line++;
        }
        return;
    }

    // Shell Mode
    if (c == '\n') { // Enter
         char cmd_buf[CMD_COLS + 1];
         int len = 0;
         int prompt_len = cmd_strlen(PROMPT);
         
         for (int i = prompt_len; i < CMD_COLS; i++) {
             char ch = screen_buffer[cursor_row][i].c;
             if (ch == 0) break;
             cmd_buf[len++] = ch;
         }
         while (len > 0 && cmd_buf[len-1] == ' ') len--;
         cmd_buf[len] = 0;

         cmd_putchar('\n');
         
         cmd_exec(cmd_buf);
         
         cmd_write(PROMPT);
    } else if (c == 17) { // UP
        // History not implemented
    } else if (c == 18) { // DOWN
        
    } else if (c == 19) { // LEFT
        if (cursor_col > (int)cmd_strlen(PROMPT)) {
            cursor_col--;
        }
    } else if (c == 20) { // RIGHT
        if (cursor_col < CMD_COLS - 1) {
            cursor_col++;
        }
    } else if (c == '\b') { // Backspace
         if (cursor_col > (int)cmd_strlen(PROMPT)) {
             cursor_col--;
             screen_buffer[cursor_row][cursor_col].c = ' ';
         }
    } else {
        if (c >= 32 && c <= 126) {
            cmd_putchar(c);
        }
    }
}

void cmd_reset(void) {
    // Reset terminal to fresh state
    cmd_screen_clear();
    cmd_write("BrewOS Command Prompt\n");
    cmd_write(PROMPT);
}

static void create_test_files(void) {
    fat32_mkdir("Documents");
    fat32_mkdir("Projects");
    fat32_mkdir("Documents/Important");
    
    FAT32_FileHandle *fh = fat32_open("README.md", "w");
    if (fh) {
        const char *content = 
            "BREW OS 1.01 ALPHA\n"
            "==================\n\n"
            "BREWKERNEL IS NOW BREWOS!\n\n"
            "Brewkernel will from now on be deprecated as its core became too messy.\n"
            "I have built a less bloated kernel and wrote a DE above it, which is why\n"
            "it is now an OS instead of a kernel.\n\n"
            "Brew Kernel is a simple x86_64 hobbyist operating system.\n"
            "It features a DE (and WM), a FAT32 filesystem, customizable UI and much much more!\n"
            "ramdisk-like filesystem.\n\n"
            "FEATURES\n"
            "--------\n"
            "* Brew WM (Window Manager)\n"
            "* FAT32 Filesystem\n"
            "* 64-bit long mode support\n"
            "* Multiboot2 compliant\n"
            "* Text editor and file explorer\n"
            "* IDT (Interrupt Descriptor Table)\n"
            "* Ability to run on actual x86_64 hardware\n"
            "* Command-line interface (CLI)\n\n"
            "PREREQUISITES\n"
            "-------------\n"
            "To build BrewOS, you'll need the following tools installed:\n\n"
            "* x86_64 ELF Toolchain (x86_64-elf-gcc, x86_64-elf-ld)\n"
            "* NASM (Netwide Assembler)\n"
            "* xorriso (for creating bootable ISO images)\n"
            "* QEMU (optional, for testing in emulator)\n\n"
            "On macOS, install via Homebrew:\n"
            "  brew install x86_64-elf-binutils x86_64-elf-gcc nasm xorriso qemu\n\n"
            "BUILDING\n"
            "--------\n"
            "Simply run 'make' from the project root:\n\n"
            "  make\n\n"
            "This will:\n"
            "1. Download Limine v7.0.0 bootloader files (if not present)\n"
            "2. Compile all kernel C sources and assembly files\n"
            "3. Link the kernel ELF binary\n"
            "4. Generate a bootable ISO image (brewos.iso)\n\n"
            "Build output:\n"
            "* Compiled object files: build/\n"
            "* ISO root filesystem: iso_root/\n"
            "* Final ISO image: brewos.iso\n\n"
            "RUNNING\n"
            "-------\n"
            "QEMU EMULATION:\n"
            "Run in QEMU with:\n"
            "  make run\n\n"
            "Or manually:\n"
            "  qemu-system-x86_64 -m 2G -serial stdio -cdrom brewos.iso -boot d\n\n"
            "RUNNING ON REAL HARDWARE:\n"
            "WARNING: This is at YOUR OWN RISK. This software comes with ZERO warranty\n"
            "and may break your system.\n\n"
            "1. Create bootable USB using Balena Etcher to flash brewos.iso\n"
            "2. Enable legacy (BIOS) boot in your system BIOS/UEFI settings\n"
            "3. Disable Secure Boot if needed\n"
            "4. Insert USB drive and select it in boot menu during startup\n\n"
            "Tested Hardware:\n"
            "* HP EliteDesk 705 G4 DM (AMD Ryzen 5 PRO 2400G, Radeon Vega)\n"
            "* Lenovo ThinkPad A475 20KL002VMH (AMD Pro A12-8830B, Radeon R7)\n\n"
            "PROJECT STRUCTURE\n"
            "-----------------\n"
            "* src/kernel/ - Main kernel implementation\n"
            "  - boot.asm - Boot assembly code\n"
            "  - main.c - Kernel entry point\n"
            "  - *.c / *.h - Core kernel modules\n"
            "  - cli_apps/ - Command-line applications\n"
            "  - wallpaper.ppm - Default desktop wallpaper\n"
            "* build/ - Compiled object files (generated during build)\n"
            "* iso_root/ - ISO filesystem layout (generated during build)\n"
            "* limine/ - Limine bootloader files (downloaded automatically)\n"
            "* linker.ld - Linker script for x86_64 ELF\n"
            "* limine.cfg - Limine bootloader configuration\n"
            "* Makefile - Build configuration and targets\n\n"
            "LICENSE\n"
            "-------\n"
            "Copyright (C) 2024-2026 boreddevnl\n\n"
            "This program is free software: you can redistribute it and/or modify\n"
            "it under the terms of the GNU General Public License as published by\n"
            "the Free Software Foundation, either version 3 of the License, or\n"
            "(at your option) any later version.\n\n"
            "For full license details, see the LICENSE file in the repository.\n";
        fat32_write(fh, (void *)content, cmd_strlen(content));
        fat32_close(fh);
    }
    
    write_license_file();
    
    fh = fat32_open("Documents/notes.txt", "w");
    if (fh) {
        const char *content = "My Notes\n\n- First note\n- Second note\n";
        fat32_write(fh, (void *)content, 39);
        fat32_close(fh);
    }
           
    fh = fat32_open("Documents/notes.txt", "w");
    if (fh) {
        const char *content = "My Notes\n\n- First note\n- Second note\n";
        fat32_write(fh, (void *)content, 39);
        fat32_close(fh);
    }
    
    fh = fat32_open("Projects/project1.txt", "w");
    if (fh) {
        const char *content = "Project 1\n\nStatus: In Progress\n";
        fat32_write(fh, (void *)content, 32);
        fat32_close(fh);
    }
}

void cmd_init(void) {
    fs_init(); // Init RAMFS
    fat32_init(); // Init FAT32 filesystem
    create_test_files();

    win_cmd.title = "Command Prompt";
    win_cmd.x = 50;
    win_cmd.y = 50;
    win_cmd.w = (CMD_COLS * CHAR_WIDTH) + 20; 
    win_cmd.h = (CMD_ROWS * LINE_HEIGHT) + 40;
    
    win_cmd.visible = false;
    win_cmd.focused = false;
    win_cmd.z_index = 0;
    win_cmd.paint = cmd_paint;
    win_cmd.handle_key = cmd_key;
    win_cmd.handle_click = NULL;
    win_cmd.handle_right_click = NULL;
    
    cmd_reset();
    
    if (!boot_time_init) {
        rtc_get_datetime(&boot_year, &boot_month, &boot_day, &boot_hour, &boot_min, &boot_sec);
        boot_time_init = 1;
    }
}

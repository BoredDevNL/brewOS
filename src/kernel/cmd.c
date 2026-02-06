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
#include "network.h"
#include "vm.h"
#include "net_defs.h"

#define CMD_COLS 116
#define CMD_ROWS 41
#define LINE_HEIGHT 10
#define CHAR_WIDTH 8
#define PROMPT "> "

#define COLOR_RED 0xFFFF0000

#define TXT_BUFFER_SIZE 4096
#define TXT_VISIBLE_LINES (CMD_ROWS - 2)

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

// Output redirection state
static FAT32_FileHandle *redirect_file = NULL;
static char redirect_mode = 0;  // '>' for write, 'a' for append, 0 for normal output
static bool pipe_capture_mode = false;
static char pipe_buffer[8192];
static int pipe_buffer_pos = 0;
int boot_year, boot_month, boot_day, boot_hour, boot_min, boot_sec;

// Message notification state
static int msg_count = 0;

void cmd_increment_msg_count(void) {
    msg_count++;
}

void cmd_reset_msg_count(void) {
    msg_count = 0;
}

// --- Helpers ---
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

// --- History ---
#define HISTORY_MAX 16
static char cmd_history[HISTORY_MAX][CMD_COLS + 1];
static int history_head = 0;
static int history_len = 0;
static int history_pos = -1;
static char history_save_buf[CMD_COLS + 1];

static void cmd_history_add(const char *cmd) {
    if (!cmd || !*cmd) return;
    
    // Don't add if same as last command
    int last_idx = (history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
    if (history_len > 0 && cmd_strcmp(cmd, cmd_history[last_idx]) == 0) {
        return;
    }

    cmd_strcpy(cmd_history[history_head], cmd);
    history_head = (history_head + 1) % HISTORY_MAX;
    if (history_len < HISTORY_MAX) history_len++;
}

static void cmd_clear_line_content(void) {
    int prompt_len = cmd_strlen(PROMPT);
    for (int i = prompt_len; i < CMD_COLS; i++) {
        screen_buffer[cursor_row][i].c = ' ';
        screen_buffer[cursor_row][i].color = current_color;
    }
    cursor_col = prompt_len;
}

static void cmd_set_line_content(const char *str) {
    cmd_clear_line_content();
    while (*str && cursor_col < CMD_COLS) {
        screen_buffer[cursor_row][cursor_col].c = *str;
        screen_buffer[cursor_row][cursor_col].color = current_color;
        cursor_col++;
        str++;
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
    // If pipe capture mode is enabled, write to pipe buffer
    if (pipe_capture_mode) {
        if (pipe_buffer_pos < (int)sizeof(pipe_buffer) - 1) {
            pipe_buffer[pipe_buffer_pos++] = c;
        }
        return;
    }
    
    // If output is being redirected to a file, write there instead
    if (redirect_file && redirect_mode) {
        fat32_write(redirect_file, &c, 1);
        return;
    }
    
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
    // If pipe capture mode is enabled, write to pipe buffer
    if (pipe_capture_mode) {
        while (*str && pipe_buffer_pos < (int)sizeof(pipe_buffer) - 1) {
            pipe_buffer[pipe_buffer_pos++] = *str++;
        }
        return;
    }
    
    // If output is being redirected to a file, write there instead
    if (redirect_file && redirect_mode) {
        fat32_write(redirect_file, (void *)str, cmd_strlen(str));
    } else {
        // Normal output to screen
        while (*str) {
            cmd_putchar(*str++);
        }
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
    {"BREWVER", cli_cmd_brewver},
    {"brewver", cli_cmd_brewver},
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
    {"TOUCH", cli_cmd_touch},
    {"touch", cli_cmd_touch},
    {"CP", cli_cmd_cp},
    {"cp", cli_cmd_cp},
    {"MV", cli_cmd_mv},
    {"mv", cli_cmd_mv},
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
    // Network Commands
    {"NETINIT", cli_cmd_netinit},
    {"netinit", cli_cmd_netinit},
    {"NETINFO", cli_cmd_netinfo},
    {"netinfo", cli_cmd_netinfo},
    {"IPSET", cli_cmd_ipset},
    {"ipset", cli_cmd_ipset},
    {"UDPSEND", cli_cmd_udpsend},
    {"udpsend", cli_cmd_udpsend},
    {"UDPTEST", cli_cmd_udptest},
    {"udptest", cli_cmd_udptest},
    {"PING", cli_cmd_ping},
    {"ping", cli_cmd_ping},
    {"DNS", cli_cmd_dns},
    {"dns", cli_cmd_dns},
    {"HTTPGET", cli_cmd_httpget},
    {"httpget", cli_cmd_httpget},
    {"PCILIST", cli_cmd_pcilist},
    {"pcilist", cli_cmd_pcilist},
    {"MSGRC", cli_cmd_msgrc},
    {"msgrc", cli_cmd_msgrc},
    {"COMPC", cli_cmd_cc},
    {"compc", cli_cmd_cc},
    {"CC", cli_cmd_cc},
    {"cc", cli_cmd_cc},
    {NULL, NULL}
};

// --- Dispatcher ---

// Find pipe operator in command string (||)
static const char* find_pipe(const char* cmd) {
    while (*cmd) {
        if (*cmd == '|' && *(cmd + 1) == '|') {
            return cmd;
        }
        cmd++;
    }
    return NULL;
}

// Execute a single command
static void cmd_exec_single(char *cmd) {
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;

    // Check for file execution ./filename
    if (cmd[0] == '.' && cmd[1] == '/') {
        char *filename = cmd + 2;
        FAT32_FileHandle *fh = fat32_open(filename, "r");
        if (fh) {
            // It's a file, try to execute it
            // Limit executable size to 4KB for now
            uint8_t buffer[4096];
            int size = fat32_read(fh, buffer, 4096);
            fat32_close(fh);
            
            if (size > 0) {
                int res = vm_exec(buffer, size);
                if (res != 0) {
                     cmd_write("Execution failed.\n");
                }
            } else {
                cmd_write("Error: Empty file.\n");
            }
        } else {
            cmd_write("Error: Command not found or file does not exist.\n");
        }
        return;
    }

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

    // Check for executable in /Apps/
    char app_path[256];
    char *p = app_path;
    const char *prefix = "/Apps/";
    while (*prefix) *p++ = *prefix++;
    char *c = cmd;
    while (*c) *p++ = *c++;
    *p = 0;

    FAT32_FileHandle *app_fh = fat32_open(app_path, "r");
    if (app_fh) {
        uint8_t *app_buffer = (uint8_t*)kmalloc(VM_MEMORY_SIZE);
        if (app_buffer) {
            int size = fat32_read(app_fh, app_buffer, VM_MEMORY_SIZE);
            fat32_close(app_fh);
            
            if (size > 0) {
                int res = vm_exec(app_buffer, size);
                if (res != 0) {
                     cmd_write("Execution failed (invalid format or runtime error).\n");
                }
            } else {
                cmd_write("Error: Empty file.\n");
            }
            kfree(app_buffer);
        } else {
            fat32_close(app_fh);
            cmd_write("Error: Out of memory.\n");
        }
        return;
    }

    cmd_write("Unknown command: ");
    cmd_write(cmd);
    cmd_write("\n");
}

// Execute command with redirection and pipe support
static void cmd_exec(char *cmd) {
    // Check for pipe operator first
    const char* pipe_pos = find_pipe(cmd);
    if (pipe_pos) {
        // Handle piped commands
        char left_cmd[256] = {0};
        char right_cmd[256] = {0};
        
        // Extract left command (before pipe)
        size_t left_len = pipe_pos - cmd;
        if (left_len >= sizeof(left_cmd)) left_len = sizeof(left_cmd) - 1;
        for (size_t j = 0; j < left_len; j++) {
            left_cmd[j] = cmd[j];
        }
        left_cmd[left_len] = '\0';
        
        // Trim trailing spaces from left command
        while (left_len > 0 && left_cmd[left_len - 1] == ' ') {
            left_cmd[--left_len] = '\0';
        }
        
        // Extract right command (after pipe ||)
        const char* right_start = pipe_pos + 2;  // Skip both '|' characters
        while (*right_start == ' ') right_start++;
        
        size_t right_len = 0;
        while (right_start[right_len] && right_len < sizeof(right_cmd) - 1) {
            right_cmd[right_len] = right_start[right_len];
            right_len++;
        }
        right_cmd[right_len] = '\0';
        
        // Trim trailing spaces from right command
        while (right_len > 0 && right_cmd[right_len - 1] == ' ') {
            right_cmd[--right_len] = '\0';
        }
        
        // Check if right command is UDPSEND
        char right_upper[256] = {0};
        for (int i = 0; right_cmd[i] && i < 255; i++) {
            right_upper[i] = right_cmd[i] >= 'a' && right_cmd[i] <= 'z' 
                           ? right_cmd[i] - 32 
                           : right_cmd[i];
        }
        
        if (right_upper[0] == 'U' && right_upper[1] == 'D' && right_upper[2] == 'P' && 
            right_upper[3] == 'S' && right_upper[4] == 'E' && right_upper[5] == 'N' && 
            right_upper[6] == 'D' && (right_upper[7] == ' ' || right_upper[7] == '\0')) {
            
            // Parse UDPSEND arguments (IP and PORT only)
            const char* args = right_cmd + 7;
            while (*args == ' ') args++;
            
            if (!network_is_initialized()) {
                cmd_write("Error: Network not initialized. Use NETINIT first.\n");
                return;
            }
            
            // Parse IP address
            ipv4_address_t dest_ip;
            int ip_bytes[4] = {0};
            int ip_idx = 0;
            int current = 0;
            const char* p = args;
            
            // Parse IP
            while (*p && ip_idx < 4) {
                if (*p >= '0' && *p <= '9') {
                    current = current * 10 + (*p - '0');
                } else if (*p == '.' || *p == ' ') {
                    ip_bytes[ip_idx++] = current;
                    current = 0;
                    if (*p == ' ') break;
                }
                p++;
            }
            if (ip_idx < 4 && current > 0) {
                ip_bytes[ip_idx++] = current;
            }
            
            if (ip_idx < 4) {
                cmd_write("Error: Invalid IP address\n");
                return;
            }
            
            for (int k = 0; k < 4; k++) {
                dest_ip.bytes[k] = (uint8_t)ip_bytes[k];
            }
            
            // Parse port
            while (*p == ' ') p++;
            int port = 0;
            while (*p >= '0' && *p <= '9') {
                port = port * 10 + (*p - '0');
                p++;
            }
            
            if (port == 0 || port > 65535) {
                cmd_write("Error: Invalid port number\n");
                return;
            }
            
            // Initialize pipe buffer
            pipe_buffer_pos = 0;
            pipe_capture_mode = true;
            
            // Execute the left command and capture its output
            cmd_exec_single(left_cmd);
            
            // Disable pipe capture mode
            pipe_capture_mode = false;
            
            // Null-terminate the captured output
            pipe_buffer[pipe_buffer_pos] = '\0';
            
            if (pipe_buffer_pos == 0) {
                cmd_write("Error: No output to send\n");
                return;
            }
            
            // Send UDP packet(s) with captured output (chunked if necessary)
            const size_t chunk_size = 512;
            size_t offset = 0;
            int sent_bytes = 0;
            
            while (offset < (size_t)pipe_buffer_pos) {
                size_t to_send = pipe_buffer_pos - offset;
                if (to_send > chunk_size) {
                    to_send = chunk_size;
                }
                
                // Send directly from pipe buffer
                int result = udp_send_packet(&dest_ip, (uint16_t)port, 54321, 
                                            (const void*)(pipe_buffer + offset), to_send);
                if (result == 0) {
                    sent_bytes += to_send;
                }
                offset += to_send;
            }
            
            if (sent_bytes > 0) {
                cmd_write("UDP packets sent successfully (");
                cmd_write_int(sent_bytes);
                cmd_write(" bytes)\n");
            } else {
                cmd_write("Error: Failed to send UDP packets\n");
            }
            
            return;
        } else {
            cmd_write("Error: Only UDPSEND is supported after pipe operator\n");
            return;
        }
    }
    
    // Check for redirection operators (> or >>)
    char *redirect_ptr = NULL;
    char redirect_op = 0;  // '>' or 'a' for append
    char output_file[256] = {0};
    int cmd_len = 0;
    
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '>' && cmd[i+1] == '>') {
            redirect_ptr = cmd + i + 2;
            redirect_op = 'a';  // append
            cmd_len = i;
            break;
        } else if (cmd[i] == '>') {
            redirect_ptr = cmd + i + 1;
            redirect_op = '>';  // write
            cmd_len = i;
            break;
        }
    }
    
    // If redirection found, set it up
    if (redirect_ptr) {
        // Null terminate command
        cmd[cmd_len] = 0;
        
        // Parse output filename
        int i = 0;
        while (redirect_ptr[i] && (redirect_ptr[i] == ' ' || redirect_ptr[i] == '\t')) {
            i++;
        }
        
        int j = 0;
        while (redirect_ptr[i] && redirect_ptr[i] != ' ' && redirect_ptr[i] != '\t') {
            output_file[j++] = redirect_ptr[i++];
        }
        output_file[j] = 0;
        
        if (!output_file[0]) {
            cmd_write("Error: No output file specified\n");
            return;
        }
        
        // Open file for redirection
        const char *mode = (redirect_op == 'a') ? "a" : "w";
        redirect_file = fat32_open(output_file, mode);
        if (!redirect_file) {
            cmd_write("Error: Cannot open file for redirection\n");
            return;
        }
        
        redirect_mode = redirect_op;
    }
    
    // Execute the command
    cmd_exec_single(cmd);
    
    // Close redirected file if it was opened
    if (redirect_file) {
        fat32_close(redirect_file);
        redirect_file = NULL;
        redirect_mode = 0;
        cmd_write("Output redirected to: ");
        cmd_write(output_file);
        cmd_write("\n");
    }
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
         
         if (len > 0) cmd_history_add(cmd_buf);
         history_pos = -1;
         
         cmd_exec(cmd_buf);
         
         cmd_write(PROMPT);
    } else if (c == 17) { // UP
        if (history_len > 0) {
            if (history_pos == -1) {
                // Save current line
                int len = 0;
                int prompt_len = cmd_strlen(PROMPT);
                for (int i = prompt_len; i < CMD_COLS; i++) {
                    char ch = screen_buffer[cursor_row][i].c;
                    if (ch == 0) break;
                    history_save_buf[len++] = ch;
                }
                while (len > 0 && history_save_buf[len-1] == ' ') len--;
                history_save_buf[len] = 0;
                
                history_pos = (history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
            } else {
                int oldest = (history_head - history_len + HISTORY_MAX) % HISTORY_MAX;
                if (history_pos != oldest) {
                    history_pos = (history_pos - 1 + HISTORY_MAX) % HISTORY_MAX;
                }
            }
            cmd_set_line_content(cmd_history[history_pos]);
        }
    } else if (c == 18) { // DOWN
        if (history_pos != -1) {
            int newest = (history_head - 1 + HISTORY_MAX) % HISTORY_MAX;
            if (history_pos == newest) {
                history_pos = -1;
                cmd_set_line_content(history_save_buf);
            } else {
                history_pos = (history_pos + 1) % HISTORY_MAX;
                cmd_set_line_content(cmd_history[history_pos]);
            }
        }
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
    if (msg_count > 0) {
        cmd_write("You have ");
        cmd_write_int(msg_count);
        cmd_write(" new message(s) run \"msgrc\" to see your new message(s).\n");
    }
    cmd_write(PROMPT);
}

static void create_test_files(void) {
    fat32_mkdir("Documents");
    fat32_mkdir("Projects");
    fat32_mkdir("Documents/Important");
    fat32_mkdir("Apps");
    
    FAT32_FileHandle *fh = fat32_open("README.md", "w");
    if (fh) {
        const char *content = 
            "# Brew OS 1.01 Alpha\n\n"
            "## Brewkernel is now BrewOS!\n"
            "Brewkernel will from now on be deprecated as it's core became too messy. I have built a less bloated kernel and wrote a DE above it, which is why it is now an OS instead of a kernel (in my opinion).\n\n"
            "Brew Kernel is a simple x86_64 hobbyist operating system.\n"
            "It features a DE (and WM), a FAT32 filesystem, customizable UI and much much more!\n\n"
            "## Features\n"
            "- Brew WM\n"
            "- Fat 32 FS\n"
            "- 64-bit long mode support\n"
            "- Multiboot2 compliant\n"
            "- Text editor\n"
            "- IDT\n"
            "- Ability to run on actual x86_64 hardware\n"
            "- CLI\n\n"
            "## Prerequisites\n\n"
            "To build BrewOS, you'll need the following tools installed:\n\n"
            "- **x86_64 ELF Toolchain**: `x86_64-elf-gcc`, `x86_64-elf-ld`\n"
            "- **NASM**: Netwide Assembler for compiling assembly code\n"
            "- **xorriso**: For creating bootable ISO images\n"
            "- **QEMU** (optional): For testing the kernel in an emulator\n\n"
            "On macOS, you can install these using Homebrew:\n"
            "```sh\n"
            "brew install x86_64-elf-binutils x86_64-elf-gcc nasm xorriso qemu\n"
            "```\n\n"
            "## Building\n\n"
            "Simply run `make` from the project root:\n\n"
            "```sh\n"
            "make\n"
            "```\n\n"
            "This will:\n"
            "1. Compile all kernel C sources and assembly files\n"
            "2. Link the kernel ELF binary\n"
            "3. Generate a bootable ISO image (`brewos.iso`)\n\n"
            "The build output is organized as follows:\n"
            "- Compiled object files: `build/`\n"
            "- ISO root filesystem: `iso_root/`\n"
            "- Final ISO image: `brewos.iso`\n\n"
            "## Running\n\n"
            "### QEMU Emulation\n\n"
            "Run the kernel in QEMU:\n\n"
            "```sh\n"
            "make run\n"
            "```\n\n"
            "Or manually:\n"
            "```sh\n"
            "qemu-system-x86_64 -m 2G -serial stdio -cdrom brewos.iso -boot d\n"
            "```\n\n"
            "### Running on Real Hardware\n\n"
            "*Warning: This is at YOUR OWN RISK. This software comes with ZERO warranty and may break your system.*\n\n"
            "1. **Create bootable USB**: Use [Balena Etcher](https://www.balena.io/etcher/) to flash `brewos.iso` to a USB drive\n\n"
            "2. **Prepare the system**:\n"
            "   - Enable legacy (BIOS) boot in your system BIOS/UEFI settings\n"
            "   - Disable Secure Boot if needed\n\n"
            "3. **Boot**: Insert the USB drive and select it in the boot menu during startup\n\n"
            "4. **Tested Hardware**:\n"
            "   - HP EliteDesk 705 G4 DM (AMD Ryzen 5 PRO 2400G, Radeon Vega)\n"
            "   - Lenovo ThinkPad A475 20KL002VMH (AMD Pro A12-8830B, Radeon R7)\n\n"
            "## Project Structure\n\n"
            "- `src/kernel/` - Main kernel implementation\n"
            "  - `boot.asm` - Boot assembly code\n"
            "  - `main.c` - Kernel entry point\n"
            "  - `*.c / *.h` - Core kernel modules (graphics, interrupts, filesystem, etc.)\n"
            "  - `cli_apps/` - Command-line applications\n"
            "  - `wallpaper.ppm` - Default desktop wallpaper\n"
            "- `build/` - Compiled object files (generated during build)\n"
            "- `iso_root/` - ISO filesystem layout (generated during build)\n"
            "- `limine/` - Limine bootloader files (downloaded automatically)\n"
            "- `linker.ld` - Linker script for x86_64 ELF\n"
            "- `limine.cfg` - Limine bootloader configuration\n"
            "- `Makefile` - Build configuration and targets\n\n"
            "## License\n\n"
            "Copyright (C) 2024-2026 boreddevnl\n\n"
            "This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.\n\n"
            "NOTICE\n"
            "------\n\n"
            "This product includes software developed by Chris (\"boreddevnl\") as part of the BrewKernel project.\n\n"
            "Copyright (C) 2024–2026 Chris / boreddevnl (previously boreddevhq)\n\n"
            "All source files in this repository contain copyright and license\n"
            "headers that must be preserved in redistributions and derivative works.\n\n"
            "If you distribute or modify this project (in whole or in part),\n"
            "you MUST:\n\n"
            "  - Retain all copyright and license headers at the top of each file.\n"
            "  - Include this NOTICE file along with any redistributions or\n"
            "    derivative works.\n"
            "  - Provide clear attribution to the original author in documentation\n"
            "    or credits where appropriate.\n\n"
            "The above attribution requirements are informational and intended to\n"
            "ensure proper credit is given. They do not alter or supersede the\n"
            "terms of the GNU General Public License (GPL), which governs this work.\n";
        fat32_write(fh, (void *)content, cmd_strlen(content));
        fat32_close(fh);
    }
    
    fh = fat32_open("Apps/README.md", "w");
    if (fh) {
        const char *content = 
            "# All compiled C files in this directory are openable from any other directory by typing in the name of the compiled file by typing in the name of the compiled file.\n\n"            
            "The c file 'wordofgod.c' contains a C program similar to one in TempleOS, which Terry A. Davis (RIP) saw as 'words from god' telling him what to do with his kernel.\n"
            "I made this file as a tribute to him, as he also inspired me to create this project in '24. If you want to run it you simply do cc (or compc) wordofgod.c and then run ./wordofgod \n";
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
    
    fh = fat32_open("Projects/project1.txt", "w");
    if (fh) {
        const char *content = "Project 1\n\nStatus: In Progress\n";
        fat32_write(fh, (void *)content, 32);
        fat32_close(fh);
    }
   
    
    fh = fat32_open("Apps/wordofgod.c", "w");
    if (fh) {
        char *h = "int main(){int l;l=malloc(400);";
        fat32_write(fh, h, cmd_strlen(h));
        char *w1 = "poke(l+0,\"In\");poke(l+4,\"the\");poke(l+8,\"beginning\");poke(l+12,\"God\");poke(l+16,\"created\");poke(l+20,\"heaven\");poke(l+24,\"and\");poke(l+28,\"earth\");poke(l+32,\"light\");poke(l+36,\"darkness\");";
        fat32_write(fh, w1, cmd_strlen(w1));
        char *w2 = "poke(l+40,\"day\");poke(l+44,\"night\");poke(l+48,\"waters\");poke(l+52,\"firmament\");poke(l+56,\"evening\");poke(l+60,\"morning\");poke(l+64,\"land\");poke(l+68,\"seas\");poke(l+72,\"grass\");poke(l+76,\"herb\");";
        fat32_write(fh, w2, cmd_strlen(w2));
        char *w3 = "poke(l+80,\"seed\");poke(l+84,\"fruit\");poke(l+88,\"tree\");poke(l+92,\"sun\");poke(l+96,\"moon\");poke(l+100,\"stars\");poke(l+104,\"signs\");poke(l+108,\"seasons\");poke(l+112,\"days\");poke(l+116,\"years\");";
        fat32_write(fh, w3, cmd_strlen(w3));
        char *w4 = "poke(l+120,\"creature\");poke(l+124,\"life\");poke(l+128,\"fowl\");poke(l+132,\"whales\");poke(l+136,\"cattle\");poke(l+140,\"creeping\");poke(l+144,\"beast\");poke(l+148,\"man\");poke(l+152,\"image\");poke(l+156,\"likeness\");";
        fat32_write(fh, w4, cmd_strlen(w4));
        char *w5 = "poke(l+160,\"dominion\");poke(l+164,\"fish\");poke(l+168,\"air\");poke(l+172,\"every\");poke(l+176,\"green\");poke(l+180,\"meat\");poke(l+184,\"holy\");poke(l+188,\"rest\");poke(l+192,\"dust\");poke(l+196,\"breath\");";
        fat32_write(fh, w5, cmd_strlen(w5));
        char *w6 = "poke(l+200,\"soul\");poke(l+204,\"garden\");poke(l+208,\"east\");poke(l+212,\"Eden\");poke(l+216,\"ground\");poke(l+220,\"sight\");poke(l+224,\"good\");poke(l+228,\"evil\");poke(l+232,\"river\");poke(l+236,\"gold\");";
        fat32_write(fh, w6, cmd_strlen(w6));
        char *w7 = "poke(l+240,\"stone\");poke(l+244,\"woman\");poke(l+248,\"wife\");poke(l+252,\"flesh\");poke(l+256,\"bone\");poke(l+260,\"naked\");poke(l+264,\"serpent\");poke(l+268,\"subtle\");poke(l+272,\"eat\");poke(l+276,\"eyes\");";
        fat32_write(fh, w7, cmd_strlen(w7));
        char *w8 = "poke(l+280,\"wise\");poke(l+284,\"cool\");poke(l+288,\"voice\");poke(l+292,\"fear\");poke(l+296,\"hid\");poke(l+300,\"cursed\");poke(l+304,\"belly\");poke(l+308,\"enmity\");poke(l+312,\"sorrow\");poke(l+316,\"conception\");";
        fat32_write(fh, w8, cmd_strlen(w8));
        char *w9 = "poke(l+320,\"children\");poke(l+324,\"desire\");poke(l+328,\"husband\");poke(l+332,\"thorns\");poke(l+336,\"thistles\");poke(l+340,\"sweat\");poke(l+344,\"bread\");poke(l+348,\"mother\");poke(l+352,\"skin\");poke(l+356,\"coats\");";
        fat32_write(fh, w9, cmd_strlen(w9));
        char *w10 = "poke(l+360,\"cherubims\");poke(l+364,\"sword\");poke(l+368,\"gate\");poke(l+372,\"offering\");poke(l+376,\"respect\");poke(l+380,\"sin\");poke(l+384,\"door\");poke(l+388,\"blood\");poke(l+392,\"brother\");poke(l+396,\"keeper\");";
        fat32_write(fh, w10, cmd_strlen(w10));
        char *e = "int c;int r;r=rand();r=r-(r/3)*3;c=5+r;int i;i=0;while(i<c){int x;x=rand();x=x-(x/100)*100;int w;w=peek(l+x*4);print_str(w);print_str(\" \");i=i+1;}print_str(\"\\n\");}";
        fat32_write(fh, e, cmd_strlen(e));
        fat32_close(fh);
    }
}

void cmd_init(void) {
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

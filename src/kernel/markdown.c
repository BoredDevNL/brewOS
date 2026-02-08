#include "markdown.h"
#include "graphics.h"
#include "fat32.h"
#include "wm.h"
#include <stdbool.h>
#include <stddef.h>

// === Markdown Viewer State ===
Window win_markdown;

#define MD_MAX_CONTENT 16384
#define MD_MAX_LINES 256
#define MD_CHAR_WIDTH 8
#define MD_LINE_HEIGHT 16
#define MD_CONTENT_Y 40
#define MD_PADDING_X 12
#define MD_CONTENT_WIDTH 400

typedef enum {
    MD_LINE_NORMAL,
    MD_LINE_HEADING1,
    MD_LINE_HEADING2,
    MD_LINE_HEADING3,
    MD_LINE_BOLD,
    MD_LINE_ITALIC,
    MD_LINE_LIST,
    MD_LINE_BLOCKQUOTE,
    MD_LINE_CODE
} MDLineType;

typedef struct {
    char content[256];
    int length;
    MDLineType type;
    int indent_level;
} MDLine;

static MDLine lines[MD_MAX_LINES];
static int line_count = 0;
static int scroll_top = 0;
static char open_filename[256] = "";

// === Helper Functions ===

static size_t md_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static void md_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int md_strncpy(char *dest, const char *src, int n) {
    int i = 0;
    while (i < n && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
    return i;
}

static int md_strcmp(const char *s1, const char *s2) {
    (void)s1;  // Suppress unused warning
    (void)s2;  // Suppress unused warning
    return 0;
}

// Check if string starts with pattern
static bool md_starts_with(const char *str, const char *pattern) {
    (void)str;  // Suppress unused warning
    (void)pattern;  // Suppress unused warning
    return false;
}

// Parse markdown line and extract formatted text
static void md_parse_line(const char *raw_line, char *output, MDLineType *type, int *indent) {
    int i = 0;
    int out_idx = 0;
    *indent = 0;
    *type = MD_LINE_NORMAL;
    
    // Skip leading whitespace and count indentation
    while (raw_line[i] == ' ' || raw_line[i] == '\t') {
        if (raw_line[i] == '\t') *indent += 2;
        else *indent += 1;
        i++;
    }
    
    // Detect line type
    if (raw_line[i] == '#') {
        // Heading
        int hash_count = 0;
        while (raw_line[i] == '#') {
            hash_count++;
            i++;
        }
        // Skip space after hashes
        if (raw_line[i] == ' ') i++;
        
        if (hash_count == 1) *type = MD_LINE_HEADING1;
        else if (hash_count == 2) *type = MD_LINE_HEADING2;
        else if (hash_count <= 6) *type = MD_LINE_HEADING3;
    } else if (raw_line[i] == '-' || raw_line[i] == '*') {
        // Could be list or horizontal rule
        if ((raw_line[i] == '-' || raw_line[i] == '*') && (raw_line[i+1] == ' ' || raw_line[i+1] == '\t')) {
            *type = MD_LINE_LIST;
            i += 2;  // Skip '- ' or '* '
            while (raw_line[i] == ' ' || raw_line[i] == '\t') i++;  // Skip extra spaces
        }
    } else if (raw_line[i] == '>') {
        // Blockquote
        *type = MD_LINE_BLOCKQUOTE;
        i++;
        if (raw_line[i] == ' ') i++;
    } else if (raw_line[i] == '`') {
        // Code block
        *type = MD_LINE_CODE;
        i++;
    }
    
    // Parse inline formatting and copy content
    while (raw_line[i] && out_idx < 255) {
        // Handle bold **text**
        if (raw_line[i] == '*' && raw_line[i+1] == '*') {
            i += 2;
            while (raw_line[i] && !(raw_line[i] == '*' && raw_line[i+1] == '*') && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            if (raw_line[i] == '*' && raw_line[i+1] == '*') i += 2;
            continue;
        }
        
        // Handle italic *text* or _text_
        if ((raw_line[i] == '*' || raw_line[i] == '_') && out_idx > 0 && raw_line[i-1] != '\\') {
            char delim = raw_line[i];
            i++;
            while (raw_line[i] && raw_line[i] != delim && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            if (raw_line[i] == delim) i++;
            continue;
        }
        
        // Handle inline code `code`
        if (raw_line[i] == '`') {
            i++;
            while (raw_line[i] && raw_line[i] != '`' && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            if (raw_line[i] == '`') i++;
            continue;
        }
        
        // Handle links [text](url) - keep only text
        if (raw_line[i] == '[') {
            i++;
            while (raw_line[i] && raw_line[i] != ']' && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            if (raw_line[i] == ']') i++;
            // Skip (url)
            if (raw_line[i] == '(') {
                while (raw_line[i] && raw_line[i] != ')') i++;
                if (raw_line[i] == ')') i++;
            }
            continue;
        }
        
        output[out_idx++] = raw_line[i++];
    }
    
    output[out_idx] = 0;
}

// Clear all markdown lines
static void md_clear_all(void) {
    for (int i = 0; i < MD_MAX_LINES; i++) {
        lines[i].content[0] = 0;
        lines[i].length = 0;
        lines[i].type = MD_LINE_NORMAL;
        lines[i].indent_level = 0;
    }
    line_count = 0;
    scroll_top = 0;
    open_filename[0] = 0;
}

// Load and parse markdown file
void markdown_open_file(const char *filename) {
    md_clear_all();
    md_strcpy(open_filename, filename);
    
    FAT32_FileHandle *fh = fat32_open(filename, "r");
    if (!fh) {
        // File not found
        return;
    }
    
    // Read file content
    char buffer[MD_MAX_CONTENT];
    int bytes_read = fat32_read(fh, buffer, sizeof(buffer) - 1);
    fat32_close(fh);
    
    if (bytes_read <= 0) {
        return;
    }
    
    buffer[bytes_read] = 0;
    
    // Parse into markdown lines
    int line = 0;
    int col = 0;
    char raw_line[256] = "";
    
    for (int i = 0; i < bytes_read && line < MD_MAX_LINES; i++) {
        char ch = buffer[i];
        
        if (ch == '\n') {
            raw_line[col] = 0;
            
            // Parse the raw line
            char parsed_content[256];
            MDLineType type;
            int indent;
            md_parse_line(raw_line, parsed_content, &type, &indent);
            
            // Store parsed line
            md_strcpy(lines[line].content, parsed_content);
            lines[line].length = md_strlen(parsed_content);
            lines[line].type = type;
            lines[line].indent_level = indent;
            
            line++;
            col = 0;
            raw_line[0] = 0;
        } else if (col < 255) {
            raw_line[col++] = ch;
        }
    }
    
    // Handle last line if no trailing newline
    if (col > 0 && line < MD_MAX_LINES) {
        raw_line[col] = 0;
        char parsed_content[256];
        MDLineType type;
        int indent;
        md_parse_line(raw_line, parsed_content, &type, &indent);
        
        md_strcpy(lines[line].content, parsed_content);
        lines[line].length = md_strlen(parsed_content);
        lines[line].type = type;
        lines[line].indent_level = indent;
        line++;
    }
    
    line_count = line;
}

// === Paint Function ===

// Helper to draw text with emphasis (bold effect by overlaying)
static void md_draw_text_bold(int x, int y, const char *text, uint32_t color) {
    draw_string(x, y, text, color);
    draw_string(x + 1, y, text, color);
}

static void md_paint(Window *win) {
    int offset_x = win->x + 4;
    int offset_y = win->y + 24;
    int content_width = win->w - 8;
    int content_height = win->h - 28;
    
    // Draw filename bar below title
    draw_rect(offset_x, offset_y, content_width, 20, COLOR_GRAY);
    draw_string(offset_x + 4, offset_y + 4, "File", COLOR_BLACK);
    draw_string(offset_x + 50, offset_y + 4, open_filename, COLOR_BLACK);
    
    // Draw scroll buttons on top right
    int btn_x_up = offset_x + content_width - 50;
    int btn_y = offset_y + 2;
    draw_button(btn_x_up, btn_y, 20, 16, "^", false);
    draw_button(btn_x_up + 24, btn_y, 20, 16, "v", false);
    
    // Content area - starts below filename bar
    int content_start_y = offset_y + 24;
    int content_start_x = offset_x + 4;
    int usable_content_width = content_width - 8 - 20;  // Reserved space for scroll button
    int usable_content_height = content_height - 28;
    int max_display_lines = usable_content_height / MD_LINE_HEIGHT;
    
    // Draw content background
    draw_rect(offset_x, content_start_y, content_width - 20, usable_content_height, COLOR_WHITE);
    

    
    int display_line = 0;
    int i = scroll_top;
    
    while (i < line_count && display_line < max_display_lines) {
        MDLine *line = &lines[i];
        
        // Determine spacing and text properties based on heading level
        int line_height = MD_LINE_HEIGHT;
        int extra_spacing = 0;
        uint32_t text_color = COLOR_BLACK;
        bool use_bold = false;
        
        switch (line->type) {
            case MD_LINE_HEADING1:
                line_height = MD_LINE_HEIGHT * 2;  // Double height
                text_color = 0xFF004080;  // Dark blue
                use_bold = true;
                extra_spacing = 4;
                break;
            case MD_LINE_HEADING2:
                line_height = MD_LINE_HEIGHT + 6;  // 1.5x height
                text_color = 0xFF1060A0;  // Medium blue
                use_bold = true;
                extra_spacing = 2;
                break;
            case MD_LINE_HEADING3:
                line_height = MD_LINE_HEIGHT + 2;  // Slightly larger
                text_color = 0xFF2080C0;  // Light blue
                use_bold = false;
                break;
            case MD_LINE_BLOCKQUOTE:
                text_color = 0xFF808080;  // Gray
                break;
            case MD_LINE_CODE:
                text_color = 0xFF800000;  // Dark red
                break;
            default:
                text_color = COLOR_BLACK;
                break;
        }
        
        // Check if this heading will fit on the screen
        if (display_line + (line_height / MD_LINE_HEIGHT) > max_display_lines) {
            break;  // Stop rendering if heading won't fit
        }
        
        // Adjust X position based on indentation
        int x_offset = content_start_x + (line->indent_level * 4);
        int available_width = usable_content_width - (line->indent_level * 4);
        int max_chars_per_line = available_width / MD_CHAR_WIDTH;
        
        if (max_chars_per_line < 1) max_chars_per_line = 1;
        
        // Handle line wrapping (word-based)
        const char *text = line->content;
        int text_len = line->length;
        int char_idx = 0;
        int local_display_line = 0;
        int wrapped_line_count = 0;
        
        while (char_idx < text_len) {
            int line_y = content_start_y + display_line * MD_LINE_HEIGHT + (local_display_line * MD_LINE_HEIGHT);
            
            // Extract line segment - copy up to max_chars_per_line characters
            char line_segment[256];
            int segment_len = 0;
            int segment_start = char_idx;
            
            // Copy characters up to max_chars_per_line OR until end of string
            while (char_idx < text_len && segment_len < max_chars_per_line) {
                line_segment[segment_len++] = text[char_idx++];
            }
            line_segment[segment_len] = 0;
            
            // Word-based wrapping: if we didn't reach end of string, find last space
            if (char_idx < text_len && segment_len > 0) {
                // Look for the last space in the segment
                int last_space = -1;
                for (int i = segment_len - 1; i >= 0; i--) {
                    if (line_segment[i] == ' ') {
                        last_space = i;
                        break;
                    }
                }
                
                // If we found a space, break there
                if (last_space > 0) {
                    segment_len = last_space;
                    line_segment[segment_len] = 0;
                    // Backtrack char_idx to position after the space
                    char_idx = segment_start + last_space + 1;
                    // Skip any additional spaces at the start of next line
                    while (char_idx < text_len && text[char_idx] == ' ') {
                        char_idx++;
                    }
                }
            }
            
            // Draw special elements for first wrapped line of this markdown line
            if (local_display_line == 0) {
                switch (line->type) {
                    case MD_LINE_LIST:
                        // Draw bullet point
                        draw_rect(x_offset, line_y + MD_LINE_HEIGHT/2 - 1, 2, 2, COLOR_BLACK);
                        x_offset += 12;
                        // Redraw segment without leading space
                        if (segment_len > 0 && line_segment[0] == ' ') {
                            for (int j = 0; j < segment_len - 1; j++) {
                                line_segment[j] = line_segment[j + 1];
                            }
                            segment_len--;
                        }
                        break;
                    case MD_LINE_BLOCKQUOTE:
                        // Draw left border
                        draw_rect(x_offset - 4, line_y, 2, line_height, 0xFF404080);
                        break;
                    case MD_LINE_CODE:
                        // Draw background for code
                        draw_rect(x_offset - 2, line_y, (max_chars_per_line * MD_CHAR_WIDTH) + 4, line_height, 0xFFF0F0F0);
                        break;
                    default:
                        break;
                }
            }
            
            // Draw the text segment with appropriate styling
            if (segment_len > 0) {
                if (use_bold) {
                    md_draw_text_bold(x_offset, line_y + extra_spacing, line_segment, text_color);
                } else {
                    draw_string(x_offset, line_y, line_segment, text_color);
                }
            }
            
            local_display_line++;
            wrapped_line_count++;
            
            if (char_idx >= text_len) break;
        }
        
        // Move display line forward by the actual number of wrapped lines created
        // Each wrapped line uses one MD_LINE_HEIGHT worth of space
        display_line += wrapped_line_count;
        
        i++;
    }
}

// === Input Handling ===

static void md_handle_key(Window *win, char c) {
    (void)win;  // Suppress unused warning
    
    // Handle scrolling with arrow keys and W/S
    // 17 = UP arrow, 18 = DOWN arrow (from ps2 keyboard mapping)
    if (c == 'w' || c == 'W' || c == 17) {  // Page up or UP arrow
        scroll_top -= 3;
        if (scroll_top < 0) scroll_top = 0;
    } else if (c == 's' || c == 'S' || c == 18) {  // Page down or DOWN arrow  
        scroll_top += 3;
        int max_scroll = line_count - 10;
        if (scroll_top > max_scroll) scroll_top = max_scroll;
        if (scroll_top < 0) scroll_top = 0;
    }
}

static void md_handle_click(Window *win, int x, int y) {
    // x and y are relative to window origin
    int content_width = win->w - 8;
    
    // Top right up button: 4 + content_width - 50, 24 + 2, 20x16
    int btn_x_up = 4 + content_width - 50;
    int btn_y = 24 + 2;
    if (x >= btn_x_up && x < btn_x_up + 20 && y >= btn_y && y < btn_y + 16) {
        // Scroll up
        scroll_top -= 3;
        if (scroll_top < 0) scroll_top = 0;
        return;
    }
    
    // Top right down button: 4 + content_width - 50 + 24, 24 + 2, 20x16
    int btn_x_down_top = 4 + content_width - 50 + 24;
    if (x >= btn_x_down_top && x < btn_x_down_top + 20 && y >= btn_y && y < btn_y + 16) {
        // Scroll down
        scroll_top += 3;
        int max_scroll = line_count - 10;
        if (scroll_top > max_scroll) scroll_top = max_scroll;
        if (scroll_top < 0) scroll_top = 0;
        return;
    }
}

// === Initialization ===

void markdown_init(void) {
    win_markdown.title = "Markdown Viewer";
    win_markdown.x = 150;
    win_markdown.y = 180;
    win_markdown.w = 600;
    win_markdown.h = 400;
    win_markdown.visible = false;
    win_markdown.focused = false;
    win_markdown.z_index = 0;
    win_markdown.paint = md_paint;
    win_markdown.handle_key = md_handle_key;
    win_markdown.handle_click = md_handle_click;
    win_markdown.handle_right_click = NULL;
    
    md_clear_all();
}

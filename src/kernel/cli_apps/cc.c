#include "cli_apps.h"
#include "cli_utils.h"
#include "../vm.h"
#include "../fat32.h"
#include "../memory_manager.h"
#include "../cmd.h"

// --- Compiler Limits ---
#define MAX_SOURCE 8192
#define MAX_TOKENS 2048
#define MAX_VARS 64 
#define CODE_SIZE 4096
#define STR_POOL_SIZE 2048

static int compile_error = 0;

// --- Lexer ---
typedef enum {
    TOK_EOF,
    TOK_INT,      // 123, 0xFF
    TOK_STRING,   // "hello"
    TOK_ID,       // abc
    TOK_PLUS,     // +
    TOK_MINUS,    // -
    TOK_MUL,      // *
    TOK_DIV,      // /
    TOK_ASSIGN,   // =
    TOK_LPAREN,   // (
    TOK_RPAREN,   // )
    TOK_LBRACKET, // [
    TOK_RBRACKET, // ]
    TOK_LBRACE,   // {
    TOK_RBRACE,   // }
    TOK_SEMI,     // ;
    TOK_COMMA,    // ,
    TOK_EQ,       // ==
    TOK_NEQ,      // !=
    TOK_LT,       // <
    TOK_GT,       // >
    TOK_LE,       // <=
    TOK_GE,       // >=
    TOK_IF,       // if
    TOK_ELSE,     // else
    TOK_WHILE,    // while
    TOK_INT_TYPE, // int
    TOK_CHAR_TYPE,// char
    TOK_VOID_TYPE,// void
    TOK_MAIN      // main
} TokenType;

typedef struct {
    TokenType type;
    int int_val;
    char str_val[64]; // Identifier or String Content
} Token;

static char *source_ptr;
static Token tokens[MAX_TOKENS];
static int token_count = 0;

static void lex_error(const char *msg) {
    cmd_write("Compiler Error: ");
    cmd_write(msg);
    cmd_write("\n");
    compile_error = 1;
}

static void lexer(const char *source) {
    source_ptr = (char*)source;
    token_count = 0;
    compile_error = 0;

    while (*source_ptr) {
        // Skip whitespace
        while (*source_ptr == ' ' || *source_ptr == '\n' || *source_ptr == '\t' || *source_ptr == '\r') source_ptr++;
        if (!*source_ptr) break;

        // Skip comments //
        if (*source_ptr == '/' && *(source_ptr+1) == '/') {
            while (*source_ptr && *source_ptr != '\n') source_ptr++;
            continue;
        }

        Token *t = &tokens[token_count++];
        
        // Hex Literals 0x...
        if (*source_ptr == '0' && (*(source_ptr+1) == 'x' || *(source_ptr+1) == 'X')) {
            source_ptr += 2; // Skip 0x
            t->type = TOK_INT;
            t->int_val = 0;
            int has_digits = 0;
            while ((*source_ptr >= '0' && *source_ptr <= '9') || 
                   (*source_ptr >= 'a' && *source_ptr <= 'f') ||
                   (*source_ptr >= 'A' && *source_ptr <= 'F')) {
                int digit = 0;
                if (*source_ptr >= '0' && *source_ptr <= '9') digit = *source_ptr - '0';
                else if (*source_ptr >= 'a' && *source_ptr <= 'f') digit = *source_ptr - 'a' + 10;
                else if (*source_ptr >= 'A' && *source_ptr <= 'F') digit = *source_ptr - 'A' + 10;
                
                t->int_val = (t->int_val << 4) | digit;
                source_ptr++;
                has_digits = 1;
            }
            if (!has_digits) {
                lex_error("Invalid hex literal");
                return;
            }
        } 
        // Decimal Integers
        else if (*source_ptr >= '0' && *source_ptr <= '9') {
            t->type = TOK_INT;
            t->int_val = 0;
            while (*source_ptr >= '0' && *source_ptr <= '9') {
                t->int_val = t->int_val * 10 + (*source_ptr - '0');
                source_ptr++;
            }
        } 
        // Strings
        else if (*source_ptr == '"') {
            t->type = TOK_STRING;
            source_ptr++; // Skip "
            int len = 0;
            while (*source_ptr && *source_ptr != '"') {
                if (*source_ptr == '\\' && *(source_ptr+1) == 'n') {
                    if (len < 63) t->str_val[len++] = '\n';
                    source_ptr += 2;
                } else {
                    if (len < 63) t->str_val[len++] = *source_ptr;
                    source_ptr++;
                }
            }
            t->str_val[len] = 0;
            if (*source_ptr == '"') source_ptr++;
        } 
        // Character Literals
        else if (*source_ptr == '\'') {
            t->type = TOK_INT;
            source_ptr++; // Skip '
            char c = 0;
            if (*source_ptr == '\\') {
                source_ptr++;
                if (*source_ptr == 'n') c = '\n';
                else if (*source_ptr == 't') c = '\t';
                else if (*source_ptr == '0') c = '\0';
                else if (*source_ptr == '\\') c = '\\';
                else if (*source_ptr == '\'') c = '\'';
                else c = *source_ptr;
                source_ptr++;
            } else {
                c = *source_ptr;
                source_ptr++;
            }
            if (*source_ptr == '\'') source_ptr++;
            else { lex_error("Expected closing '"); return; }
            t->int_val = (int)c;
        }
        // Identifiers
        else if ((*source_ptr >= 'a' && *source_ptr <= 'z') || (*source_ptr >= 'A' && *source_ptr <= 'Z') || *source_ptr == '_') {
            int len = 0;
            while ((*source_ptr >= 'a' && *source_ptr <= 'z') || (*source_ptr >= 'A' && *source_ptr <= 'Z') || (*source_ptr >= '0' && *source_ptr <= '9') || *source_ptr == '_') {
                if (len < 63) t->str_val[len++] = *source_ptr;
                source_ptr++;
            }
            t->str_val[len] = 0;

            if (cli_strcmp(t->str_val, "if") == 0) t->type = TOK_IF;
            else if (cli_strcmp(t->str_val, "else") == 0) t->type = TOK_ELSE;
            else if (cli_strcmp(t->str_val, "while") == 0) t->type = TOK_WHILE;
            else if (cli_strcmp(t->str_val, "int") == 0) t->type = TOK_INT_TYPE;
            else if (cli_strcmp(t->str_val, "char") == 0) t->type = TOK_CHAR_TYPE; 
            else if (cli_strcmp(t->str_val, "void") == 0) t->type = TOK_VOID_TYPE;
            else if (cli_strcmp(t->str_val, "main") == 0) t->type = TOK_MAIN;
            else t->type = TOK_ID;
        } else {
            switch (*source_ptr) {
                case '+': t->type = TOK_PLUS; break;
                case '-': t->type = TOK_MINUS; break;
                case '*': t->type = TOK_MUL; break;
                case '/': t->type = TOK_DIV; break;
                case '(': t->type = TOK_LPAREN; break;
                case ')': t->type = TOK_RPAREN; break;
                case '[': t->type = TOK_LBRACKET; break;
                case ']': t->type = TOK_RBRACKET; break;
                case '{': t->type = TOK_LBRACE; break;
                case '}': t->type = TOK_RBRACE; break;
                case ';': t->type = TOK_SEMI; break;
                case ',': t->type = TOK_COMMA; break;
                case '=': 
                    if (*(source_ptr+1) == '=') { t->type = TOK_EQ; source_ptr++; } 
                    else t->type = TOK_ASSIGN; 
                    break;
                case '!':
                    if (*(source_ptr+1) == '=') { t->type = TOK_NEQ; source_ptr++; } 
                    else { lex_error("Unexpected !"); return; }
                    break;
                case '<':
                    if (*(source_ptr+1) == '=') { t->type = TOK_LE; source_ptr++; } 
                    else t->type = TOK_LT;
                    break;
                case '>':
                    if (*(source_ptr+1) == '=') { t->type = TOK_GE; source_ptr++; } 
                    else t->type = TOK_GT;
                    break;
                default: 
                    lex_error("Unknown char"); 
                    return;
            }
            source_ptr++;
        }
    }
    tokens[token_count].type = TOK_EOF;
}

// --- Builtins ---

typedef struct {
    const char *name;
    int syscall_id;
} Builtin;

static const Builtin builtins[] = {
    {"exit", SYS_EXIT},
    {"print_int", SYS_PRINT_INT},
    {"print_char", SYS_PRINT_CHAR},
    {"print_str", SYS_PRINT_STR}, // puts
    {"print", SYS_PRINT_INT},     // Alias
    {"pritc", SYS_PRINT_CHAR},    // Alias
    {"puts", SYS_PRINT_STR},      // Alias
    {"nl", SYS_NL},
    {"cls", SYS_CLS},
    {"getchar", SYS_GETCHAR},
    {"strlen", SYS_STRLEN},
    {"strcmp", SYS_STRCMP},
    {"strcpy", SYS_STRCPY},
    {"strcat", SYS_STRCAT},
    {"memset", SYS_MEMSET},
    {"memcpy", SYS_MEMCPY},
    {"malloc", SYS_MALLOC},
    {"free", SYS_FREE},
    {"rand", SYS_RAND},
    {"srand", SYS_SRAND},
    {"abs", SYS_ABS},
    {"min", SYS_MIN},
    {"max", SYS_MAX},
    {"pow", SYS_POW},
    {"sqrt", SYS_SQRT},
    {"sleep", SYS_SLEEP},
    {"fopen", SYS_FOPEN},
    {"fclose", SYS_FCLOSE},
    {"fread", SYS_FREAD},
    {"fwrite", SYS_FWRITE},
    {"fseek", SYS_FSEEK},
    {"remove", SYS_REMOVE},
    {"draw_pixel", SYS_DRAW_PIXEL},
    {"draw_rect", SYS_DRAW_RECT},
    {"draw_line", SYS_DRAW_LINE},
    {"draw_text", SYS_DRAW_TEXT},
    {"get_width", SYS_GET_WIDTH},
    {"get_height", SYS_GET_HEIGHT},
    {"get_time", SYS_GET_TIME},
    {"kb_hit", SYS_KB_HIT},
    {"mouse_x", SYS_MOUSE_X},
    {"mouse_y", SYS_MOUSE_Y},
    {"mouse_state", SYS_MOUSE_STATE},
    {"play_sound", SYS_PLAY_SOUND},
    {"atoi", SYS_ATOI},
    {"itoa", SYS_ITOA},
    {"peek", SYS_PEEK},
    {"poke", SYS_POKE},
    {"exec", SYS_EXEC},
    {"system", SYS_SYSTEM},
    {"strchr", SYS_STRCHR},
    {"memcmp", SYS_MEMCMP},
    {"isalnum", SYS_ISALNUM},
    {"isalpha", SYS_ISALPHA},
    {"isdigit", SYS_ISDIGIT},
    {"tolower", SYS_TOLOWER},
    {"toupper", SYS_TOUPPER},
    {"strncpy", SYS_STRNCPY},
    {"strncat", SYS_STRNCAT},
    {"strncmp", SYS_STRNCMP},
    {"strstr", SYS_STRSTR},
    {"strrchr", SYS_STRRCHR},
    {"memmove", SYS_MEMMOVE},
    {NULL, 0}
};

static int find_builtin(const char *name) {
    for (int i = 0; builtins[i].name != NULL; i++) {
        if (cli_strcmp(builtins[i].name, name) == 0) {
            return builtins[i].syscall_id;
        }
    }
    return -1;
}

// --- Parser & CodeGen ---

static uint8_t code[CODE_SIZE];
static int code_pos = 0;
static int cur_token = 0;

static uint8_t str_pool[STR_POOL_SIZE];
static int str_pool_pos = 0;

// Variables
typedef struct {
    char name[32];
    int addr; // Address in VM memory
} Symbol;

static Symbol symbols[MAX_VARS];
static int symbol_count = 0;

static int next_var_addr = 4096;

static int find_symbol(const char *name) {
    for (int i = 0; i < symbol_count; i++) {
        if (cli_strcmp(symbols[i].name, name) == 0) return symbols[i].addr;
    }
    return -1;
}

static int add_symbol(const char *name) {
    if (find_symbol(name) != -1) return find_symbol(name);
    if (symbol_count >= MAX_VARS) return -1;
    cli_strcpy(symbols[symbol_count].name, name);
    symbols[symbol_count].addr = next_var_addr;
    next_var_addr += 4; // 32-bit int
    return symbol_count++;
}

static void emit(uint8_t b) {
    if (code_pos < CODE_SIZE) code[code_pos++] = b;
    else {
        cmd_write("Error: Code buffer overflow\n");
        compile_error = 1;
    }
}

static void emit32(int v) {
    emit(v & 0xFF);
    emit((v >> 8) & 0xFF);
    emit((v >> 16) & 0xFF);
    emit((v >> 24) & 0xFF);
}

static int add_string(const char *str) {
    int start = str_pool_pos;
    int len = cli_strlen(str);
    if (str_pool_pos + len + 1 >= STR_POOL_SIZE) {
        cmd_write("Error: String pool overflow\n");
        compile_error = 1;
        return 0;
    }
    for(int i=0; i<len; i++) str_pool[str_pool_pos++] = str[i];
    str_pool[str_pool_pos++] = 0;
    return start;
}

static void match(TokenType t) {
    if (compile_error) return;
    if (tokens[cur_token].type == t) {
        cur_token++;
    } else {
        cmd_write("Syntax Error: Expected token ");
        // Debugging helper
        cmd_write_int(t);
        cmd_write(" got ");
        cmd_write_int(tokens[cur_token].type);
        cmd_write("\n");
        compile_error = 1;
    }
}

// Forward decls
static void expression();
static void statement();
static void block();

static void function_call(int syscall_id) {
    if (compile_error) return;
    cur_token++; // ID
    match(TOK_LPAREN);
    
    if (tokens[cur_token].type != TOK_RPAREN) {
        expression();
        while (tokens[cur_token].type == TOK_COMMA) {
            cur_token++;
            expression();
        }
    }
    match(TOK_RPAREN);
    
    emit(OP_SYSCALL);
    emit32(syscall_id);
}

static void factor() {
    if (compile_error) return;
    if (tokens[cur_token].type == TOK_INT) {
        emit(OP_IMM);
        emit32(tokens[cur_token].int_val);
        cur_token++;
    } else if (tokens[cur_token].type == TOK_STRING) {
        int offset = add_string(tokens[cur_token].str_val);
        emit(OP_PUSH_PTR); 
        emit32(offset);
        cur_token++;
    } else if (tokens[cur_token].type == TOK_ID) {
        int syscall = find_builtin(tokens[cur_token].str_val);
        if (syscall != -1 && tokens[cur_token+1].type == TOK_LPAREN) {
            function_call(syscall);
        } else {
            int addr = find_symbol(tokens[cur_token].str_val);
            if (addr == -1) {
                cmd_write("Error: Undefined variable: ");
                cmd_write(tokens[cur_token].str_val);
                cmd_write("\n");
                compile_error = 1;
            }
            emit(OP_LOAD);
            emit32(addr);
            cur_token++;
        }
    } else if (tokens[cur_token].type == TOK_LPAREN) {
        cur_token++;
        expression();
        match(TOK_RPAREN);
    } else {
        cmd_write("Syntax Error: Unexpected token in factor\n");
        compile_error = 1;
    }
}

static void term() {
    if (compile_error) return;
    factor();
    while (tokens[cur_token].type == TOK_MUL || tokens[cur_token].type == TOK_DIV) {
        TokenType op = tokens[cur_token].type;
        cur_token++;
        factor();
        if (op == TOK_MUL) emit(OP_MUL);
        else emit(OP_DIV);
    }
}

static void additive() {
    if (compile_error) return;
    term();
    while (tokens[cur_token].type == TOK_PLUS || tokens[cur_token].type == TOK_MINUS) {
        TokenType op = tokens[cur_token].type;
        cur_token++;
        term();
        if (op == TOK_PLUS) emit(OP_ADD);
        else emit(OP_SUB);
    }
}

static void relation() {
    if (compile_error) return;
    additive();
    if (tokens[cur_token].type >= TOK_EQ && tokens[cur_token].type <= TOK_GE) {
        TokenType op = tokens[cur_token].type;
        cur_token++;
        additive();
        switch (op) {
            case TOK_EQ: emit(OP_EQ); break;
            case TOK_NEQ: emit(OP_NEQ); break;
            case TOK_LT: emit(OP_LT); break;
            case TOK_GT: emit(OP_GT); break;
            case TOK_LE: emit(OP_LE); break;
            case TOK_GE: emit(OP_GE); break;
            default: break;
        }
    }
}

static void expression() {
    if (compile_error) return;
    relation();
}

static void statement() {
    if (compile_error) return;
    if (tokens[cur_token].type == TOK_INT_TYPE || tokens[cur_token].type == TOK_CHAR_TYPE) {
        // Declaration
        cur_token++;
        while (tokens[cur_token].type == TOK_MUL) cur_token++; // Skip pointers *
        
                if (tokens[cur_token].type == TOK_ID) {
                    add_symbol(tokens[cur_token].str_val);
                    cur_token++;
                    
                    // Skip array size [INT]
                    if (tokens[cur_token].type == TOK_LBRACKET) {
                        cur_token++;
                        if (tokens[cur_token].type == TOK_INT) cur_token++;
                        if (tokens[cur_token].type == TOK_RBRACKET) cur_token++;
                        else cmd_write("Error: Expected ]\n");
                    }
        
                    if (tokens[cur_token].type == TOK_ASSIGN) {                int addr = find_symbol(tokens[cur_token-1].str_val);
                cur_token++;
                expression();
                emit(OP_STORE);
                emit32(addr);
            }
            match(TOK_SEMI);
        } else {
            cmd_write("Syntax Error: Expected identifier\n");
            compile_error = 1;
        }
    } else if (tokens[cur_token].type == TOK_ID) {
        int syscall = find_builtin(tokens[cur_token].str_val);
        if (syscall != -1 && tokens[cur_token+1].type == TOK_LPAREN) {
            function_call(syscall);
            match(TOK_SEMI);
            // Drop return value (primitive)
            emit(OP_POP); 
        } else {
            // Assignment: x = expr;
            int addr = find_symbol(tokens[cur_token].str_val);
            if (addr == -1) {
                 cmd_write("Error: Undefined variable assignment: ");
                 cmd_write(tokens[cur_token].str_val);
                 cmd_write("\n");
                 compile_error = 1;
                 return;
            }
            cur_token++;
            match(TOK_ASSIGN);
            expression();
            match(TOK_SEMI);
            emit(OP_STORE);
            emit32(addr);
        }
    } else if (tokens[cur_token].type == TOK_IF) {
        cur_token++;
        match(TOK_LPAREN);
        expression();
        match(TOK_RPAREN);
        
        emit(OP_JZ);
        int jz_addr_pos = code_pos;
        emit32(0); 

        block();
        
        if (tokens[cur_token].type == TOK_ELSE) {
            emit(OP_JMP);
            int jmp_addr_pos = code_pos;
            emit32(0); 
            
            int else_start = code_pos;
            code[jz_addr_pos] = else_start & 0xFF;
            code[jz_addr_pos+1] = (else_start >> 8) & 0xFF;
            code[jz_addr_pos+2] = (else_start >> 16) & 0xFF;
            code[jz_addr_pos+3] = (else_start >> 24) & 0xFF;

            cur_token++;
            block();
            
            int end_addr = code_pos;
            code[jmp_addr_pos] = end_addr & 0xFF;
            code[jmp_addr_pos+1] = (end_addr >> 8) & 0xFF;
            code[jmp_addr_pos+2] = (end_addr >> 16) & 0xFF;
            code[jmp_addr_pos+3] = (end_addr >> 24) & 0xFF;
        } else {
            int end_addr = code_pos;
            code[jz_addr_pos] = end_addr & 0xFF;
            code[jz_addr_pos+1] = (end_addr >> 8) & 0xFF;
            code[jz_addr_pos+2] = (end_addr >> 16) & 0xFF;
            code[jz_addr_pos+3] = (end_addr >> 24) & 0xFF;
        }
    } else if (tokens[cur_token].type == TOK_WHILE) {
        int start_addr = code_pos;
        cur_token++;
        match(TOK_LPAREN);
        expression();
        match(TOK_RPAREN);

        emit(OP_JZ);
        int jz_addr_pos = code_pos;
        emit32(0); 

        block();

        emit(OP_JMP);
        emit32(start_addr);

        int end_addr = code_pos;
        code[jz_addr_pos] = end_addr & 0xFF;
        code[jz_addr_pos+1] = (end_addr >> 8) & 0xFF;
        code[jz_addr_pos+2] = (end_addr >> 16) & 0xFF;
        code[jz_addr_pos+3] = (end_addr >> 24) & 0xFF;
    } else {
        cur_token++;
    }
}

static void block() {
    if (compile_error) return;
    match(TOK_LBRACE);
    while (tokens[cur_token].type != TOK_RBRACE && tokens[cur_token].type != TOK_EOF && !compile_error) {
        statement();
    }
    match(TOK_RBRACE);
}

static void program() {
    if (tokens[cur_token].type == TOK_INT_TYPE || tokens[cur_token].type == TOK_VOID_TYPE) cur_token++;
    if (tokens[cur_token].type == TOK_MAIN) cur_token++;
    match(TOK_LPAREN);
    match(TOK_RPAREN);
    block();
    emit(OP_HALT);
}

void cli_cmd_cc(char *args) {
    if (!args || !*args) {
        cmd_write("Usage: cc <filename.c>\n");
        return;
    }

    FAT32_FileHandle *fh = fat32_open(args, "r");
    if (!fh) {
        cmd_write("Error: Cannot open source file.\n");
        return;
    }
    
    char source[MAX_SOURCE];
    int len = fat32_read(fh, source, MAX_SOURCE - 1);
    source[len] = 0;
    fat32_close(fh);

    lexer(source);
    if (compile_error) return;
    
    code_pos = 0;
    symbol_count = 0;
    cur_token = 0;
    str_pool_pos = 0;
    next_var_addr = 4096; 
    
    const char* magic = VM_MAGIC;
    for(int i=0; i<7; i++) emit(magic[i]);
    emit(1); 

    program();
    
    if (compile_error) {
        cmd_write("Compilation Failed.\n");
        return;
    }
    
    // Finalize Code
    int pool_start_addr = code_pos;
    for(int i=0; i<str_pool_pos; i++) {
        emit(str_pool[i]);
    }
    
    // Fixup OP_PUSH_PTR
    int pc = 8;
    while (pc < pool_start_addr) {
        uint8_t op = code[pc++];
        switch (op) {
            case OP_HALT: break;
            case OP_IMM: pc += 4; break;
            case OP_LOAD: pc += 4; break;
            case OP_STORE: pc += 4; break;
            case OP_LOAD8: pc += 4; break;
            case OP_STORE8: pc += 4; break;
            case OP_ADD: break;
            case OP_SUB: break;
            case OP_MUL: break;
            case OP_DIV: break;
            case OP_PRINT: break;
            case OP_PRITC: break;
            case OP_JMP: pc += 4; break;
            case OP_JZ: pc += 4; break;
            case OP_EQ: break;
            case OP_NEQ: break;
            case OP_LT: break;
            case OP_GT: break;
            case OP_LE: break;
            case OP_GE: break;
            case OP_SYSCALL: pc += 4; break;
            case OP_POP: break;
            case OP_PUSH_PTR: {
                int offset = 0;
                offset |= code[pc];
                offset |= code[pc+1] << 8;
                offset |= code[pc+2] << 16;
                offset |= code[pc+3] << 24;
                
                int abs_addr = pool_start_addr + offset;
                
                code[pc] = abs_addr & 0xFF;
                code[pc+1] = (abs_addr >> 8) & 0xFF;
                code[pc+2] = (abs_addr >> 16) & 0xFF;
                code[pc+3] = (abs_addr >> 24) & 0xFF;
                
                pc += 4;
                code[pc-5] = OP_IMM; 
                break;
            }
            default: break;
        }
    }
    
    char out_name[64];
    int i = 0;
    while(args[i] && args[i] != '.') {
        out_name[i] = args[i];
        i++;
    }
    out_name[i] = 0;

    FAT32_FileHandle *out_fh = fat32_open(out_name, "w");
    if (out_fh) {
        fat32_write(out_fh, code, code_pos);
        fat32_close(out_fh);
        cmd_write("Compilation successful. Output: ");
        cmd_write(out_name);
        cmd_write("\n");
    } else {
        cmd_write("Error: Cannot write output file.\n");
    }
}
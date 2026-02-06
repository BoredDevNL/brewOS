#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <stdbool.h>

// Simple Stack-Based VM
// Header: "BREWEXE" (7 bytes) + Version (1 byte)

#define VM_MAGIC "BREWEXE"
#define VM_STACK_SIZE 256
#define VM_MEMORY_SIZE (64 * 1024) // 64KB RAM

typedef enum {
    OP_HALT = 0,
    OP_IMM,     // Push immediate (int32)
    OP_LOAD,    // Load from memory (addr) - int32
    OP_STORE,   // Store to memory (addr) - int32
    OP_ADD,     // +
    OP_SUB,     // -
    OP_MUL,     // *
    OP_DIV,     // /
    OP_PRINT,   // Deprecated
    OP_PRITC,   // Deprecated
    OP_JMP,     // Jump (addr)
    OP_JZ,      // Jump if zero
    OP_EQ,      // ==
    OP_NEQ,     // !=
    OP_LT,      // <
    OP_GT,      // >
    OP_LE,      // <=
    OP_GE,      // >=
    OP_SYSCALL, // Call system function (id)
    OP_LOAD8,   // Load byte
    OP_STORE8,  // Store byte
    OP_PUSH_PTR, // Push pointer to data segment (relative to start of mem)
    OP_POP      // Pop and discard top of stack
} OpCode;

// Syscall IDs
typedef enum {
    SYS_EXIT = 0,
    SYS_PRINT_INT,
    SYS_PRINT_CHAR,
    SYS_PRINT_STR,
    SYS_NL,
    SYS_CLS,
    SYS_GETCHAR,
    SYS_STRLEN,
    SYS_STRCMP,
    SYS_STRCPY,
    SYS_STRCAT,
    SYS_MEMSET,
    SYS_MEMCPY,
    SYS_MALLOC,
    SYS_FREE,
    SYS_RAND,
    SYS_SRAND,
    SYS_ABS,
    SYS_MIN,
    SYS_MAX,
    SYS_POW,
    SYS_SQRT,
    SYS_SLEEP,
    SYS_FOPEN,
    SYS_FCLOSE,
    SYS_FREAD,
    SYS_FWRITE,
    SYS_FSEEK,
    SYS_REMOVE,
    SYS_DRAW_PIXEL,
    SYS_DRAW_RECT,
    SYS_DRAW_LINE,
    SYS_DRAW_TEXT,
    SYS_GET_WIDTH,
    SYS_GET_HEIGHT,
    SYS_GET_TIME,
    SYS_KB_HIT,
    SYS_MOUSE_X,
    SYS_MOUSE_Y,
    SYS_MOUSE_STATE,
    SYS_PLAY_SOUND,
    SYS_ATOI,
    SYS_ITOA,
    SYS_PEEK,
    SYS_POKE,
    SYS_EXEC,
    SYS_SYSTEM,
    SYS_STRCHR,
    SYS_MEMCMP,
    SYS_GET_DATE,
    // New Builtins
    SYS_ISALNUM,
    SYS_ISALPHA,
    SYS_ISDIGIT,
    SYS_TOLOWER,
    SYS_TOUPPER,
    SYS_STRNCPY,
    SYS_STRNCAT,
    SYS_STRNCMP,
    SYS_STRSTR,
    SYS_STRRCHR,
    SYS_MEMMOVE
} SyscallID;

int vm_exec(const uint8_t *code, int code_size);

#endif
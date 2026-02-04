#include "cli_utils.h"

void cli_cmd_math(char *args) {
    while (*args == ' ') args++;
    if (!*args) {
        cli_write("Usage: math <op> <n1> <n2>\n");
        return;
    }
    char op = *args;
    args++;
    while (*args == ' ') args++;
    
    char *end = args;
    while (*end && *end != ' ') end++;
    int saved = *end;
    *end = 0;
    int n1 = cli_atoi(args);
    if (saved) *end = saved;
    
    args = end;
    while (*args == ' ') args++;
    
    int n2 = cli_atoi(args);
    
    int res = 0;
    switch(op) {
        case '+': res = n1 + n2; break;
        case '-': res = n1 - n2; break;
        case '*': res = n1 * n2; break;
        case '/': if(n2!=0) res = n1/n2; else { cli_write("Div by zero\n"); return; } break;
        default: cli_write("Invalid op.\n"); return;
    }
    cli_write("Result: "); cli_write_int(res); cli_write("\n");
}

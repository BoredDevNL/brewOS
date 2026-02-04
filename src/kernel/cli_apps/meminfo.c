#include "cli_utils.h"
#include "../memory_manager.h"

void cli_cmd_meminfo(char *args) {
    (void)args;
    
    // Print memory statistics
    memory_print_stats();
}

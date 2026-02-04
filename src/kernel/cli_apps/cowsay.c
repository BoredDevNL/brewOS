#include "cli_utils.h"

void cli_cmd_cowsay(char *args) {
    if (!args || !*args) args = (char*)"Brew!";
    size_t len = cli_strlen(args);
    
    cli_write(" ");
    for(size_t i=0; i<len+2; i++) cli_write("_");
    cli_write("\n< "); cli_write(args); cli_write(" >\n ");
    for(size_t i=0; i<len+2; i++) cli_write("-");
    cli_write("\n");
    cli_write("        \\   ^__^\n");
    cli_write("         \\  (oo)\\_______\n");
    cli_write("            (__)\\       )\\/\\\n");
    cli_write("                ||----w |\n");
    cli_write("                ||     ||\n\n");
}

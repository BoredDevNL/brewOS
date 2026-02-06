#ifndef CLI_APPS_H
#define CLI_APPS_H

// All CLI command function declarations
void cli_cmd_help(char *args);
void cli_cmd_date(char *args);
void cli_cmd_math(char *args);
void cli_cmd_beep(char *args);
void cli_cmd_cowsay(char *args);
void cli_cmd_reboot(char *args);
void cli_cmd_shutdown(char *args);
void cli_cmd_uptime(char *args);
void cli_cmd_man(char *args);
void cli_cmd_license(char *args);
void cli_cmd_txtedit(char *args);
void cli_cmd_blind(char *args);
void cli_cmd_readtheman(char *args);
void cli_cmd_brewver(char *args);
void cli_cmd_clear(char *args);
void cli_cmd_exit(char *args);

// Filesystem commands
void cli_cmd_cd(char *args);
void cli_cmd_pwd(char *args);
void cli_cmd_ls(char *args);
void cli_cmd_mkdir(char *args);
void cli_cmd_rm(char *args);
void cli_cmd_echo(char *args);
void cli_cmd_cat(char *args);
void cli_cmd_touch(char *args);

// Memory management commands
void cli_cmd_meminfo(char *args);
void cli_cmd_malloc(char *args);
void cli_cmd_free_mem(char *args);
void cli_cmd_memblock(char *args);
void cli_cmd_memvalid(char *args);
void cli_cmd_memtest(char *args);

// Network commands
void cli_cmd_netinit(char *args);
void cli_cmd_netinfo(char *args);
void cli_cmd_ipset(char *args);
void cli_cmd_udpsend(char *args);
void cli_cmd_udptest(char *args);

// PCI commands
void cli_cmd_pcilist(char *args);

// Compiler
void cli_cmd_cc(char *args);

#endif

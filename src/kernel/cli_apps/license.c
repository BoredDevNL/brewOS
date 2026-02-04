#include "cli_utils.h"

// Forward declaration from cmd.c
extern void pager_wrap_content(const char **lines, int count);
extern void pager_set_mode(void);

const char* license_pages[] = {
    "                    GNU GENERAL PUBLIC LICENSE",
    "                       Version 3, 29 June 2007",
    "                Copyright (C) 2024-2026 boreddevnl",
    "",
    "  (License text abbreviated for build size. See https://www.gnu.org/licenses/gpl-3.0.txt)",
    "--- End of License ---"
};
const int license_num_lines = sizeof(license_pages) / sizeof(char*);

void cli_cmd_license(char *args) {
    (void)args;
    pager_wrap_content(license_pages, license_num_lines);
    pager_set_mode();
}

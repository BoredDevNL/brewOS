#include "cli_utils.h"
#include "fat32.h"
#include "cmd.h"

void cli_cmd_cd(char *args) {
    if (!args || args[0] == 0) {
        // No argument - show current directory
        char cwd[256];
        fat32_get_current_dir(cwd, sizeof(cwd));
        cli_write("Current directory: ");
        cli_write(cwd);
        cli_write("\n");
        return;
    }
    
    // Parse argument
    char path[256];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        path[i] = args[i];
        i++;
    }
    path[i] = 0;
    
    if (fat32_chdir(path)) {
        char cwd[256];
        fat32_get_current_dir(cwd, sizeof(cwd));
        cli_write("Changed to: ");
        cli_write(cwd);
        cli_write("\n");
    } else {
        cli_write("Error: Cannot change to directory: ");
        cli_write(path);
        cli_write("\n");
    }
}

void cli_cmd_pwd(char *args) {
    (void)args;
    char cwd[256];
    fat32_get_current_dir(cwd, sizeof(cwd));
    cli_write(cwd);
    cli_write("\n");
}

void cli_cmd_ls(char *args) {
    char path[256];
    
    if (!args || args[0] == 0) {
        // List current directory
        fat32_get_current_dir(path, sizeof(path));
    } else {
        // Parse argument
        int i = 0;
        while (args[i] && args[i] != ' ' && args[i] != '\t') {
            path[i] = args[i];
            i++;
        }
        path[i] = 0;
    }
    
    FAT32_FileInfo entries[256];
    int count = fat32_list_directory(path, entries, 256);
    
    if (count < 0) {
        cli_write("Error: Cannot list directory\n");
        return;
    }
    
    for (int i = 0; i < count; i++) {
        cli_write(entries[i].name);
        if (entries[i].is_directory) {
            cli_write("/");
        }
        cli_write("  (");
        cli_write_int(entries[i].size);
        cli_write(" bytes)\n");
    }
    
    cli_write("\n");
    cli_write("Total: ");
    cli_write_int(count);
    cli_write(" items\n");
}

void cli_cmd_mkdir(char *args) {
    if (!args || args[0] == 0) {
        cli_write("Usage: mkdir <dirname>\n");
        return;
    }
    
    char dirname[256];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        dirname[i] = args[i];
        i++;
    }
    dirname[i] = 0;
    
    if (fat32_mkdir(dirname)) {
        cli_write("Created directory: ");
        cli_write(dirname);
        cli_write("\n");
    } else {
        cli_write("Error: Cannot create directory\n");
    }
}

void cli_cmd_rm(char *args) {
    if (!args || args[0] == 0) {
        cli_write("Usage: rm <filename>\n");
        return;
    }
    
    char filename[256];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        filename[i] = args[i];
        i++;
    }
    filename[i] = 0;
    
    if (fat32_delete(filename)) {
        cli_write("Deleted: ");
        cli_write(filename);
        cli_write("\n");
    } else {
        cli_write("Error: Cannot delete file\n");
    }
}

void cli_cmd_echo(char *args) {
    if (!args || args[0] == 0) {
        cli_write("\n");
        return;
    }
    
    // Check for redirection operators
    char *redirect_ptr = NULL;
    char redirect_mode = 0;  // '>' for write, 'a' for append
    char output_file[256] = {0};
    char echo_text[512] = {0};
    
    // Find > or >>
    for (int i = 0; args[i]; i++) {
        if (args[i] == '>' && args[i+1] == '>') {
            redirect_ptr = args + i + 2;
            redirect_mode = 'a';  // append
            // Copy text before redirection
            for (int j = 0; j < i; j++) {
                echo_text[j] = args[j];
            }
            echo_text[i] = 0;
            break;
        } else if (args[i] == '>') {
            redirect_ptr = args + i + 1;
            redirect_mode = '>';  // write
            // Copy text before redirection
            for (int j = 0; j < i; j++) {
                echo_text[j] = args[j];
            }
            echo_text[i] = 0;
            break;
        }
    }
    
    // If no redirection, just print the text
    if (!redirect_ptr) {
        cli_write(args);
        cli_write("\n");
        return;
    }
    
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
        cli_write("Error: No output file specified\n");
        return;
    }
    
    // Open file
    const char *mode = (redirect_mode == 'a') ? "a" : "w";
    FAT32_FileHandle *fh = fat32_open(output_file, mode);
    if (!fh) {
        cli_write("Error: Cannot open file for writing\n");
        return;
    }
    
    // Write text
    int text_len = 0;
    while (echo_text[text_len]) text_len++;
    
    fat32_write(fh, echo_text, text_len);
    fat32_write(fh, "\n", 1);
    fat32_close(fh);
    
    cli_write("Wrote to: ");
    cli_write(output_file);
    cli_write("\n");
}

void cli_cmd_cat(char *args) {
    if (!args || args[0] == 0) {
        cli_write("Usage: cat <filename>\n");
        return;
    }
    
    char filename[256];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        filename[i] = args[i];
        i++;
    }
    filename[i] = 0;

    if (cli_strcmp(filename, "messages") == 0) {
        cmd_reset_msg_count();
    }    
    
    FAT32_FileHandle *fh = fat32_open(filename, "r");
    if (!fh) {
        cli_write("Error: Cannot open file\n");
        return;
    }
    
    // Read and display file
    char buffer[4096];
    int bytes_read;
    while ((bytes_read = fat32_read(fh, buffer, sizeof(buffer))) > 0) {
        for (int j = 0; j < bytes_read; j++) {
            cli_putchar(buffer[j]);
        }
    }
    
    fat32_close(fh);
}

void cli_cmd_touch(char *args) {
    if (!args || args[0] == 0) {
        cli_write("Usage: touch <filename>\n");
        return;
    }
    
    char filename[256];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t') {
        filename[i] = args[i];
        i++;
    }
    filename[i] = 0;
    
    // Check if file already exists
    if (fat32_exists(filename)) {
        cli_write("File already exists: ");
        cli_write(filename);
        cli_write("\n");
        return;
    }
    
    // Open file in write mode to create it
    FAT32_FileHandle *fh = fat32_open(filename, "w");
    if (!fh) {
        cli_write("Error: Cannot create file\n");
        return;
    }
    
    fat32_close(fh);
    
    cli_write("Created: ");
    cli_write(filename);
    cli_write("\n");
}

void cli_cmd_cp(char *args) {
    char *src = args;
    while (*src == ' ') src++;
    
    char *dest = src;
    while (*dest && *dest != ' ') dest++;
    
    if (*dest) {
        *dest = 0;
        dest++;
        while (*dest == ' ') dest++;
    }
    
    if (!*src || !*dest) {
        cli_write("Usage: cp <source> <dest>\n");
        return;
    }
    
    // Check if dest is a directory
    char final_dest[256];
    cli_strcpy(final_dest, dest);
    
    if (fat32_is_directory(dest)) {
        // Append filename from src to dest
        int len = cli_strlen(final_dest);
        if (len > 0 && final_dest[len-1] != '/') {
            final_dest[len++] = '/';
            final_dest[len] = 0;
        }
        
        // Extract filename from src
        const char *fname = src;
        const char *p = src;
        while (*p) {
            if (*p == '/') fname = p + 1;
            p++;
        }
        
        // Append
        int j = 0;
        while (fname[j]) {
            final_dest[len++] = fname[j++];
        }
        final_dest[len] = 0;
    }
    
    FAT32_FileHandle *fh_in = fat32_open(src, "r");
    if (!fh_in) {
        cli_write("Error: Cannot open source file: ");
        cli_write(src);
        cli_write("\n");
        return;
    }
    
    FAT32_FileHandle *fh_out = fat32_open(final_dest, "w");
    if (!fh_out) {
        cli_write("Error: Cannot create destination file: ");
        cli_write(final_dest);
        cli_write("\n");
        fat32_close(fh_in);
        return;
    }
    
    char buffer[4096];
    int bytes;
    while ((bytes = fat32_read(fh_in, buffer, sizeof(buffer))) > 0) {
        fat32_write(fh_out, buffer, bytes);
    }
    
    fat32_close(fh_in);
    fat32_close(fh_out);
    
    cli_write("Copied ");
    cli_write(src);
    cli_write(" to ");
    cli_write(final_dest);
    cli_write("\n");
}

void cli_cmd_mv(char *args) {
    // Parse args similar to cp
    char *src = args;
    while (*src == ' ') src++;
    
    char *dest = src;
    while (*dest && *dest != ' ') dest++;
    
    if (*dest) {
        *dest = 0;
        dest++;
        while (*dest == ' ') dest++;
    }
    
    if (!*src || !*dest) {
        cli_write("Usage: mv <source> <dest>\n");
        return;
    }
    

    char cp_args[512];
    int i = 0;
    const char *s = src;
    while (*s) cp_args[i++] = *s++;
    cp_args[i++] = ' ';
    const char *d = dest;
    while (*d) cp_args[i++] = *d++;
    cp_args[i] = 0;
    
    cli_cmd_cp(cp_args);

    fat32_delete(src);
}

#include "cli_utils.h"
#include "fat32.h"

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

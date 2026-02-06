#include "fat32.h"
#include <stdbool.h>
#include <stddef.h>

// === Memory-based FAT32 Implementation ===
// This is a simplified FAT32 for the OS kernel
// It allocates everything in RAM instead of using a real disk

#define MAX_FILES 256
#define MAX_CLUSTERS 1024
#define MAX_OPEN_HANDLES 32

// In-memory FAT table
static uint32_t fat_table[MAX_CLUSTERS];
static uint8_t cluster_data[MAX_CLUSTERS][FAT32_CLUSTER_SIZE];

// File/Directory tracking
typedef struct {
    char full_path[FAT32_MAX_PATH];
    char filename[FAT32_MAX_FILENAME];
    uint32_t start_cluster;
    uint32_t size;
    uint32_t attributes;
    bool used;
    char parent_path[FAT32_MAX_PATH];
} FileEntry;

static FileEntry files[MAX_FILES];
static uint32_t next_cluster = 3;  // Start after reserved clusters 0, 1, 2
static FAT32_FileHandle open_handles[MAX_OPEN_HANDLES];
static char current_dir[FAT32_MAX_PATH] = "/";

// === Helper Functions ===

static size_t fs_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static void fs_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int fs_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void fs_strcat(char *dest, const char *src) {
    while (*dest) dest++;
    fs_strcpy(dest, src);
}

static bool fs_ends_with(const char *str, const char *suffix) {
    int str_len = fs_strlen(str);
    int suffix_len = fs_strlen(suffix);
    if (suffix_len > str_len) return false;
    return fs_strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Extract filename from path
static void extract_filename(const char *path, char *filename) {
    int len = fs_strlen(path);
    int i = len - 1;
    
    // Skip trailing slashes
    while (i > 0 && path[i] == '/') i--;
    
    // Find last slash
    int start = i;
    while (start >= 0 && path[start] != '/') start--;
    start++;
    
    // Copy filename
    int j = 0;
    for (int k = start; k <= i; k++) {
        filename[j++] = path[k];
    }
    filename[j] = 0;
}

// Extract parent path
static void extract_parent_path(const char *path, char *parent) {
    int len = fs_strlen(path);
    int i = len - 1;
    
    // Skip trailing slashes
    while (i > 0 && path[i] == '/') i--;
    
    // Find last slash
    while (i > 0 && path[i] != '/') i--;
    
    if (i == 0) {
        parent[0] = '/';
        parent[1] = 0;
    } else {
        for (int j = 0; j < i; j++) {
            parent[j] = path[j];
        }
        parent[i] = 0;
    }
}

// Normalize path (remove .., ., etc)
void fat32_normalize_path(const char *path, char *normalized) {
    char temp[FAT32_MAX_PATH];
    int temp_len = 0;

    // Initialize with current directory or root
    if (path[0] == '/') {
        temp[0] = '/';
        temp[1] = 0;
        temp_len = 1;
    } else {
        fs_strcpy(temp, current_dir);
        temp_len = fs_strlen(temp);
    }

    int i = 0;
    while (path[i]) {
        // Skip separators
        while (path[i] == '/') i++;
        if (!path[i]) break;

        // Extract component
        char component[256];
        int j = 0;
        while (path[i] && path[i] != '/' && j < 255) {
            component[j++] = path[i++];
        }
        component[j] = 0;

        if (fs_strcmp(component, ".") == 0) {
            continue;
        } else if (fs_strcmp(component, "..") == 0) {
            // Go up one level
            if (temp_len > 1) { // Not root
                while (temp_len > 0 && temp[temp_len - 1] != '/') {
                    temp_len--;
                }
                if (temp_len > 1) { // Remove trailing slash if not root
                    temp_len--;
                }
                temp[temp_len] = 0;
            }
        } else {
            // Append component
            if (temp[temp_len - 1] != '/') {
                temp[temp_len++] = '/';
                temp[temp_len] = 0;
            }
            fs_strcat(temp, component);
            temp_len = fs_strlen(temp);
        }
    }
    
    // Remove trailing slashes (except for root)
    if (temp_len > 1 && temp[temp_len - 1] == '/') {
        temp[--temp_len] = 0;
    }
    
    fs_strcpy(normalized, temp);
}

// Find file entry by path
static FileEntry* find_file(const char *path) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].full_path, normalized) == 0) {
            return &files[i];
        }
    }
    return NULL;
}

// Find first unused file entry
static FileEntry* find_free_entry(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            return &files[i];
        }
    }
    return NULL;
}

// Find free handle
static FAT32_FileHandle* find_free_handle(void) {
    for (int i = 0; i < MAX_OPEN_HANDLES; i++) {
        if (!open_handles[i].valid) {
            return &open_handles[i];
        }
    }
    return NULL;
}

// Allocate cluster
static uint32_t allocate_cluster(void) {
    if (next_cluster >= MAX_CLUSTERS) return 0;
    uint32_t cluster = next_cluster++;
    fat_table[cluster] = 0xFFFFFFFF;  // End of chain
    return cluster;
}

// === Public API ===

void fat32_init(void) {
    // Initialize FAT table
    for (int i = 0; i < MAX_CLUSTERS; i++) {
        fat_table[i] = 0;
    }
    fat_table[0] = 0xFFFFFFF8;  // Media descriptor
    fat_table[1] = 0xFFFFFFFF;  // Bad sector marker
    
    // Create root directory entry
    FileEntry *root = find_free_entry();
    if (root) {
        root->used = true;
        root->filename[0] = 0;
        fs_strcpy(root->full_path, "/");
        root->start_cluster = 2;  // Root cluster
        root->size = 0;
        root->attributes = ATTR_DIRECTORY;
        fat_table[2] = 0xFFFFFFFF;  // Root is EOF
    }
    
    next_cluster = 3;
    current_dir[0] = '/';
    current_dir[1] = 0;
}

FAT32_FileHandle* fat32_open(const char *path, const char *mode) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    FileEntry *entry = find_file(normalized);
    
    if (mode[0] == 'r') {
        // Read mode
        if (!entry || (entry->attributes & ATTR_DIRECTORY)) {
            return NULL;  // File not found or is directory
        }
    } else if (mode[0] == 'w' || (mode[0] == 'a')) {
        // Write/append mode - create if not exists
        if (!entry) {
            entry = find_free_entry();
            if (!entry) return NULL;
            
            entry->used = true;
            fs_strcpy(entry->full_path, normalized);
            extract_filename(normalized, entry->filename);
            extract_parent_path(normalized, entry->parent_path);
            entry->start_cluster = allocate_cluster();
            if (!entry->start_cluster) return NULL;
            entry->size = 0;
            entry->attributes = 0;  // Regular file
        }
        
        if (mode[0] == 'w') {
            entry->size = 0;  // Truncate
        }
    }
    
    // Find free handle
    FAT32_FileHandle *handle = find_free_handle();
    if (!handle) return NULL;
    
    handle->valid = true;
    handle->cluster = entry->start_cluster;
    handle->position = 0;
    handle->size = entry->size;
    
    if (mode[0] == 'r') {
        handle->mode = 0;
    } else if (mode[0] == 'w') {
        handle->mode = 1;
    } else {
        handle->mode = 2;  // append
        handle->position = entry->size;
    }
    
    return handle;
}

void fat32_close(FAT32_FileHandle *handle) {
    if (handle) {
        handle->valid = false;
    }
}

int fat32_read(FAT32_FileHandle *handle, void *buffer, int size) {
    if (!handle || !handle->valid || handle->mode != 0) {
        return -1;
    }
    
    int bytes_read = 0;
    uint8_t *buf = (uint8_t *)buffer;
    
    while (bytes_read < size && handle->position < handle->size) {
        uint32_t offset_in_cluster = handle->position % FAT32_CLUSTER_SIZE;
        int to_read = size - bytes_read;
        int available = handle->size - handle->position;
        
        if (to_read > available) {
            to_read = available;
        }
        if (to_read > FAT32_CLUSTER_SIZE - offset_in_cluster) {
            to_read = FAT32_CLUSTER_SIZE - offset_in_cluster;
        }
        
        if (handle->cluster >= MAX_CLUSTERS) {
            break;
        }
        
        uint8_t *src = cluster_data[handle->cluster] + offset_in_cluster;
        for (int i = 0; i < to_read; i++) {
            buf[bytes_read + i] = src[i];
        }
        
        bytes_read += to_read;
        handle->position += to_read;
        
        // Move to next cluster if needed
        if (handle->position % FAT32_CLUSTER_SIZE == 0 && handle->position < handle->size) {
            handle->cluster = fat_table[handle->cluster];
        }
    }
    
    return bytes_read;
}

int fat32_write(FAT32_FileHandle *handle, const void *buffer, int size) {
    if (!handle || !handle->valid || (handle->mode != 1 && handle->mode != 2)) {
        return -1;
    }
    
    int bytes_written = 0;
    const uint8_t *buf = (const uint8_t *)buffer;
    uint32_t initial_cluster = handle->cluster;
    
    while (bytes_written < size) {
        uint32_t offset_in_cluster = handle->position % FAT32_CLUSTER_SIZE;
        int to_write = size - bytes_written;
        
        if (to_write > FAT32_CLUSTER_SIZE - offset_in_cluster) {
            to_write = FAT32_CLUSTER_SIZE - offset_in_cluster;
        }
        
        if (handle->cluster >= MAX_CLUSTERS) {
            break;
        }
        
        uint8_t *dest = cluster_data[handle->cluster] + offset_in_cluster;
        for (int i = 0; i < to_write; i++) {
            dest[i] = buf[bytes_written + i];
        }
        
        bytes_written += to_write;
        handle->position += to_write;
        
        if (handle->position > handle->size) {
            handle->size = handle->position;
        }
        
        // Move to next cluster if needed
        if (offset_in_cluster + to_write >= FAT32_CLUSTER_SIZE && bytes_written < size) {
            uint32_t next = allocate_cluster();
            if (!next) break;
            fat_table[handle->cluster] = next;
            handle->cluster = next;
        }
    }
    
    // Update file entry
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && files[i].start_cluster == initial_cluster) {
            files[i].size = handle->size;
            break;
        }
    }
    
    return bytes_written;
}

int fat32_seek(FAT32_FileHandle *handle, int offset, int whence) {
    if (!handle || !handle->valid) {
        return -1;
    }
    
    uint32_t new_position = handle->position;
    
    if (whence == 0) {  // SEEK_SET
        new_position = offset;
    } else if (whence == 1) {  // SEEK_CUR
        new_position += offset;
    } else if (whence == 2) {  // SEEK_END
        new_position = handle->size + offset;
    }
    
    if (new_position > handle->size) {
        new_position = handle->size;
    }
    
    handle->position = new_position;
    return new_position;
}

bool fat32_mkdir(const char *path) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    if (find_file(normalized)) {
        return false;  // Already exists
    }
    
    FileEntry *entry = find_free_entry();
    if (!entry) return false;
    
    entry->used = true;
    fs_strcpy(entry->full_path, normalized);
    extract_filename(normalized, entry->filename);
    extract_parent_path(normalized, entry->parent_path);
    entry->start_cluster = allocate_cluster();
    entry->size = 0;
    entry->attributes = ATTR_DIRECTORY;
    
    return true;
}

bool fat32_rmdir(const char *path) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    FileEntry *entry = find_file(normalized);
    if (!entry || !(entry->attributes & ATTR_DIRECTORY)) {
        return false;
    }
    
    entry->used = false;
    return true;
}

bool fat32_delete(const char *path) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    FileEntry *entry = find_file(normalized);
    if (!entry || (entry->attributes & ATTR_DIRECTORY)) {
        return false;
    }
    
    entry->used = false;
    return true;
}

bool fat32_exists(const char *path) {
    return find_file(path) != NULL;
}

bool fat32_is_directory(const char *path) {
    FileEntry *entry = find_file(path);
    return entry && (entry->attributes & ATTR_DIRECTORY);
}

int fat32_list_directory(const char *path, FAT32_FileInfo *entries, int max_entries) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    FileEntry *dir = find_file(normalized);
    if (!dir || !(dir->attributes & ATTR_DIRECTORY)) {
        return 0;  // Not a directory
    }
    
    int count = 0;
    for (int i = 0; i < MAX_FILES && count < max_entries; i++) {
        if (files[i].used && fs_strcmp(files[i].parent_path, normalized) == 0) {
            fs_strcpy(entries[count].name, files[i].filename);
            entries[count].size = files[i].size;
            entries[count].is_directory = (files[i].attributes & ATTR_DIRECTORY) != 0;
            entries[count].start_cluster = files[i].start_cluster;
            count++;
        }
    }
    
    return count;
}

bool fat32_chdir(const char *path) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    FileEntry *entry = find_file(normalized);
    if (!entry || !(entry->attributes & ATTR_DIRECTORY)) {
        return false;
    }
    
    fs_strcpy(current_dir, normalized);
    return true;
}

void fat32_get_current_dir(char *buffer, int size) {
    int len = fs_strlen(current_dir);
    if (len >= size) len = size - 1;
    
    for (int i = 0; i < len; i++) {
        buffer[i] = current_dir[i];
    }
    buffer[len] = 0;
}

#include "fat32.h"
#include "ata.h"
#include "memory_manager.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// === Disk-based FAT32 Implementation ===

#define MAX_OPEN_HANDLES 32
#define SECTOR_SIZE 512

// Globals
static uint32_t fat_start_lba;
static uint32_t data_start_lba;
static uint32_t sectors_per_cluster;
static uint32_t sectors_per_fat;
static uint32_t root_cluster;
static uint32_t bytes_per_cluster;
static uint32_t total_clusters;

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

static int fs_toupper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static int fs_strcasecmp(const char *s1, const char *s2) {
    while (*s1 && fs_toupper(*s1) == fs_toupper(*s2)) {
        s1++;
        s2++;
    }
    return (unsigned char)fs_toupper(*s1) - (unsigned char)fs_toupper(*s2);
}

static void fs_memset(void *dest, int val, size_t len) {
    uint8_t *ptr = (uint8_t*)dest;
    while (len--) *ptr++ = (uint8_t)val;
}

static void fs_memcpy(void *dest, const void *src, size_t len) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while (len--) *d++ = *s++;
}

static int fs_memcmp(const void *ptr1, const void *ptr2, size_t num) {
    const unsigned char *p1 = (const unsigned char *)ptr1;
    const unsigned char *p2 = (const unsigned char *)ptr2;
    for (size_t i = 0; i < num; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

static void fs_strcat(char *dest, const char *src) {
    while (*dest) dest++;
    fs_strcpy(dest, src);
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

// Find free handle
static FAT32_FileHandle* find_free_handle(void) {
    for (int i = 0; i < MAX_OPEN_HANDLES; i++) {
        if (!open_handles[i].valid) {
            return &open_handles[i];
        }
    }
    return NULL;
}

// === Disk Access Helpers ===

static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start_lba + (cluster - 2) * sectors_per_cluster;
}

static uint32_t get_fat_entry(uint32_t cluster) {
    uint32_t sector = fat_start_lba + (cluster * 4) / SECTOR_SIZE;
    uint32_t offset = (cluster * 4) % SECTOR_SIZE;
    
    uint8_t buf[SECTOR_SIZE];
    ata_read_sectors(sector, 1, buf);
    
    uint32_t entry = *(uint32_t*)&buf[offset];
    return entry & 0x0FFFFFFF;
}

static void set_fat_entry(uint32_t cluster, uint32_t value) {
    uint32_t sector = fat_start_lba + (cluster * 4) / SECTOR_SIZE;
    uint32_t offset = (cluster * 4) % SECTOR_SIZE;
    
    uint8_t buf[SECTOR_SIZE];
    ata_read_sectors(sector, 1, buf);
    
    *(uint32_t*)&buf[offset] = (value & 0x0FFFFFFF);
    
    // Write to FAT1
    ata_write_sectors(sector, 1, buf);
    
    // Write to FAT2 (Mirroring)
    ata_write_sectors(sector + sectors_per_fat, 1, buf);
}

static uint32_t find_free_cluster(void) {
    // Start searching from cluster 3 (2 is root usually)
    // Simple linear search
    for (uint32_t i = 3; i < total_clusters; i++) {
        if (get_fat_entry(i) == 0) {
            return i;
        }
    }
    return 0; // Disk full
}

static void clear_cluster(uint32_t cluster) {
    uint8_t zeros[SECTOR_SIZE] = {0};
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t i = 0; i < sectors_per_cluster; i++) {
        ata_write_sectors(lba + i, 1, zeros);
    }
}

// Convert 8.3 name to string
static void fat_name_to_str(const char *name, const char *ext, uint8_t nt_res, char *dest) {
    int i, j = 0;
    bool name_lower = (nt_res & 0x08) != 0;
    bool ext_lower = (nt_res & 0x10) != 0;

    for (i = 0; i < 8 && name[i] != ' '; i++) {
        char c = name[i];
        if (name_lower && c >= 'A' && c <= 'Z') c += 32;
        dest[j++] = c;
    }
    if (ext[0] != ' ') {
        dest[j++] = '.';
        for (i = 0; i < 3 && ext[i] != ' '; i++) {
            char c = ext[i];
            if (ext_lower && c >= 'A' && c <= 'Z') c += 32;
            dest[j++] = c;
        }
    }
    dest[j] = 0;
}

// Convert string to 8.3 name
static void str_to_fat_name(const char *str, char *name, char *ext, uint8_t *nt_res) {
    // Pad with spaces
    for(int i=0; i<8; i++) name[i] = ' ';
    for(int i=0; i<3; i++) ext[i] = ' ';
    
    *nt_res = 0;
    bool all_lower_name = true;
    bool all_lower_ext = true;
    bool has_ext = false;

    int i = 0;
    int j = 0;
    
    // Copy name
    while(str[i] && str[i] != '.' && j < 8) {
        char c = str[i++];
        if (c >= 'A' && c <= 'Z') all_lower_name = false;
        if (c >= 'a' && c <= 'z') c -= 32;
        name[j++] = c;
    }
    
    // Skip to extension
    while(str[i] && str[i] != '.') {
        if (str[i] >= 'A' && str[i] <= 'Z') all_lower_name = false;
        i++;
    }

    if(str[i] == '.') {
        has_ext = true;
        i++;
        j = 0;
        while(str[i] && j < 3) {
            char c = str[i++];
            if (c >= 'A' && c <= 'Z') all_lower_ext = false;
            if (c >= 'a' && c <= 'z') c -= 32;
            ext[j++] = c;
        }
    }

    if (all_lower_name) *nt_res |= 0x08;
    if (all_lower_ext && has_ext) *nt_res |= 0x10;
}

typedef struct {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t zero;
    uint16_t name3[2];
} __attribute__((packed)) FAT32_LFNEntry;

static void extract_lfn_part(FAT32_LFNEntry *lfn, char *buffer) {
    int idx = 0;
    for(int i=0; i<5; i++) {
        uint16_t c = lfn->name1[i];
        if (c == 0) { buffer[idx] = 0; return; }
        if (c < 128) buffer[idx++] = (char)c;
        else buffer[idx++] = '?';
    }
    for(int i=0; i<6; i++) {
        uint16_t c = lfn->name2[i];
        if (c == 0) { buffer[idx] = 0; return; }
        if (c < 128) buffer[idx++] = (char)c;
        else buffer[idx++] = '?';
    }
    for(int i=0; i<2; i++) {
        uint16_t c = lfn->name3[i];
        if (c == 0) { buffer[idx] = 0; return; }
        if (c < 128) buffer[idx++] = (char)c;
        else buffer[idx++] = '?';
    }
    buffer[idx] = 0;
}

// Calculate checksum for LFN
static uint8_t lfn_checksum(const uint8_t *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i];
    }
    return sum;
}

// Check if a character is valid for a strict 8.3 Short File Name
static bool is_valid_sfn_char(char c) {
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    // Standard allowed special chars in SFN
    if (c == ' ' || c == '$' || c == '%' || c == '-' || c == '_' || c == '@' || 
        c == '~' || c == '`' || c == '!' || c == '(' || c == ')' || c == '{' || 
        c == '}' || c == '^' || c == '#' || c == '&') return true;
    return false;
}

// Determine if a filename requires LFN entries
static bool needs_lfn(const char *name) {
    int len = fs_strlen(name);
    if (len > 12) return true; // Exceeds 8.3 length
    
    int dot_pos = -1;
    for (int i = 0; i < len; i++) {
        if (name[i] == '.') {
            if (dot_pos != -1) return true; // More than one dot
            dot_pos = i;
        } else {
            if (!is_valid_sfn_char(name[i])) return true; // Invalid char (including lowercase)
        }
    }
    
    if (dot_pos == -1) {
        if (len > 8) return true;
    } else {
        if (dot_pos > 8) return true; // Name part > 8
        if (len - dot_pos - 1 > 3) return true; // Ext part > 3
    }
    
    return false;
}

// Check if a specific SFN exists in the directory (to avoid collisions)
static bool sfn_exists(uint32_t dir_cluster, const char *sfn_name, const char *sfn_ext) {
    uint32_t current_cluster = dir_cluster;
    uint8_t *buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!buf) return false;

    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(current_cluster);
        ata_read_sectors(lba, sectors_per_cluster, buf);
        
        int entries_per_cluster = bytes_per_cluster / sizeof(FAT32_DirEntry);
        FAT32_DirEntry *entries = (FAT32_DirEntry*)buf;
        
        for (int i = 0; i < entries_per_cluster; i++) {
            if (entries[i].filename[0] == 0) { kfree(buf); return false; }
            if (entries[i].filename[0] == 0xE5) continue;
            if (entries[i].attributes == 0x0F) continue;

            if (fs_memcmp(entries[i].filename, sfn_name, 8) == 0 &&
                fs_memcmp(entries[i].extension, sfn_ext, 3) == 0) {
                kfree(buf);
                return true;
            }
        }
        current_cluster = get_fat_entry(current_cluster);
    }
    kfree(buf);
    return false;
}

// Generate a unique SFN (e.g., FILE~1, FILE~2)
static void generate_unique_sfn(uint32_t dir_cluster, const char *long_name, char *out_name, char *out_ext) {
    // 1. Create Basis: Uppercase, skip dots/spaces, max 6 chars
    char basis[7];
    int bi = 0;
    for (int i = 0; long_name[i] && bi < 6; i++) {
        char c = long_name[i];
        if (c == '.') break; // Stop at extension
        if (c != ' ') {
            if (c >= 'a' && c <= 'z') c -= 32;
            basis[bi++] = c;
        }
    }
    basis[bi] = 0;

    // 2. Extract Extension: Uppercase, max 3 chars
    char ext[4] = "   ";
    const char *dot = long_name;
    while (*dot && *dot != '.') dot++;
    if (*dot == '.') {
        dot++;
        int ei = 0;
        while (dot[ei] && ei < 3) {
            char c = dot[ei];
            if (c >= 'a' && c <= 'z') c -= 32;
            ext[ei] = c;
            ei++;
        }
    }

    // 3. Try numeric tails ~1 to ~9 (and beyond if needed, simplified to ~9 for now)
    for (int i = 1; i <= 9; i++) {
        fs_memset(out_name, ' ', 8);
        fs_memset(out_ext, ' ', 3);
        
        // Copy basis
        for(int k=0; k<bi; k++) out_name[k] = basis[k];
        
        // Append ~N
        out_name[6] = '~';
        out_name[7] = i + '0';
        
        // Copy extension
        for(int k=0; k<3; k++) out_ext[k] = ext[k];

        if (!sfn_exists(dir_cluster, out_name, out_ext)) {
            return; // Found unique
        }
    }
    
    // Fallback if 1-9 taken (unlikely for this OS usage): overwrite ~1
    // In a full OS, we'd try ~10, ~11, etc.
}

// Find file in directory cluster chain
static bool find_in_directory(uint32_t dir_cluster, const char *name, FAT32_DirEntry *result_entry, uint32_t *entry_sector, uint32_t *entry_offset) {
    uint32_t current_cluster = dir_cluster;
    uint8_t *buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!buf) return false;
    
    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(current_cluster);
        ata_read_sectors(lba, sectors_per_cluster, buf);
        
        int entries_per_cluster = bytes_per_cluster / sizeof(FAT32_DirEntry);
        FAT32_DirEntry *entries = (FAT32_DirEntry*)buf;
        
        char lfn_name[256];
        fs_memset(lfn_name, 0, 256);
        bool has_lfn = false;

        for (int i = 0; i < entries_per_cluster; i++) {
            if (entries[i].filename[0] == 0) { kfree(buf); return false; } // End of dir
            if (entries[i].filename[0] == 0xE5) { has_lfn = false; continue; } // Deleted
            
            if (entries[i].attributes == 0x0F) {
                FAT32_LFNEntry *lfn = (FAT32_LFNEntry*)&entries[i];
                if (lfn->order & 0x40) {
                    fs_memset(lfn_name, 0, 256);
                    has_lfn = true;
                }
                if (has_lfn) {
                    int order = lfn->order & 0x3F;
                    char part[14];
                    extract_lfn_part(lfn, part);
                    int pos = (order - 1) * 13;
                    if (pos < 255) {
                        int part_len = fs_strlen(part);
                        for(int k=0; k<part_len && (pos+k)<255; k++) {
                            lfn_name[pos+k] = part[k];
                        }
                    }
                }
                continue; 
            }
            
            char entry_name[256];
            if (has_lfn) {
                fs_strcpy(entry_name, lfn_name);
                has_lfn = false;
            } else {
                fat_name_to_str((const char*)entries[i].filename, (const char*)entries[i].extension, entries[i].reserved, entry_name);
            }
            
            // Simple case-insensitive match could be added here
            if (fs_strcasecmp(entry_name, name) == 0) {
                *result_entry = entries[i];
                if (entry_sector) *entry_sector = lba + (i * sizeof(FAT32_DirEntry)) / SECTOR_SIZE;
                if (entry_offset) *entry_offset = (i * sizeof(FAT32_DirEntry)) % SECTOR_SIZE;
                kfree(buf);
                return true;
            }
        }
        current_cluster = get_fat_entry(current_cluster);
    }
    kfree(buf);
    return false;
}

// Resolve path to directory entry
static bool resolve_path(const char *path, FAT32_DirEntry *entry) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    uint32_t current_cluster = root_cluster;
    
    // Handle root
    if (fs_strcmp(normalized, "/") == 0) {
        entry->attributes = ATTR_DIRECTORY;
        entry->start_cluster_low = root_cluster & 0xFFFF;
        entry->start_cluster_high = root_cluster >> 16;
        return true;
    }
    
    char *p = normalized;
    if (*p == '/') p++;
    
    while (*p) {
        char component[256];
        int i = 0;
        while (*p && *p != '/' && i < 255) component[i++] = *p++;        
        component[i] = 0;
        if (*p == '/') p++;
        
        if (!find_in_directory(current_cluster, component, entry, NULL, NULL)) {
            return false;
        }
        
        if (*p) {
            if (!(entry->attributes & ATTR_DIRECTORY)) return false;
            current_cluster = (entry->start_cluster_high << 16) | entry->start_cluster_low;
        }
    }
    return true;
}

// Create a new entry in a directory
static bool create_entry(uint32_t dir_cluster, const char *name, uint8_t attributes, uint32_t *created_cluster) {
    FAT32_DirEntry new_entry;
    // Prepare entry
    fs_memset(&new_entry, 0, sizeof(FAT32_DirEntry));
    new_entry.attributes = attributes;

    bool need_lfn = needs_lfn(name);
    int name_len = fs_strlen(name);

    if (need_lfn) {
        // Generate unique SFN (e.g. FILE~1)
        char sfn_name[8], sfn_ext[3];
        generate_unique_sfn(dir_cluster, name, sfn_name, sfn_ext);
        
        fs_memcpy(new_entry.filename, sfn_name, 8);
        fs_memcpy(new_entry.extension, sfn_ext, 3);
        new_entry.reserved = 0; // No NT case flags for SFN when LFN exists
    } else {
        // Fits in 8.3, use standard conversion
        str_to_fat_name(name, (char*)new_entry.filename, (char*)new_entry.extension, &new_entry.reserved);
    }
    
    // Allocate a cluster for the file content (if not empty file)
    // For now, we start with 0 size and 0 cluster
    new_entry.start_cluster_high = 0;
    new_entry.start_cluster_low = 0;
    new_entry.file_size = 0;
    
    if (attributes & ATTR_DIRECTORY) {
        uint32_t clus = find_free_cluster();
        if (clus == 0) return false;
        set_fat_entry(clus, 0x0FFFFFFF);
        clear_cluster(clus);
        new_entry.start_cluster_high = (clus >> 16);
        new_entry.start_cluster_low = (clus & 0xFFFF);
        if (created_cluster) *created_cluster = clus;
    }

    // Calculate LFN entries needed
    int lfn_count = 0;
    if (need_lfn) {
        lfn_count = (name_len + 12) / 13;
    }
    int entries_needed = 1 + lfn_count;

    // Find free slot in directory
    uint32_t current_cluster = dir_cluster;
    uint8_t *buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!buf) return false;
    
    while (true) {
        uint32_t lba = cluster_to_lba(current_cluster);
        ata_read_sectors(lba, sectors_per_cluster, buf);
        
        FAT32_DirEntry *entries = (FAT32_DirEntry*)buf;
        int entries_per_cluster = bytes_per_cluster / sizeof(FAT32_DirEntry);
        
        for (int i = 0; i < entries_per_cluster; i++) {
            // Check if we have 'entries_needed' contiguous free slots
            bool found = true;
            if (i + entries_needed > entries_per_cluster) {
                found = false; // Split across clusters not implemented for simplicity
            } else {
                for (int k = 0; k < entries_needed; k++) {
                    if (entries[i+k].filename[0] != 0 && entries[i+k].filename[0] != 0xE5) {
                        found = false;
                        break;
                    }
                }
            }

            if (found) {
                // Write LFN entries first (in reverse order)
                if (need_lfn) {
                    uint8_t checksum = lfn_checksum(new_entry.filename);
                    for (int k = 0; k < lfn_count; k++) {
                        FAT32_LFNEntry *lfn = (FAT32_LFNEntry*)&entries[i + lfn_count - 1 - k];
                        fs_memset(lfn, 0, sizeof(FAT32_LFNEntry));
                        
                        lfn->order = (k + 1) | (k == lfn_count - 1 ? 0x40 : 0);
                        lfn->attr = 0x0F;
                        lfn->type = 0;
                        lfn->checksum = checksum;
                        
                        int char_idx = k * 13;
                        // Name 1 (5 chars)
                        for(int m=0; m<5; m++) {
                            if (char_idx < name_len) lfn->name1[m] = (uint8_t)name[char_idx++];
                            else if (char_idx == name_len) { lfn->name1[m] = 0; char_idx++; }
                            else lfn->name1[m] = 0xFFFF;
                        }
                        // Name 2 (6 chars)
                        for(int m=0; m<6; m++) {
                            if (char_idx < name_len) lfn->name2[m] = (uint8_t)name[char_idx++];
                            else if (char_idx == name_len) { lfn->name2[m] = 0; char_idx++; }
                            else lfn->name2[m] = 0xFFFF;
                        }
                        // Name 3 (2 chars)
                        for(int m=0; m<2; m++) {
                            if (char_idx < name_len) lfn->name3[m] = (uint8_t)name[char_idx++];
                            else if (char_idx == name_len) { lfn->name3[m] = 0; char_idx++; }
                            else lfn->name3[m] = 0xFFFF;
                        }
                    }
                }
                entries[i + lfn_count] = new_entry;
                ata_write_sectors(lba, sectors_per_cluster, buf);
                kfree(buf);
                return true;
            }
        }
        
        uint32_t next = get_fat_entry(current_cluster);
        if (next >= 0x0FFFFFF8) {
            // Extend directory
            uint32_t new_clus = find_free_cluster();
            if (new_clus == 0) { kfree(buf); return false; }
            set_fat_entry(current_cluster, new_clus);
            set_fat_entry(new_clus, 0x0FFFFFFF);
            clear_cluster(new_clus);
            current_cluster = new_clus;
        } else {
            current_cluster = next;
        }
    }
}

// === Public API ===

void fat32_init(void) {
    if (!ata_init()) {
        // Fallback or panic if no disk found
        return;
    }
    
    uint8_t buf[SECTOR_SIZE];
    ata_read_sectors(0, 1, buf);
    
    FAT32_BootSector *bpb = (FAT32_BootSector*)buf;
    
    // Basic BPB parsing
    fat_start_lba = bpb->reserved_sectors;
    sectors_per_cluster = bpb->sectors_per_cluster;
    sectors_per_fat = bpb->sectors_per_fat_32;
    root_cluster = bpb->root_cluster;
    data_start_lba = fat_start_lba + (bpb->num_fats * bpb->sectors_per_fat_32);
    bytes_per_cluster = sectors_per_cluster * SECTOR_SIZE;
    total_clusters = bpb->total_sectors_32 / sectors_per_cluster;
}

FAT32_FileHandle* fat32_open(const char *path, const char *mode) {
    FAT32_DirEntry entry;
    uint32_t dir_sector = 0, dir_offset = 0;
    
    // We need to find the entry AND its location on disk
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    // Split path into parent and filename
    char parent_path[FAT32_MAX_PATH];
    char filename[256];
    int len = fs_strlen(normalized);
    int split = len;
    while(split > 0 && normalized[split] != '/') split--;
    
    if (split == 0) {
        fs_strcpy(parent_path, "/");
        fs_strcpy(filename, normalized + (normalized[0] == '/' ? 1 : 0));
    } else {
        for(int i=0; i<split; i++) parent_path[i] = normalized[i];
        parent_path[split] = 0;
        fs_strcpy(filename, normalized + split + 1);
    }
    if (fs_strlen(parent_path) == 0) fs_strcpy(parent_path, "/");

    // Resolve parent
    FAT32_DirEntry parent;
    if (!resolve_path(parent_path, &parent)) return NULL;
    uint32_t parent_cluster = (parent.start_cluster_high << 16) | parent.start_cluster_low;
    if (parent_cluster == 0) parent_cluster = root_cluster;

    bool exists = find_in_directory(parent_cluster, filename, &entry, &dir_sector, &dir_offset);
    
    if (mode[0] == 'r' && (!exists || (entry.attributes & ATTR_DIRECTORY))) {
        return NULL;
    }
    
    if (mode[0] == 'w' && !exists) {
        if (!create_entry(parent_cluster, filename, 0, NULL)) return NULL;
        // Find it again to get location
        if (!find_in_directory(parent_cluster, filename, &entry, &dir_sector, &dir_offset)) return NULL;
        exists = true;
    }
    
    // Find free handle
    FAT32_FileHandle *handle = find_free_handle();
    if (!handle) return NULL;
    
    uint32_t start_cluster = (entry.start_cluster_high << 16) | entry.start_cluster_low;
    
    handle->valid = true;
    handle->cluster = start_cluster;
    handle->start_cluster = start_cluster;
    handle->position = 0;
    handle->size = entry.file_size;
    handle->dir_sector = dir_sector;
    handle->dir_offset = dir_offset;
    
    if (mode[0] == 'r') {
        handle->mode = 0;
    } else if (mode[0] == 'w') {
        handle->mode = 1;
        handle->size = 0; // Truncate logic would go here
        // Note: We should update the size on disk to 0 here, but we do it on write/close
    } else {
        handle->mode = 2;  // append
        handle->position = entry.file_size;
        // Fast forward cluster
        uint32_t pos = 0;
        while (pos + bytes_per_cluster <= handle->position) {
            uint32_t next = get_fat_entry(handle->cluster);
            if (next >= 0x0FFFFFF8) break;
            handle->cluster = next;
            pos += bytes_per_cluster;
        }
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
        uint32_t offset_in_cluster = handle->position % bytes_per_cluster;
        int to_read = size - bytes_read;
        int available = handle->size - handle->position;
        
        if (to_read > available) {
            to_read = available;
        }
        if ((uint32_t)to_read > bytes_per_cluster - offset_in_cluster) {
            to_read = bytes_per_cluster - offset_in_cluster;
        }
        
        // Read whole cluster to temp buffer (inefficient but simple)
        uint8_t *cluster_buf = (uint8_t*)kmalloc(bytes_per_cluster);
        if (!cluster_buf) return -1;
        ata_read_sectors(cluster_to_lba(handle->cluster), sectors_per_cluster, cluster_buf);
        
        for (int i = 0; i < to_read; i++) {
            buf[bytes_read + i] = cluster_buf[offset_in_cluster + i];
        }
        kfree(cluster_buf);
        
        bytes_read += to_read;
        handle->position += to_read;
        
        // Move to next cluster if needed
        if (handle->position % bytes_per_cluster == 0 && handle->position < handle->size) {
            handle->cluster = get_fat_entry(handle->cluster);
        }
    }
    
    return bytes_read;
}

int fat32_write(FAT32_FileHandle *handle, const void *buffer, int size) {
    if (!handle || !handle->valid || handle->mode == 0) return -1;

    const uint8_t *buf = (const uint8_t*)buffer;
    int bytes_written = 0;

    // Allocate first cluster if needed
    if (handle->cluster == 0) {
        uint32_t new_clus = find_free_cluster();
        if (new_clus == 0) return 0;
        set_fat_entry(new_clus, 0x0FFFFFFF);
        clear_cluster(new_clus);
        handle->cluster = new_clus;
        handle->start_cluster = new_clus;
        
        // Update directory entry start cluster
        uint8_t sec_buf[SECTOR_SIZE];
        ata_read_sectors(handle->dir_sector, 1, sec_buf);
        FAT32_DirEntry *ent = (FAT32_DirEntry*)(sec_buf + handle->dir_offset);
        ent->start_cluster_high = (new_clus >> 16);
        ent->start_cluster_low = (new_clus & 0xFFFF);
        ata_write_sectors(handle->dir_sector, 1, sec_buf);
    }

    while (bytes_written < size) {
        uint32_t offset_in_cluster = handle->position % bytes_per_cluster;
        int to_write = size - bytes_written;
        if ((uint32_t)to_write > bytes_per_cluster - offset_in_cluster) {
            to_write = bytes_per_cluster - offset_in_cluster;
        }

        // Read-Modify-Write cluster
        uint8_t *cluster_buf = (uint8_t*)kmalloc(bytes_per_cluster);
        if (!cluster_buf) return -1;
        uint32_t lba = cluster_to_lba(handle->cluster);
        ata_read_sectors(lba, sectors_per_cluster, cluster_buf);
        
        for(int i=0; i<to_write; i++) cluster_buf[offset_in_cluster + i] = buf[bytes_written + i];
        
        ata_write_sectors(lba, sectors_per_cluster, cluster_buf);
        kfree(cluster_buf);
        
        bytes_written += to_write;
        handle->position += to_write;
        if (handle->position > handle->size) handle->size = handle->position;

        if (handle->position % bytes_per_cluster == 0 && bytes_written < size) {
            uint32_t next = get_fat_entry(handle->cluster);
            if (next >= 0x0FFFFFF8) {
                uint32_t new_clus = find_free_cluster();
                if (new_clus == 0) break; // Disk full
                set_fat_entry(handle->cluster, new_clus);
                set_fat_entry(new_clus, 0x0FFFFFFF);
                clear_cluster(new_clus);
                handle->cluster = new_clus;
            } else {
                handle->cluster = next;
            }
        }
    }

    // Update file size in directory entry
    uint8_t sec_buf[SECTOR_SIZE];
    ata_read_sectors(handle->dir_sector, 1, sec_buf);
    FAT32_DirEntry *ent = (FAT32_DirEntry*)(sec_buf + handle->dir_offset);
    ent->file_size = handle->size;
    ata_write_sectors(handle->dir_sector, 1, sec_buf);

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
    FAT32_DirEntry dummy;
    if (resolve_path(path, &dummy)) return false; // Already exists

    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    // Split path
    char parent_path[FAT32_MAX_PATH];
    char dirname[256];
    int len = fs_strlen(normalized);
    int split = len;
    while(split > 0 && normalized[split] != '/') split--;
    
    if (split == 0) {
        fs_strcpy(parent_path, "/");
        fs_strcpy(dirname, normalized + (normalized[0] == '/' ? 1 : 0));
    } else {
        for(int i=0; i<split; i++) parent_path[i] = normalized[i];
        parent_path[split] = 0;
        fs_strcpy(dirname, normalized + split + 1);
    }
    
    FAT32_DirEntry parent;
    if (!resolve_path(parent_path, &parent)) return false;
    uint32_t parent_cluster = (parent.start_cluster_high << 16) | parent.start_cluster_low;
    if (parent_cluster == 0) parent_cluster = root_cluster;
    
    return create_entry(parent_cluster, dirname, ATTR_DIRECTORY, NULL);
}

bool fat32_rmdir(const char *path) {
    (void)path;
    return false; // Not implemented
}

bool fat32_delete(const char *path) {
    (void)path;
    return false; // Not implemented
}

bool fat32_exists(const char *path) {
    FAT32_DirEntry entry;
    return resolve_path(path, &entry);
}

bool fat32_is_directory(const char *path) {
    FAT32_DirEntry entry;
    return resolve_path(path, &entry) && (entry.attributes & ATTR_DIRECTORY);
}

int fat32_list_directory(const char *path, FAT32_FileInfo *entries, int max_entries) {
    FAT32_DirEntry dir_entry;
    if (!resolve_path(path, &dir_entry) || !(dir_entry.attributes & ATTR_DIRECTORY)) {
        return 0;
    }
    
    uint32_t current_cluster = (dir_entry.start_cluster_high << 16) | dir_entry.start_cluster_low;
    if (current_cluster == 0) current_cluster = root_cluster;
    
    int count = 0;
    uint8_t *buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!buf) return 0;

    char lfn_name[256];
    fs_memset(lfn_name, 0, 256);
    bool has_lfn = false;
    
    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8 && count < max_entries) {
        ata_read_sectors(cluster_to_lba(current_cluster), sectors_per_cluster, buf);
        FAT32_DirEntry *d = (FAT32_DirEntry*)buf;
        int entries_per_cluster = bytes_per_cluster / sizeof(FAT32_DirEntry);
        
        for (int i = 0; i < entries_per_cluster && count < max_entries; i++) {
            if (d[i].filename[0] == 0) { kfree(buf); return count; }
            if (d[i].filename[0] == 0xE5) { has_lfn = false; continue; }
            
            if (d[i].attributes == 0x0F) {
                FAT32_LFNEntry *lfn = (FAT32_LFNEntry*)&d[i];
                if (lfn->order & 0x40) {
                    fs_memset(lfn_name, 0, 256);
                    has_lfn = true;
                }
                if (has_lfn) {
                    int order = lfn->order & 0x3F;
                    char part[14];
                    extract_lfn_part(lfn, part);
                    int pos = (order - 1) * 13;
                    if (pos < 255) {
                        int part_len = fs_strlen(part);
                        for(int k=0; k<part_len && (pos+k)<255; k++) {
                            lfn_name[pos+k] = part[k];
                        }
                    }
                }
                continue;
            }
            
            if (has_lfn) {
                fs_strcpy(entries[count].name, lfn_name);
                has_lfn = false;
            } else {
                fat_name_to_str((const char*)d[i].filename, (const char*)d[i].extension, d[i].reserved, entries[count].name);
            }
            entries[count].size = d[i].file_size;
            entries[count].is_directory = (d[i].attributes & ATTR_DIRECTORY) != 0;
            entries[count].start_cluster = (d[i].start_cluster_high << 16) | d[i].start_cluster_low;
            count++;
        }
        current_cluster = get_fat_entry(current_cluster);
    }
    kfree(buf);
    
    return count;
}

bool fat32_chdir(const char *path) {
    char normalized[FAT32_MAX_PATH];
    fat32_normalize_path(path, normalized);
    
    FAT32_DirEntry entry;
    if (!resolve_path(normalized, &entry) || !(entry.attributes & ATTR_DIRECTORY)) {
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

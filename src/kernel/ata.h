#pragma once
#include <stdint.h>
#include <stdbool.h>

// Initialize ATA driver and find the FAT32 disk
bool ata_init(void);

// Read/Write sectors (LBA28)
// Returns 0 on success, non-zero on error
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer);
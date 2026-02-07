#include "ata.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SEC_CNT     0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_CMD         0x1F7

#define CMD_READ_PIO    0x20
#define CMD_WRITE_PIO   0x30
#define CMD_IDENTIFY    0xEC

// IO Port Helpers
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void insw(uint16_t port, void *addr, uint32_t cnt) {
    __asm__ volatile ("rep insw" : "+D"(addr), "+c"(cnt) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t cnt) {
    __asm__ volatile ("rep outsw" : "+S"(addr), "+c"(cnt) : "d"(port) : "memory");
}

static void ata_wait_bsy(void) {
    while (inb(ATA_STATUS) & 0x80);
}

static void ata_wait_drq(void) {
    while (!(inb(ATA_STATUS) & 0x08));
}

// Select drive (0 = Master, 1 = Slave)
static void ata_select_drive(int drive) {
    outb(ATA_DRIVE_HEAD, 0xE0 | (drive << 4));
    // Small delay
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
}

bool ata_init(void) {
    // Simple probe: Try to identify Primary Slave (where we put disk.img in QEMU index=1)
    // Note: QEMU index=0 is usually the CDROM if -boot d is used, or Primary Master.
    // We will try to read from the drive to verify it exists.
    ata_select_drive(1); // Select Slave
    outb(ATA_SEC_CNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, CMD_IDENTIFY);
    
    uint8_t status = inb(ATA_STATUS);
    if (status == 0) return false; // No drive
    
    // Poll until BSY clears
    while(inb(ATA_STATUS) & 0x80);
    
    // Check for ATAPI signature (we don't want the CDROM)
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) {
        return false; // Not ATA
    }
    
    // Read 256 words of identify data to clear buffer
    uint16_t buffer[256];
    ata_wait_drq();
    insw(ATA_DATA, buffer, 256);
    
    return true;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer) {
    ata_wait_bsy();
    ata_select_drive(1); // Always using Slave for storage
    
    outb(ATA_SEC_CNT, count);
    outb(ATA_LBA_LO, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_DRIVE_HEAD, 0xE0 | (1 << 4) | ((lba >> 24) & 0x0F));
    outb(ATA_CMD, CMD_READ_PIO);
    
    uint16_t *buf = (uint16_t*)buffer;
    for (int i = 0; i < count; i++) {
        ata_wait_bsy();
        ata_wait_drq();
        insw(ATA_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer) {
    ata_wait_bsy();
    ata_select_drive(1);
    
    outb(ATA_SEC_CNT, count);
    outb(ATA_LBA_LO, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_DRIVE_HEAD, 0xE0 | (1 << 4) | ((lba >> 24) & 0x0F));
    outb(ATA_CMD, CMD_WRITE_PIO);
    
    const uint16_t *buf = (const uint16_t*)buffer;
    for (int i = 0; i < count; i++) {
        ata_wait_bsy();
        ata_wait_drq();
        outsw(ATA_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}
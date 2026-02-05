#include <stdint.h>
#include "pci.h"
#include "io.h"

uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)((1u << 31) | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((1u << 31) | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

int pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pci_get_vendor_id(bus, device, function);
    return vendor_id != 0xFFFF;
}

uint16_t pci_get_vendor_id(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t config = pci_read_config(bus, device, function, 0x00);
    return (uint16_t)(config & 0xFFFF);
}

uint16_t pci_get_device_id(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t config = pci_read_config(bus, device, function, 0x00);
    return (uint16_t)((config >> 16) & 0xFFFF);
}

uint8_t pci_get_class_code(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t config = pci_read_config(bus, device, function, 0x08);
    return (uint8_t)((config >> 24) & 0xFF);
}

uint8_t pci_get_subclass(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t config = pci_read_config(bus, device, function, 0x08);
    return (uint8_t)((config >> 16) & 0xFF);
}

uint8_t pci_get_prog_if(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t config = pci_read_config(bus, device, function, 0x08);
    return (uint8_t)((config >> 8) & 0xFF);
}

int pci_enumerate_devices(pci_device_t* devices, int max_devices) {
    int count = 0;
    for (uint8_t bus = 0; bus < 256 && count < max_devices; bus++) {
        for (uint8_t dev = 0; dev < 32 && count < max_devices; dev++) {
            if (pci_device_exists(bus, dev, 0)) {
                uint32_t config_val = pci_read_config(bus, dev, 0, 0x0C);
                uint8_t header_type = (uint8_t)((config_val >> 16) & 0xFF);
                uint8_t num_functions = (header_type & 0x80) ? 8 : 1;
                for (uint8_t fn = 0; fn < num_functions && count < max_devices; fn++) {
                    if (pci_device_exists(bus, dev, fn)) {
                        devices[count].bus = bus;
                        devices[count].device = dev;
                        devices[count].function = fn;
                        devices[count].vendor_id = pci_get_vendor_id(bus, dev, fn);
                        devices[count].device_id = pci_get_device_id(bus, dev, fn);
                        devices[count].class_code = pci_get_class_code(bus, dev, fn);
                        devices[count].subclass = pci_get_subclass(bus, dev, fn);
                        devices[count].prog_if = pci_get_prog_if(bus, dev, fn);
                        count++;
                    }
                }
            }
        }
    }
    return count;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* device) {
    pci_device_t devices[32];
    int count = pci_enumerate_devices(devices, 32);
    for (int i = 0; i < count; i++) {
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id) {
            *device = devices[i];
            return 1;
        }
    }
    return 0;
}

int pci_find_device_by_class(uint8_t class_code, uint8_t subclass, pci_device_t* device) {
    pci_device_t devices[32];
    int count = pci_enumerate_devices(devices, 32);
    for (int i = 0; i < count; i++) {
        if (devices[i].class_code == class_code && devices[i].subclass == subclass) {
            *device = devices[i];
            return 1;
        }
    }
    return 0;
}

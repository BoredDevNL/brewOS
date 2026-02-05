#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
} pci_device_t;

#define PCI_CLASS_NETWORK_CONTROLLER  0x02
#define PCI_CLASS_ETHERNET_CONTROLLER 0x00

uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
int pci_device_exists(uint8_t bus, uint8_t device, uint8_t function);
uint16_t pci_get_vendor_id(uint8_t bus, uint8_t device, uint8_t function);
uint16_t pci_get_device_id(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_class_code(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_subclass(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_prog_if(uint8_t bus, uint8_t device, uint8_t function);
int pci_enumerate_devices(pci_device_t* devices, int max_devices);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* device);
int pci_find_device_by_class(uint8_t class_code, uint8_t subclass, pci_device_t* device);

#endif

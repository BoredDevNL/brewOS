#include <stdint.h>
#include "limine.h"
#include <stddef.h>
static volatile struct limine_hhdm_request hhdm_request __attribute__((used, section(".requests"))) = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL
};
static volatile struct limine_kernel_address_request kernel_addr_request __attribute__((used, section(".requests"))) = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
    .response = NULL
};
static uint64_t hhdm_offset = 0;
static uint64_t kernel_phys_base = 0;
static uint64_t kernel_virt_base = 0;
void platform_init(void) {
    if (hhdm_request.response) { hhdm_offset = hhdm_request.response->offset; }
    if (kernel_addr_request.response) {
        kernel_phys_base = kernel_addr_request.response->physical_base;
        kernel_virt_base = kernel_addr_request.response->virtual_base;
    }
}
uint64_t p2v(uint64_t phys) { return phys + hhdm_offset; }
uint64_t v2p(uint64_t virt) {
    if (kernel_virt_base && virt >= kernel_virt_base) {
        return virt - kernel_virt_base + kernel_phys_base;
    }
    if (hhdm_offset && virt >= hhdm_offset) {
        return virt - hhdm_offset;
    }
    return virt;
}

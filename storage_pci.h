#ifndef STORAGE_PCI_H
#define STORAGE_PCI_H
#include <stdint.h>
#include "multiboot.h"
void storage_pci_boot_memory(const MultibootInfo *info, void (*progress)(unsigned int bus));
/* Shared PCI MMIO allocator, also used by USB host controllers. */
unsigned int storage_pci_mmio_bar(unsigned int pci, unsigned int bar);
uint64_t storage_pci_lower_framebuffer(uint64_t address, int force);
int storage_pci_clear_large_window(void);
#define PCI_STORAGE_MAX 32
void storage_pci_init(void);
int storage_pci_count(void);
const char *storage_pci_name(int index);
const char *storage_pci_type(int index);
uint64_t storage_pci_capacity(int index);
int storage_pci_present(int index);
int storage_pci_read(int index, unsigned int lba, void *data, unsigned int sectors);
int storage_pci_write(int index, unsigned int lba, const void *data, unsigned int sectors);
int storage_pci_flush(int index);
#endif
